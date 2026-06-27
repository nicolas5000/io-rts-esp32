/**
 * @file IoHomeControl.cpp
 * @brief io-homecontrol Node Controller Implementation
 * @author iown-homecontrol project
 */

#include "IoHomeControl.hpp"
#include "iohome_commands.hpp"
#include "EncodingHelpers.hpp"
#include "oled_display.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "freertos/task.h"

#include <stdio.h>
#include <time.h>
#include <algorithm>
#include <cmath>
#include "esp_log.h"
#include <ios>
#include <sstream>
#include <map>
#include <vector>
#include <list>
#include <iostream>
#include <iomanip>
#include <format>
#include <mutex>

static const char *TAG = "io-hctrl";

constexpr TickType_t MUTEX_MAX_WAIT_TICKS = 30000 * portTICK_PERIOD_MS;                     // 30 seconds
constexpr TickType_t RECEIVED_IO_TREATMENT_WAIT_TICKS = 500 * portTICK_PERIOD_MS;           // 500 ms
constexpr TickType_t RECEIVED_IO_DISCOVERY_RESPONSE_WAIT_TICKS = 2000 * portTICK_PERIOD_MS; // 2s, I have seen devices between 1s and 1.5s!

constexpr TickType_t UPDATE_STATUS_WAKEUP_INTERVAL_MS = 1000; // 1 second
constexpr TickType_t TIME_BETWEEN_RETRY_MS = 250;             // 250 ms

constexpr UBaseType_t RADIO_FRAME_PROCESSING_PRIORITY = tskIDLE_PRIORITY + 8; // priority higher than IDLE as we want relevant frequency hopping
constexpr UBaseType_t IO_FRAME_PROCESSING_TASK = tskIDLE_PRIORITY + 6;        // priority higher than IDLE but less than radio, to perform IO frame work
constexpr UBaseType_t DEVICE_STATUS_UPDATE_PRIORITY = tskIDLE_PRIORITY + 4;   // priority higher than IDLE but less than IO processing, to launch status update
constexpr UBaseType_t DEVICE_STATUS_CALLBACK_PRIORITY = tskIDLE_PRIORITY;     // priority same as IDLE (low priority)
constexpr UBaseType_t LOG_CALLBACK_PRIORITY = tskIDLE_PRIORITY;               // priority same as IDLE (low priority)

constexpr int64_t STATUS_UPDATE_MAX_TIME_US = 3600000000;   // 1 hour to next status update if nothing happens before
constexpr int64_t STATUS_UPDATE_AUTO_MARGIN_US = 60000000;  // 60 seconds to wait for automatic update from device
constexpr int64_t STATUS_UPDATE_MANUAL_MARGIN_US = 1000000; // 1 second to wait before asking manual update from device
constexpr int64_t STATUS_UPDATE_NEXT_TRY_US = 60000000;     // 60 seconds to wait if GetStatus failed
constexpr int64_t STATUS_UPDATE_AFTER_REMOTE_US = 2000000;  // 2 seconds after detection of a frame sent by a remote

constexpr size_t LOG_MESSAGE_MAXSIZE = 256;

#define IO_LOGE(a, ...)                                              \
  do                                                                 \
  {                                                                  \
    IoLog(ESP_LOG_ERROR, std::format(a __VA_OPT__(, ) __VA_ARGS__)); \
  } while (0)
#define IO_LOGI(a, ...)                                             \
  do                                                                \
  {                                                                 \
    IoLog(ESP_LOG_INFO, std::format(a __VA_OPT__(, ) __VA_ARGS__)); \
  } while (0)

#define EVENT_BIT_RX (1 << 0) // Event bit when RxFrameQueueItem is pushed in sRxIoQueue
#define EVENT_BIT_TX (1 << 1) // Event bit when TxFrameQueueItem is pushed in sTxIoQueue

using namespace RadioLinks;

namespace iohome
{
  static EventGroupHandle_t sEventGroup;                           // Handle for RX and TX events in RxIoQueue and TxIoQueue
  static QueueHandle_t sRadioRxFramesQueue = NULL;                 // Contains RAW frames (RadioRxFrameQueueItem) received from radio layer
  static QueueHandle_t sRxIoQueue = NULL;                          // Contains IO frames (RxFrameQueueItem) received from radio layer
  static QueueHandle_t sTxIoQueue = NULL;                          // Contains IO frames (TxFrameQueueItem) to be sent to radio layer
  static QueueHandle_t sLogQueue = NULL;                           // Contains logs (LogQueueItem) to be sent to log callback
  static QueueHandle_t sIoDeviceStatusQueue = NULL;                // Contains IoDevice status (IoDevice items) to be sent to status callback
  static SemaphoreHandle_t sMutex;                                 // Mutex to use when accessing IO queues
  static StaticSemaphore_t sMutexBuffer;                           // Memory allocated staticly to the mutex (allocation cannot fail at runtime)
  static std::map<std::string, IoDevice> sDeviceMap;               // Map of all known devices
  static std::map<std::string, std::list<std::string>> sRemoteMap; // Map of remotes and the devices they control, protected by sRemoteMapMutex
  static std::mutex sRemoteMapMutex;                               // Mutex to protect sRemoteMap
  static LoggerCallback sLoggerCallback;                           // Callback to send logs to
  static UpdatedDeviceCallback sDeviceStatusCallback;              // Callback to send device status updates to
  static UnknownSenderCallback sUnknownSenderCallback = nullptr;   // Callback for frames from unregistered senders
  static KeySniffCallback sKeySniffCallback = nullptr;             // Callback invoked when a key is captured during sniffing
  static MovementStartedCallback sMovementStartedCallback = nullptr; // Callback invoked when movement tracking starts
  static volatile bool sSniffKeyActive = false;                    // true while passive key sniffing is active
  static char sSniffedKey[33] = {};                                // last captured key as 32-char hex + null
  static int64_t sSniffStartUs = 0;                                // timestamp when sniffing started

  constexpr int64_t KEY_SNIFF_TIMEOUT_US = 120LL * 1000000LL;     // 120 s auto-stop

  struct RadioRxFrameQueueItem
  {
    int64_t timestamp;             // Timestamp of the frame when added to the queue, in us (use esp_timer_get_time() to fill)
    int64_t preamble_time;         // Time between preamble is detected and frame is received, in us (only available if DIO4 is wired)
    uint8_t frame[FRAME_MAX_SIZE]; // RAW frame
    uint8_t frame_len;             // Actual length of frame
    uint32_t frequency;            // Frequency used to receive packet
    float rssi;                    // RSSI value in dBm
  };

  struct RxFrameQueueItem
  {
    uint32_t frequency; // Frequency used to receive packet
    float rssi;         // RSSI value in dBm
    IoFrame frame;      // IO frame
  };

  struct TxFrameQueueItem
  {
    uint32_t frequency; // Frequency used to send
    uint16_t preamble;  // Preamble length before frame
    IoFrame frame;      // IO frame
  };

  struct LogQueueItem
  {
    char log[LOG_MESSAGE_MAXSIZE];
    uint32_t timestamp;
    esp_log_level_t log_level;
  };

  /// @brief Generate a log from given string and level, and send it to registered callback
  /// @param log_level Log level
  /// @param log Log string
  static void IoLog(esp_log_level_t log_level, std::string log)
  {
    LogQueueItem logItem;
    memset(&logItem, 0, sizeof(LogQueueItem));
    logItem.log_level = log_level;
    logItem.timestamp = esp_log_timestamp();
    size_t messageSize = log.length() < LOG_MESSAGE_MAXSIZE ? log.length() : LOG_MESSAGE_MAXSIZE - 1;
    memcpy(logItem.log, log.c_str(), messageSize);
    if (!xQueueSendToBack(sLogQueue, &logItem, 0))
    {
      ESP_LOGE(TAG, "IoLog can't add log to queue!");
    }
  }

  /// @brief Convert a buffer to a HEX string representation
  /// (0x12 0x34 0xab -> "1234AB")
  /// @param len length of the input buffer
  /// @param buffer Buffer containing the bytes
  /// @return The string containing the HEX representation of the input buffer
  static std::string buffToHexString(uint8_t len, const uint8_t buffer[])
  {
    std::ostringstream convert;
    for (int a = 0; a < len; a++)
    {
      convert << std::format("{:02X}", buffer[a]);
    }
    return convert.str();
  }

  /// @brief Convert an HEX string to a byte buffer
  /// @param hex HEX string (eg "1234AB")
  /// @param buffer Buffer that will receive the string conversion (eg, will be 0x12 0x34 0xab)
  /// @param len Length of the allocated buffer, needs to be at least HEX string length / 2
  static void HexStringToBuff(const std::string &hex, uint8_t buffer[], uint8_t len)
  {
    if (len < hex.length() / 2)
    {
      IO_LOGE("HexStringToBuff: buffer is too short! ({}, need {})", len, hex.length() / 2);
      return;
    }
    std::istringstream ss(hex);
    std::string s2;
    unsigned int i = 0;
    while ((ss >> std::setw(2) >> s2))
    {
      buffer[i / 2] = (uint8_t)strtol(s2.c_str(), nullptr, 16);
      i += 2;
    }
  }

  /// @brief Low priority task that takes logs from queue and send them to registered callback
  /// @param arg Pointer to IoHomeControl object
  static void process_log_task(void *arg)
  {
    IoHomeControl *ioHome = static_cast<IoHomeControl *>(arg);
    for (;;)
    {
      LogQueueItem logItem;
      if (xQueueReceive(sLogQueue, &logItem, portMAX_DELAY))
      {
        if (ioHome->isVerbose() && sLoggerCallback)
        {
          sLoggerCallback(logItem.log_level, TAG, std::format("({}) {}", logItem.timestamp, logItem.log));
        }
      }
    }
  }

  /// @brief Low priority task that takes devices status from queue and send them to registered callback
  /// @param arg currently not used
  static void process_iodevicestatus_task(void *arg)
  {
    for (;;)
    {
      IoDevice device;
      if (xQueueReceive(sIoDeviceStatusQueue, &device, portMAX_DELAY))
      {
        if (sDeviceStatusCallback && !device.is_deleted) // we have a callback and device is not marked deleted
        {
          sDeviceStatusCallback(buffToHexString(NODE_ID_SIZE, device.info.node_id), device);
        }
      }
    }
  }

  /// @brief Function callback called by radio layer when a frame is received. Puts the frame to queue
  /// @param len Length of the frame received by radio layer
  /// @param buffer Frame data received by radio layer
  /// @param frequency Frequency used to receive the frame
  /// @param rssi RSSI of the received data
  /// @param preamble_time time between preamble is detected and sync word is detected
  static void received_frame_handler(uint8_t len, uint8_t buffer[], uint32_t frequency, float rssi, int64_t preamble_time)
  {
    if (len > FRAME_MAX_SIZE)
    {
      IO_LOGE("received_frame_handler received frame too long! ({})", len);
      return;
    }
    RadioRxFrameQueueItem item;
    item.timestamp = esp_timer_get_time();
    item.preamble_time = preamble_time;
    item.frame_len = len;
    item.frequency = frequency;
    item.rssi = rssi;
    memcpy(item.frame, buffer, len);
    if (!xQueueSendToBack(sRadioRxFramesQueue, &item, 0))
    {
      IO_LOGE("received_frame_handler can't add received frame to queue!");
    }
    else
    {
      xEventGroupSetBits(sEventGroup, EVENT_BIT_RX);
    }
  }

  /// @brief Task processing received IO frames
  /// @param arg Pointer to IoHomeControl object
  static void process_ioframe_task(void *arg)
  {
    IoHomeControl *ioHome = static_cast<IoHomeControl *>(arg);
    ioHome->ProcessReceivedFrameTask();
  }

  /// @brief Task responsible for updating IO devices status when needed
  /// @param arg Pointer to IoHomeControl object
  static void update_devices_status_task(void *arg)
  {
    IoHomeControl *ioHome = static_cast<IoHomeControl *>(arg);
    ioHome->UpdateDevicesStatusTask();
  }

  /// @brief Task responsible for processing received frames and sending frames to radio layer
  /// It processes RAW frames from sRadioRxFramesQueue and pushs IO frames to sRxIoQueue
  /// It processes IO frames from sTxIoQueue and sends them to radio
  /// It changes radio frequency depending on currently receiving or sending frames and waiting for response.
  /// @param arg Pointer to IoHomeControl object
  static void process_radio_task(void *arg)
  {
    RadioRxFrameQueueItem item;
    IoHomeControl *ioHome = static_cast<IoHomeControl *>(arg);
    uint32_t currentFrequency = FREQUENCY_CHANNEL_2;
    TickType_t waitTime = CHANNEL_HOP_TIME_US * portTICK_PERIOD_MS / 1000;
    for (;;)
    {
      if (uxQueueMessagesWaiting(sRadioRxFramesQueue) == 0 && uxQueueMessagesWaiting(sTxIoQueue) == 0)
      {
        // Wait for RX or TX event
        xEventGroupWaitBits(sEventGroup, EVENT_BIT_RX | EVENT_BIT_TX, pdTRUE, pdFALSE, waitTime);
      }
      if (xQueueReceive(sRadioRxFramesQueue, &item, 0))
      {
        // Process received frame
        RxFrameQueueItem rxFrame;
        // Parse frame
        if (!parse_frame(item.frame, item.frame_len, rxFrame.frame))
        {
          IO_LOGE("Error: Frame parsing failed");
        }
        else
        {
          rxFrame.frequency = item.frequency;
          rxFrame.rssi = item.rssi;
          IO_LOGI("Received at {} ({}us preamble) - RSSI {:.1f}", item.timestamp, item.preamble_time, item.rssi);
          IO_LOGI("Rcvd ({:.2f}) command {:02X} from {} to {} CTRL0 {:02X} CTRL1 {:02X} - {} bytes: {}",
                  rxFrame.frequency / 1000000.0, rxFrame.frame.command_id, buffToHexString(NODE_ID_SIZE, rxFrame.frame.src_node), buffToHexString(NODE_ID_SIZE, rxFrame.frame.dest_node),
                  rxFrame.frame.ctrl_byte_0, rxFrame.frame.ctrl_byte_1, rxFrame.frame.data_len, buffToHexString(rxFrame.frame.data_len, rxFrame.frame.data));
          {
            char cmd_hex[3];
            snprintf(cmd_hex, sizeof(cmd_hex), "%02X", rxFrame.frame.command_id);
            oled_show_rx(buffToHexString(NODE_ID_SIZE, rxFrame.frame.src_node).c_str(), cmd_hex, -1, (int)item.rssi);
          }
          if (!xQueueSendToBack(sRxIoQueue, &rxFrame, 0))
          {
            IO_LOGE("process_radio_task can't add received frame to IO queue!");
          }
          if (is_2w_mode(rxFrame.frame) && (is_start(rxFrame.frame) || !is_end(rxFrame.frame)) && ioHome->isPassive()) // if passive mode we want to capture responses
          {
            waitTime = CHANNEL_RESPONSE_TIME_US * portTICK_PERIOD_MS / 1000;
          }
          else
          {
            waitTime = CHANNEL_HOP_TIME_US * portTICK_PERIOD_MS / 1000;
          }
        }
      }
      else if (ioHome->isReceiving()                                                              // still receiving enabled
               && (ioHome->mRadio->isSyncWordDetected() || ioHome->mRadio->isPreambleDetected())) // Preamble or sync word detected
      {
        waitTime = CHANNEL_PREAMBLE_TIME_US * portTICK_PERIOD_MS / 1000; // We will see soon if frame arrives...
        // IO_LOGI("process_radio_task: Preamble or sync word detected");
      }
      else if (ioHome->isReceiving()                     // still receiving enabled
               && !ioHome->mRadio->isSyncWordDetected()  // No sync word detected
               && !ioHome->mRadio->isPreambleDetected()) // No preamble detected
      {
        TxFrameQueueItem item;
        if (xQueueReceive(sTxIoQueue, &item, 0))
        {
          IO_LOGI("Send ({:.2f}) command {:02X} from {} to {} CTRL0 {:02X} CTRL1 {:02X} - {} bytes: {}",
                  item.frequency / 1000000.0, item.frame.command_id, buffToHexString(NODE_ID_SIZE, item.frame.src_node), buffToHexString(NODE_ID_SIZE, item.frame.dest_node),
                  item.frame.ctrl_byte_0, item.frame.ctrl_byte_1, item.frame.data_len, buffToHexString(item.frame.data_len, item.frame.data));
          {
            char cmd_hex[5];
            snprintf(cmd_hex, sizeof(cmd_hex), "0x%02X", item.frame.command_id);
            oled_show_tx(cmd_hex, buffToHexString(NODE_ID_SIZE, item.frame.dest_node).c_str());
          }
          // Process frame to send
          uint16_t preamble = item.preamble;
          // Transmit
          uint8_t buffer[FRAME_MAX_SIZE];
          uint8_t size = serialize_frame(item.frame, buffer, sizeof(buffer));
          if (ioHome->mRadio->Send(size, buffer, preamble, item.frequency))
            IO_LOGE("process_radio_task: transmit failed");
          if (is_start(item.frame)) // Start flag: wait for a response
          {
            waitTime = CHANNEL_RESPONSE_START_TIME_US * portTICK_PERIOD_MS / 1000;
          }
          else if (is_end(item.frame)) // Wait up to next hop if nothing received before (no retry, no waiting for response)
          {
            waitTime = 2 * CHANNEL_HOP_TIME_US * portTICK_PERIOD_MS / 1000;
          }
          else // if (!is_start(item.frame)) // Wait for a response
          {
            waitTime = CHANNEL_RESPONSE_TIME_US * portTICK_PERIOD_MS / 1000;
          }
          /*else // Start flag without end flag: wait for a response
          {
            waitTime = CHANNEL_RESPONSE_START_TIME_US * portTICK_PERIOD_MS / 1000;
          }*/
        }
        else
        {
          // Nothing received and nothing to send: frequency hopping! 1 -> 2 -> 3 -> 1 ...
          uint32_t nextFrequency;
          switch (currentFrequency)
          {
          case FREQUENCY_CHANNEL_1:
            nextFrequency = FREQUENCY_CHANNEL_2; // we need CHANNEL_2 to snif commands sent by remotes
            break;
          case FREQUENCY_CHANNEL_3:
            nextFrequency = FREQUENCY_CHANNEL_1;
            break;
          case FREQUENCY_CHANNEL_2:
          default:
            nextFrequency = FREQUENCY_CHANNEL_3;
            break;
          }
          if (ioHome->mRadio->SetFrequency(nextFrequency) != RADIO_ERR_NONE)
            IO_LOGE("Error: Frequency hopping failed");
          else
            currentFrequency = nextFrequency;
          waitTime = CHANNEL_HOP_TIME_US * portTICK_PERIOD_MS / 1000; // Wait up to next hop if nothing received before
        }
      }
    }
  }

  IoHomeControl::IoHomeControl(RadioModule *radio, LoggerCallback logger, UpdatedDeviceCallback deviceStatusCallback)
      : mRadio(radio),
        mInitialized(false),
        mReceiving(false),
        mVerbose(true),
        mIgnoreAutoUpdate(false)
  {
    memset(mOwnNodeId, 0, NODE_ID_SIZE);
    memset(mSystemKey, 0, AES_KEY_SIZE);

    sLoggerCallback = logger;
    sDeviceStatusCallback = deviceStatusCallback;

    // Create logger queue and task
    sLogQueue = xQueueCreate(50, sizeof(LogQueueItem));
    xTaskCreate(process_log_task, "process_log_task", 4096, this, LOG_CALLBACK_PRIORITY, NULL);

    // Create IoDevice status queue and task
    sIoDeviceStatusQueue = xQueueCreate(20, sizeof(IoDevice));
    xTaskCreate(process_iodevicestatus_task, "process_iodevicestatus_task", 4096, NULL, DEVICE_STATUS_CALLBACK_PRIORITY, NULL);

    // Initialize mutex
    sMutex = xSemaphoreCreateMutexStatic(&sMutexBuffer);

    // create event group
    sEventGroup = xEventGroupCreate();
    // create queue to handle radio frames
    sRadioRxFramesQueue = xQueueCreate(10, sizeof(RadioRxFrameQueueItem));
    // create queues to handle IO frames
    sRxIoQueue = xQueueCreate(10, sizeof(RxFrameQueueItem));
    sTxIoQueue = xQueueCreate(10, sizeof(TxFrameQueueItem));
    // High-priority tasks (P8/P6/P4) are started later via StartTasks(),
    // after LoadIoDevicesFromStorage() has populated sDeviceMap.
  }

  void IoHomeControl::StartTasks()
  {
    xTaskCreate(process_radio_task, "process_radio_task", 4096, this, RADIO_FRAME_PROCESSING_PRIORITY, NULL);
    xTaskCreate(process_ioframe_task, "process_ioframe_task", 4096, this, IO_FRAME_PROCESSING_TASK, NULL);
    xTaskCreate(update_devices_status_task, "update_devices_status_task", 4096, this, DEVICE_STATUS_UPDATE_PRIORITY, NULL);
  }

  bool IoHomeControl::Begin(
      const std::string &own_node_id,
      const std::string &system_key,
      bool isPassive)
  {
    HexStringToBuff(own_node_id, mOwnNodeId, NODE_ID_SIZE);
    HexStringToBuff(system_key, mSystemKey, AES_KEY_SIZE);
    mPassiveMode = isPassive;

    IO_LOGI("Node ID: {}", own_node_id);

    return true;
  }

  void IoHomeControl::SetUnknownSenderCallback(UnknownSenderCallback cb)
  {
    sUnknownSenderCallback = cb;
  }

  int16_t IoHomeControl::ConfigureRadio(uint8_t power)
  {
    if (mRadio == nullptr)
    {
      IO_LOGE("Error: Radio not initialized");
      return RADIO_ERR_NULL_POINTER;
    }

    RADIO_ERRCODE state;

    mRadio->Init(true);

    // Set modulation
    state = mRadio->SetModulation(RadioLinks::FSK);
    if (state != RADIO_ERR_NONE)
    {
      IO_LOGE("Error: SetModulation failed ({})", (int16_t)state);
      return state;
    }

    // Set bandwidth
    state = mRadio->SetBandwidth(BAND_WIDTH);
    if (state != RADIO_ERR_NONE)
    {
      IO_LOGE("Error: SetBandwidth failed ({})", (int16_t)state);
      return state;
    }

    // Set bitrate
    mRadio->SetBitRate(BIT_RATE); // 38.4 kbps
    if (state != RADIO_ERR_NONE)
    {
      IO_LOGE("Error: SetBitRate failed ({})", (int16_t)state);
      return state;
    }

    // Set frequency
    state = mRadio->SetFrequency(FREQUENCY_CHANNEL_2);
    if (state != RADIO_ERR_NONE)
    {
      IO_LOGE("Error: SetFrequency failed ({})", (int16_t)state);
      return state;
    }

    // Set frequency deviation
    mRadio->SetFrequencyDeviation(FREQ_DEVIATION); // 19.2 kHz
    if (state != RADIO_ERR_NONE)
    {
      IO_LOGE("Error: SetFrequencyDeviation failed ({})", (int16_t)state);
      return state;
    }

    // Set output power
    mRadio->SetOutputPower(power);
    if (state != RADIO_ERR_NONE)
    {
      IO_LOGE("Error: SetOutputPower failed ({})", (int16_t)state);
      return state;
    }

    // Set preamble
    state = mRadio->SetPreambleLength(LONG_PREAMBLE_LENGTH);
    if (state != RADIO_ERR_NONE)
    {
      IO_LOGE("Error: setPreambleLength failed ({})", (int16_t)state);
      return state;
    }

    // Set sync word (0x33FF55, 3 bytes)
    uint8_t sync_word[SYNC_WORD_LEN];
    sync_word[2] = (SYNC_WORD >> 16) & 0xFF;
    sync_word[1] = (SYNC_WORD >> 8) & 0xFF;
    sync_word[0] = SYNC_WORD & 0xFF;

    state = mRadio->SetSyncWord(SYNC_WORD_LEN, sync_word);
    if (state != RADIO_ERR_NONE)
    {
      IO_LOGE("Error: setSyncWord failed ({})", (int16_t)state);
      return state;
    }

    mRadio->RegisterReceiveCallback(received_frame_handler);

    IO_LOGI("Radio configured successfully");
    mInitialized = true;
    return RADIO_ERR_NONE;
  }

  int16_t IoHomeControl::StartReceive()
  {
    if (!mInitialized)
    {
      IO_LOGE("Error: Not initialized");
      return RADIO_ERR_NOT_INITIALIZED;
    }

    int16_t state = mRadio->StartReceive();
    if (state != RADIO_ERR_NONE)
    {
      IO_LOGE("Error: startReceive failed ({})", (int16_t)state);
      return state;
    }

    mReceiving = true;
    IO_LOGI("Receiving started");
    return RADIO_ERR_NONE;
  }

  void IoHomeControl::StopReceive()
  {
    if (mReceiving)
    {
      mRadio->StopReceive();
      mReceiving = false;
      IO_LOGI("Receiving stopped");
    }
  }

  void IoHomeControl::ProcessReceivedFrameTask()
  {
    for (;;)
    {
      if (xSemaphoreTake(sMutex, MUTEX_MAX_WAIT_TICKS))
      {
        RxFrameQueueItem item;
        if (xQueueReceive(sRxIoQueue, &item, RECEIVED_IO_TREATMENT_WAIT_TICKS))
        {
          // Auto-stop key sniffing after timeout
          if (sSniffKeyActive && (esp_timer_get_time() - sSniffStartUs) > KEY_SNIFF_TIMEOUT_US)
          {
            sSniffKeyActive = false;
            IO_LOGI("Key sniffing timed out after 120 s");
          }
          std::string dstDevice = buffToHexString(NODE_ID_SIZE, item.frame.dest_node);
          std::string srcDevice = buffToHexString(NODE_ID_SIZE, item.frame.src_node);
          switch (item.frame.command_id)
          {
          case CMD_PRIVATE_RESPONSE: // New status response to request sent by another box
                                     // case CMD_PRIVATE2_RESPONSE:
            if (isPassive())         // Passive mode
            {
              // Update directly
              UpdateDeviceStatus(item.frame);
            }
            else if (sDeviceMap.contains(srcDevice))
            {
              // Update directly
              UpdateDeviceStatus(item.frame);
            }
            break;
          case CMD_KEY_INIT_TRANSFER: // Pairing request sent by another box
            if (isPassive())
            {
              // Listen and get the key!
              RxFrameQueueItem challengeItem, keyTransferItem;
              uint8_t decrypted_key[AES_KEY_SIZE];
              if (xQueueReceive(sRxIoQueue, &challengeItem, RECEIVED_IO_TREATMENT_WAIT_TICKS)      // Did we receive a frame?
                  && challengeItem.frame.command_id == CMD_CHALLENGE_REQUEST                        // Is it a challenge?
                  && challengeItem.frame.data_len == HMAC_SIZE                                      // Does it contain correct data length?
                  && xQueueReceive(sRxIoQueue, &keyTransferItem, RECEIVED_IO_TREATMENT_WAIT_TICKS) // Did we receive another frame?
                  && keyTransferItem.frame.command_id == CMD_KEY_TRANSFER                           // Is it a key transfer?
                  && keyTransferItem.frame.data_len == AES_KEY_SIZE)                                // Does it contain correct data length?
              {
                uint8_t challenge_fd[1 + HMAC_SIZE];
                challenge_fd[0] = challengeItem.frame.command_id;
                memcpy(challenge_fd + 1, challengeItem.frame.data, HMAC_SIZE);
                if (iohome::crypto::crypt_2w_key(challenge_fd, sizeof(challenge_fd), challengeItem.frame.data, keyTransferItem.frame.data, decrypted_key))
                {
                  std::string hexKey = buffToHexString(AES_KEY_SIZE, decrypted_key);
                  IO_LOGI("Extracted a key to control device {}: {}", buffToHexString(NODE_ID_SIZE, keyTransferItem.frame.dest_node), hexKey);
                  if (sSniffKeyActive)
                  {
                    strncpy(sSniffedKey, hexKey.c_str(), 32);
                    sSniffedKey[32] = '\0';
                    sSniffKeyActive = false;
                    if (sKeySniffCallback)
                      sKeySniffCallback(hexKey);
                  }
                }
                else
                {
                  IO_LOGE("ProcessReceivedFrameTask - Failed to decrypt the key!");
                }
              }
              else
              {
                IO_LOGE("ProcessReceivedFrameTask - Failed to receive challenge/key frames!");
              }
            }
            break;
          case CMD_STATUS_UPDATE: // Status sent by a device to us or to another box
            if (isPassive())      // Passive mode
            {
              // Update directly
              UpdateDeviceStatus(item.frame);
            }
            else if (sDeviceMap.contains(srcDevice)) // Active mode and we know the device
            {
              if (memcmp(item.frame.dest_node, mOwnNodeId, NODE_ID_SIZE) == 0)
              {
                // We are the destination and active mode: challenge before accepting status update!
                if (AuthenticateReceivedRequest(item.frame, item.frequency))
                {
                  // Authenticated so reply OK and let's use the status update received from device
                  IoFrame end;
                  create_status_update_response(end, mOwnNodeId, item.frame.src_node); // reply OK
                  TransmitFrame(end, FREQUENCY_CHANNEL_1, SHORT_PREAMBLE_LENGTH);      // send response
                  TransmitFrame(end, FREQUENCY_CHANNEL_2, SHORT_PREAMBLE_LENGTH);      // send also on other channels
                  TransmitFrame(end, FREQUENCY_CHANNEL_3, SHORT_PREAMBLE_LENGTH);      // send also on other channels
                  UpdateDeviceStatus(item.frame);                                      // Update device status
                }
                else
                {
                  IO_LOGE("ProcessReceivedFrameTask - Failed to authenticate status update received from device");
                  IoFrame end;
                  create_error_response(end, mOwnNodeId, item.frame.src_node, CMD_PARAM_ERROR_BAD_AUTH);
                  TransmitFrame(end, item.frequency, SHORT_PREAMBLE_LENGTH); // send authentication error response
                }
              }
              else
              {
                // Update directly
                UpdateDeviceStatus(item.frame);
              }
            }
            break;
          default: // is it a known remote?
          {
            std::lock_guard<std::mutex> guard(sRemoteMapMutex); // Take mutex! It will be released when quitting the scope (before break statement)
            std::map<std::string, std::list<std::string>>::iterator it = sRemoteMap.find(srcDevice);
            if (it != sRemoteMap.end())
            {
              // Decode target position from 1W execute frame (data[2] = 2*position, 0-200)
              float remoteTarget = -1.0f;
              if (item.frame.command_id == CMD_EXECUTE_REQUEST && item.frame.data_len >= 3
                  && item.frame.data[2] <= 200)
              {
                remoteTarget = item.frame.data[2] / 2.0f; // 0–200 → 0.0–100.0
              }
              for (std::string deviceID : it->second)
              {
                std::map<std::string, IoDevice>::iterator device = sDeviceMap.find(deviceID);
                if (device != sDeviceMap.end())
                {
                  device->second.next_status_update_timestamp = esp_timer_get_time() + STATUS_UPDATE_AFTER_REMOTE_US;
                  // Start interpolation if we know the target (not STOP/FAVORITE/UNKNOWN)
                  if (remoteTarget >= 0.0f)
                  {
                    device->second.move_start_us   = esp_timer_get_time();
                    device->second.move_start_pos  = device->second.position;
                    device->second.move_target_pos = remoteTarget;
                    if (sMovementStartedCallback)
                    {
                      float dist = std::abs(remoteTarget - device->second.position) / 100.0f;
                      sMovementStartedCallback(deviceID, device->second.transit_time_ms, dist);
                    }
                  }
                  else
                  {
                    device->second.move_start_us = 0; // STOP or special command, no interpolation
                  }
                }
              }
              if (sUnknownSenderCallback != nullptr)
                sUnknownSenderCallback(srcDevice); // lets capture window detect already-linked remotes
            }
            else if (sUnknownSenderCallback != nullptr)
            {
              sUnknownSenderCallback(srcDevice);
            }
          }
          break;
          }
        }
        xSemaphoreGive(sMutex);
        vTaskDelay(pdMS_TO_TICKS(5)); // yield so P4/httpd can acquire mutex between frames
      }
      else
      {
        IO_LOGE("ProcessReceivedFrameTask - No mutex available!");
      }
    }
  }

  void IoHomeControl::UpdateDevicesStatusTask()
  {
    for (;;) // infinite loop
    {
      if (!isPassive()) // not passive, check if we should update some device status!
      {
        // Snapshot device IDs under mutex to avoid iterating sDeviceMap concurrently with
        // insertions (RestoreDevice) or field writes (ProcessReceivedFrameTask).
        std::vector<std::string> deviceIds;
        if (xSemaphoreTake(sMutex, MUTEX_MAX_WAIT_TICKS))
        {
          for (const auto &kv : sDeviceMap)
            deviceIds.push_back(kv.first);
          xSemaphoreGive(sMutex);
        }

        for (const auto &devId : deviceIds)
        {
          // Read device state under mutex to avoid data race with ProcessReceivedFrameTask writes.
          bool ownNode = false, isDeleted = false, needsName = false, needsType = false, shouldUpdate = false;
          if (xSemaphoreTake(sMutex, MUTEX_MAX_WAIT_TICKS))
          {
            auto it = sDeviceMap.find(devId);
            if (it != sDeviceMap.end())
            {
              ownNode      = (memcmp(it->second.info.node_id, mOwnNodeId, NODE_ID_SIZE) == 0);
              isDeleted    = it->second.is_deleted;
              needsName    = !ownNode && !isDeleted && (strlen(it->second.info.name) <= 1);
              needsType    = !ownNode && !isDeleted && (it->second.info.device_type == DeviceType::UNKNOWN);
              shouldUpdate = !ownNode && !isDeleted &&
                             ((esp_timer_get_time() > it->second.last_status_timestamp + STATUS_UPDATE_MAX_TIME_US) ||
                              (it->second.next_status_update_timestamp < esp_timer_get_time()));
            }
            xSemaphoreGive(sMutex);
          }

          if (ownNode)
          {
            IO_LOGE("UpdateDevicesStatusTask: device {} has node_id equal to own node ID — skipping!", devId);
            continue;
          }
          if (!shouldUpdate && !needsName && !needsType) continue;

          if (needsName) DeviceGetName(devId);
          // DeviceGetGeneralInfo1 / DeviceGetGeneralInfo3 reserved for future use
          if (needsType) DeviceGetGeneralInfo2(devId);

          if (shouldUpdate)
          {
            if (xSemaphoreTake(sMutex, MUTEX_MAX_WAIT_TICKS))
            {
              auto dev = sDeviceMap.find(devId);
              if (dev != sDeviceMap.end() && !dev->second.is_deleted)
              {
                IoFrame request;
                IoFrame response;
                bool reqOk;
                if (deviceTypeSupportsTilt(dev->second.info.device_type))
                  reqOk = create_getstatus03_tilt_request(request, mOwnNodeId, dev->second.info.node_id);
                else
                  reqOk = create_getstatus03_request(request, mOwnNodeId, dev->second.info.node_id);
                UBaseType_t currentPriority = uxTaskPriorityGet(NULL);
                vTaskPrioritySet(NULL, IO_FRAME_PROCESSING_TASK);
                bool exchangeOk = reqOk && SendAndReceive(request, response, FREQUENCY_CHANNEL_2);
                vTaskPrioritySet(NULL, currentPriority);
                if (exchangeOk)
                {
                  UpdateDeviceStatus(response);
                }
                else
                {
                  IO_LOGE("UpdateDevicesStatusTask: failed to send request or get response!");
                  dev->second.next_status_update_timestamp = esp_timer_get_time() + STATUS_UPDATE_NEXT_TRY_US;
                }
              }
              xSemaphoreGive(sMutex);
            }
            else
            {
              IO_LOGE("UpdateDevicesStatusTask: failed to take mutex!");
            }
          }
        }
      }
      vTaskDelay(pdMS_TO_TICKS(UPDATE_STATUS_WAKEUP_INTERVAL_MS)); // Wait until next loop to check again
    }
  }

  void IoHomeControl::AddDevice(const std::string &tmpDeviceID)
  {
    std::string deviceID(tmpDeviceID.length(), '0'); // init avoiding C++ 3133 warning
    std::transform(tmpDeviceID.begin(), tmpDeviceID.end(), deviceID.begin(), [](unsigned char c)
                   { return std::toupper(c); }); // convert to uppercase
    if (!sDeviceMap.contains(deviceID))
    {
      IoDevice device;
      memset(&device, 0, sizeof(IoDevice));
      device.is_stopped = true;
      device.is_deleted = false;
      device.position = UNKNOWN_POSITION;
      device.target = UNKNOWN_POSITION;
      device.tilt = UNKNOWN_POSITION;
      HexStringToBuff(deviceID, device.info.node_id, NODE_ID_SIZE);
      sDeviceMap.insert({deviceID, device});
    }
    else
    {
      auto it = sDeviceMap.find(deviceID); // we always have something in this 'else'
      if (it->second.is_deleted)           // the device was previously deleted with no reboot, "undelete" it
      {
        it->second.is_deleted = false;
        // force update
        it->second.last_status_timestamp = 0;
        it->second.next_status_update_timestamp = 0;
      }
    }
  }

  void IoHomeControl::RestoreDevice(const std::string &deviceID, const iohome::IoDevice &device)
  {
    if (xSemaphoreTake(sMutex, MUTEX_MAX_WAIT_TICKS))
    {
      if (!sDeviceMap.contains(deviceID))
        sDeviceMap.insert({deviceID, device});
      else
        IO_LOGE("RestoreDevice: can't restore a device that already exists!");
      xSemaphoreGive(sMutex);
    }
  }

  void IoHomeControl::DeleteDevice(const std::string &tmpDeviceID)
  {
    std::string deviceID(tmpDeviceID.length(), '0'); // init avoiding C++ 3133 warning
    std::transform(tmpDeviceID.begin(), tmpDeviceID.end(), deviceID.begin(), [](unsigned char c)
                   { return std::toupper(c); }); // convert to uppercase
    if (xSemaphoreTake(sMutex, MUTEX_MAX_WAIT_TICKS))
    {
      auto it = sDeviceMap.find(deviceID);
      if (it != sDeviceMap.end())
        it->second.is_deleted = true;
      xSemaphoreGive(sMutex);
    }
  }

  PairResult IoHomeControl::DiscoverAndPairDevice()
  {
    if (!mInitialized || !mReceiving || mPassiveMode)
    {
      IO_LOGE("DiscoverAndPairDevice: invalid state! (not initialized or not listening or passive mode)");
      return PairResult::FAILED_NO_RESPONSE;
    }

    if (xSemaphoreTake(sMutex, MUTEX_MAX_WAIT_TICKS))
    {
      IoFrame request;
      IoFrame response;
      IoDevice device;
      RxFrameQueueItem rxItem;
      PairResult result = PairResult::FAILED_NO_RESPONSE;
      UBaseType_t currentPriority = uxTaskPriorityGet(NULL);
      vTaskPrioritySet(NULL, IO_FRAME_PROCESSING_TASK); // change task priority to higher!
      // Send discovery request
      if (create_discovery_request(request, mOwnNodeId)                                                                                          // request created
          && TransmitFrame(request, FREQUENCY_CHANNEL_2, LONG_PREAMBLE_LENGTH)                                                                   // send OK, received something
          && xQueueReceive(sRxIoQueue, &rxItem, RECEIVED_IO_DISCOVERY_RESPONSE_WAIT_TICKS) && (rxItem.frame.command_id == CMD_DISCOVER_RESPONSE) // expected answer
          && process_discovery_response(rxItem.frame, device))                                                                                   // discovery response parsing OK
      {
        // Confirm discovery with device (CMD 2C → 2D) before key exchange
        if (create_discovery_confirmation_request(request, mOwnNodeId, device.info.node_id) // request created
            && SendAndReceive(request, response, FREQUENCY_CHANNEL_2)                       // send OK, received something
            && (response.command_id == CMD_DISCOVER_CONFIRMATION_ACK))                      // expected answer (CMD 2D)
        {
          // We have confirmed discovery, let's start pairing process
          if (create_init_transfer(request, mOwnNodeId, device.info.node_id)        // request created
              && TransmitFrame(request, FREQUENCY_CHANNEL_2, LONG_PREAMBLE_LENGTH)) // send OK
          {
            // We have sent key init request, let's get challenge from device
            if (xQueueReceive(sRxIoQueue, &rxItem, RECEIVED_IO_TREATMENT_WAIT_TICKS))
            {
              // We have a response, check it
              if (memcmp(rxItem.frame.src_node, device.info.node_id, NODE_ID_SIZE) != 0 // same device?
                  || memcmp(rxItem.frame.dest_node, mOwnNodeId, NODE_ID_SIZE) != 0      // for us?
                  || rxItem.frame.command_id != CMD_CHALLENGE_REQUEST                   // expected answer ID?
                  || rxItem.frame.data_len != HMAC_SIZE)                                // expected answer data?
              {
                IO_LOGE("DiscoverAndPairDevice: failed to confirm discovery!");
              }
              else
              {
              // We have received a challenge, let's send our key!
              IoFrame keyTransfer;
              if (create_key_transfer(keyTransfer, request, device.info.node_id, mOwnNodeId, mSystemKey, rxItem.frame.data) // request created
                  && SendAndReceive(keyTransfer, response, FREQUENCY_CHANNEL_2)                                             // send OK, received something
                  && (response.command_id == CMD_KEY_TRANSFER_CONFIRMATION))                                                // expected answer
              {
                // Create this device
                std::string deviceID = buffToHexString(NODE_ID_SIZE, device.info.node_id);
                device.is_deleted = false;
                sDeviceMap.insert({deviceID, device});
                IO_LOGI("DiscoverAndPairDevice: device {} added!", deviceID);
                result = PairResult::PAIRED_FULL;
                // Now try to find "sub devices" (like a light or switch on DexxoSmartio800)
                if (create_discoveryspe_request(request, mOwnNodeId, mSystemKey)          // request created
                    && TransmitFrame(request, FREQUENCY_CHANNEL_2, LONG_PREAMBLE_LENGTH)) // send OK
                {
                  while ((xQueueReceive(sRxIoQueue, &rxItem, RECEIVED_IO_TREATMENT_WAIT_TICKS)))
                  {
                    IoDevice subDevice;
                    if (memcmp(rxItem.frame.dest_node, mOwnNodeId, NODE_ID_SIZE) == 0 // for us
                        && rxItem.frame.command_id == CMD_DISCOVER_SPE_RESPONSE       // expected response
                        && process_discoveryspe_response(rxItem.frame, subDevice))    // parsing success
                    {
                      // Let's add the device (it already has the key)
                      std::string subDeviceID = buffToHexString(NODE_ID_SIZE, subDevice.info.node_id);
                      sDeviceMap.insert({subDeviceID, subDevice});
                      IO_LOGI("DiscoverAndPairDevice: subdevice {} added!", subDeviceID);
                      // and confirm discovery to this device
                      if (create_discovery_confirmation_request(request, mOwnNodeId, subDevice.info.node_id) // request created
                          && SendAndReceive(request, response, FREQUENCY_CHANNEL_2)                          // send OK, received something
                          && (response.command_id == CMD_DISCOVER_CONFIRMATION_ACK))                         // expected answer
                      {
                        // IO_LOGI("DiscoverAndPairDevice: confirmed discovery to subdevice {}!", subDeviceID);
                      }
                      else
                      {
                        // IO_LOGE("DiscoverAndPairDevice: failed to confirm discovery to subdevice!");
                      }
                    }
                    else
                      break;
                  }
                }
                // Finally try to configure device to send its status (we don't stop if refused as not all devices support it!)
                if (create_set_config1_command(request, mOwnNodeId, device.info.node_id, device.info.is_low_power) && SendAndReceive(request, response, FREQUENCY_CHANNEL_2))
                {
                  if (response.command_id == CMD_ERROR_RESPONSE)
                  {
                    IO_LOGI("ConfigureDeviceToSendStatus: it seems device doesn't support this command!");
                  }
                  else if (response.command_id != CMD_SET_CONFIG1_RESPONSE)
                  {
                    IO_LOGE("ConfigureDeviceToSendStatus: unexpected response!");
                  }
                }
                else
                {
                  IO_LOGE("ConfigureDeviceToSendStatus: failed to send request or didn't receive a response!");
                }
              }
              else
              {
                // Key exchange failed — device may already share our key (re-pairing scenario).
                // Add provisionally and verify with CMD 03. Already holding sMutex — call low-level directly.
                std::string deviceID = buffToHexString(NODE_ID_SIZE, device.info.node_id);
                device.is_deleted = false;
                sDeviceMap.insert({deviceID, device});
                IoFrame statusReq, statusResp;
                if (create_getstatus03_request(statusReq, mOwnNodeId, device.info.node_id)
                    && SendAndReceive(statusReq, statusResp, FREQUENCY_CHANNEL_2)
                    && statusResp.command_id == CMD_PRIVATE_RESPONSE)
                {
                  IO_LOGI("DiscoverAndPairDevice: shortcut verified — device {} responds to CMD 03, shared key confirmed", deviceID);
                  result = PairResult::PAIRED_SHORTCUT_VERIFIED;
                }
                else
                {
                  IO_LOGE("DiscoverAndPairDevice: CMD 03 no response — device {} has a different key, factory reset required", deviceID);
                  sDeviceMap.erase(deviceID);
                  result = PairResult::FAILED_KEY_MISMATCH;
                }
              }
            }
          }
              else
              {
                IO_LOGE("DiscoverAndPairDevice: no answer received to key init request!");
              }
            }
            else
            {
              IO_LOGE("DiscoverAndPairDevice: failed to send key init request!");
            }
          }
          else
          {
            IO_LOGE("DiscoverAndPairDevice: failed to confirm discovery (CMD 2C/2D)!");
          }
        }
        else
        {
          IO_LOGE("DiscoverAndPairDevice: failed to send discovery request / no or bad answer received!");
      }
      vTaskPrioritySet(NULL, currentPriority); // restore task priority
      xSemaphoreGive(sMutex);
      return result;
    }
    else
    {
      IO_LOGE("DiscoverAndPairDevice: failed to take mutex!");
      return PairResult::FAILED_NO_RESPONSE;
    }
  }

  std::string IoHomeControl::LearnKeyFromController(const volatile bool *active)
  {
    if (!mInitialized || !mReceiving || mPassiveMode)
    {
      IO_LOGE("LearnKeyFromController: invalid state!");
      return "";
    }

    IO_LOGI("LearnKeyFromController: session started (120 s window)");

    const int64_t SESSION_US = 120LL * 1000000LL;
    const int64_t deadline   = esp_timer_get_time() + SESSION_US;

    RxFrameQueueItem rxItem;

    // Wait for CMD 29 within timeout_ms, skipping CMD 28/2A/2E broadcasts.
    auto wait_for_cmd29 = [&](RxFrameQueueItem &item, int timeout_ms) -> bool {
      int64_t end_us = esp_timer_get_time() + (int64_t)timeout_ms * 1000LL;
      for (;;) {
        int64_t rem_us = end_us - esp_timer_get_time();
        if (rem_us <= 0) return false;
        TickType_t ticks = (TickType_t)(rem_us / 1000 / portTICK_PERIOD_MS);
        if (ticks == 0) ticks = 1;
        if (!xQueueReceive(sRxIoQueue, &item, ticks)) return false;
        if (item.frame.command_id == CMD_DISCOVER_RESPONSE) return true;
        uint8_t cmd = item.frame.command_id;
        if (cmd != CMD_DISCOVER_REQUEST && cmd != CMD_DISCOVER_SPE_REQUEST && cmd != CMD_DISCOVER2E_REQUEST)
          return false; // unexpected — abort this attempt
      }
    };

    // Outer loop: send CMD 28, wait for TaHoma's CMD 29, then request its key via CMD 38.
    // Retry on any step failure until the 120 s session window expires.
    while ((active == nullptr || *active) && esp_timer_get_time() < deadline)
    {
      if (!xSemaphoreTake(sMutex, MUTEX_MAX_WAIT_TICKS))
      {
        IO_LOGE("LearnKeyFromController: failed to take mutex!");
        return "";
      }

      UBaseType_t savedPriority = uxTaskPriorityGet(NULL);
      vTaskPrioritySet(NULL, IO_FRAME_PROCESSING_TASK);

      // Step 1: broadcast CMD 28 to trigger TaHoma (in key-send mode) into responding
      IoFrame disc_req;
      if (!create_discovery_request(disc_req, mOwnNodeId)
          || !TransmitFrame(disc_req, FREQUENCY_CHANNEL_2, LONG_PREAMBLE_LENGTH))
      {
        IO_LOGE("LearnKeyFromController: failed to send CMD 28");
        vTaskPrioritySet(NULL, savedPriority);
        xSemaphoreGive(sMutex);
        vTaskDelay(pdMS_TO_TICKS(500));
        continue;
      }
      IO_LOGI("LearnKeyFromController: CMD 28 sent — waiting for CMD 29");

      // Step 2: wait for CMD 29 from TaHoma
      if (!wait_for_cmd29(rxItem, 2000))
      {
        IO_LOGI("LearnKeyFromController: no CMD 29 — retrying");
        vTaskPrioritySet(NULL, savedPriority);
        xSemaphoreGive(sMutex);
        continue;
      }
      uint8_t tahoma_node[NODE_ID_SIZE];
      memcpy(tahoma_node, rxItem.frame.src_node, NODE_ID_SIZE);
      uint32_t tahoma_freq = rxItem.frequency;
      IO_LOGI("LearnKeyFromController: CMD 29 from {} data[{}]={}", buffToHexString(NODE_ID_SIZE, tahoma_node), rxItem.frame.data_len, buffToHexString(rxItem.frame.data_len, rxItem.frame.data));

      // Steps 3+4: send CMD 38 up to 3 times per CMD 29 cycle.
      // Same challenge for all retries — TaHoma may respond to any of the 3 attempts and the
      // challenge must match whichever one it received.
      // CMD 32 arrives within ~60ms when TaHoma processes CMD 38, so 100ms is sufficient for
      // attempts 1 and 2. The last attempt uses 1s to give TaHoma time to recover or respond.
      // Note: 2C/2D confirmation handshake was tested and found to break TaHoma's key exchange —
      // TaHoma stops responding to CMD 38 with CMD 32 after receiving CMD 2C.
      uint8_t our_challenge[HMAC_SIZE];
      esp_fill_random(our_challenge, HMAC_SIZE);

      bool got32 = false;
      for (int attempt = 0; attempt < 3 && !got32; ++attempt)
      {
        IoFrame launch_frame;
        if (!create_launch_key_transfer(launch_frame, mOwnNodeId, tahoma_node, our_challenge)
            || !TransmitFrame(launch_frame, tahoma_freq, SHORT_PREAMBLE_LENGTH))
        {
          IO_LOGE("LearnKeyFromController: failed to send CMD 38[{}]", attempt);
          break;
        }
        TickType_t wait = (attempt < 2) ? pdMS_TO_TICKS(100) : pdMS_TO_TICKS(1000);
        IO_LOGI("LearnKeyFromController: CMD 38[{}] challenge={} — waiting {}ms for CMD 32",
                attempt, buffToHexString(HMAC_SIZE, our_challenge), (attempt < 2) ? 100 : 1000);

        if (!xQueueReceive(sRxIoQueue, &rxItem, wait))
        {
          IO_LOGI("LearnKeyFromController: CMD 38[{}] no frame — retrying", attempt);
          continue;
        }
        if (rxItem.frame.command_id == CMD_KEY_TRANSFER && rxItem.frame.data_len == AES_KEY_SIZE)
        {
          got32 = true;
        }
        else
        {
          IO_LOGI("LearnKeyFromController: CMD 38[{}] rx 0x{:02X} from {} — retrying",
                  attempt, rxItem.frame.command_id,
                  buffToHexString(NODE_ID_SIZE, rxItem.frame.src_node));
        }
      }
      if (!got32) {
        vTaskPrioritySet(NULL, savedPriority);
        xSemaphoreGive(sMutex);
        continue;
      }

      // Step 5: decrypt TaHoma's system key.
      // IV frame_data = [CMD_38, challenge[0..5]] — command byte + challenge payload, matching TaHoma's construction.
      uint8_t cmd38_fd[1 + HMAC_SIZE];
      cmd38_fd[0] = CMD_LAUNCH_KEY_TRANSFER;
      memcpy(cmd38_fd + 1, our_challenge, HMAC_SIZE);
      uint8_t decrypted_key[AES_KEY_SIZE];
      iohome::crypto::crypt_2w_key(cmd38_fd, sizeof(cmd38_fd), our_challenge, rxItem.frame.data, decrypted_key);
      ESP_LOGI(TAG, "LearnKeyFromController: CMD 32 raw=%s",
               buffToHexString(AES_KEY_SIZE, rxItem.frame.data).c_str());
      ESP_LOGI(TAG, "LearnKeyFromController: system key=%s",
               buffToHexString(AES_KEY_SIZE, decrypted_key).c_str());

      std::string result = buffToHexString(AES_KEY_SIZE, decrypted_key);
      IO_LOGI("LearnKeyFromController: key received: {}", result);

      vTaskPrioritySet(NULL, savedPriority);
      xSemaphoreGive(sMutex);
      IO_LOGI("LearnKeyFromController: session complete");
      return result;
    }

    IO_LOGI("LearnKeyFromController: session ended (timeout or cancelled)");
    return "";
  }

  void IoHomeControl::WaitAndRespondToCmd28(const volatile bool *active)
  {
    if (!mInitialized || !mReceiving || mPassiveMode)
    {
      IO_LOGE("WaitAndRespondToCmd28: invalid state!");
      return;
    }

    IO_LOGI("WaitAndRespondToCmd28: session started — listening for TaHoma CMD 28 (30 s)");

    const int64_t deadline = esp_timer_get_time() + 30LL * 1000000LL;

    while ((active == nullptr || *active) && esp_timer_get_time() < deadline)
    {
      if (!xSemaphoreTake(sMutex, MUTEX_MAX_WAIT_TICKS))
        continue;

      UBaseType_t savedPriority = uxTaskPriorityGet(NULL);
      vTaskPrioritySet(NULL, IO_FRAME_PROCESSING_TASK);

      // Poll for CMD 28 for up to 2 s while holding the mutex (same pattern as LearnKeyFromController).
      // The main processing loop also reads sRxIoQueue under this mutex, so we must hold it first.
      RxFrameQueueItem item;
      bool got28 = false;
      int64_t poll_end = esp_timer_get_time() + 2000000LL;
      while (esp_timer_get_time() < poll_end)
      {
        int64_t rem_us = poll_end - esp_timer_get_time();
        TickType_t ticks = (TickType_t)(rem_us / 1000 / portTICK_PERIOD_MS);
        if (ticks == 0) ticks = 1;
        if (!xQueueReceive(sRxIoQueue, &item, ticks)) break;
        if (item.frame.command_id == CMD_DISCOVER_REQUEST) { got28 = true; break; }
        ESP_LOGD(TAG, "WaitAndRespondToCmd28: skip CMD 0x%02X", item.frame.command_id);
      }
      if (!got28)
      {
        vTaskPrioritySet(NULL, savedPriority);
        xSemaphoreGive(sMutex);
        continue;
      }

      uint8_t tahoma_node[NODE_ID_SIZE];
      memcpy(tahoma_node, item.frame.src_node, NODE_ID_SIZE);
      uint32_t tahoma_freq = item.frequency;
      ESP_LOGI(TAG, "WaitAndRespondToCmd28: CMD 28 from %s freq=%lu",
               buffToHexString(NODE_ID_SIZE, tahoma_node).c_str(), (unsigned long)tahoma_freq);

      // Step 2: send CMD 29 — announce ourselves to TaHoma.
      IoFrame resp29;
      if (!create_discovery_response(resp29, mOwnNodeId, tahoma_node, 1023)
          || !TransmitFrame(resp29, tahoma_freq, SHORT_PREAMBLE_LENGTH))
      {
        IO_LOGE("WaitAndRespondToCmd28: failed to send CMD 29");
        vTaskPrioritySet(NULL, savedPriority);
        xSemaphoreGive(sMutex);
        continue;
      }
      ESP_LOGI(TAG, "WaitAndRespondToCmd28: CMD 29 sent — waiting for TaHoma response");

      // Step 3: log whatever TaHoma sends next so we can identify the expected command.
      for (int i = 0; i < 5; i++)
      {
        RxFrameQueueItem rx = {};
        if (!xQueueReceive(sRxIoQueue, &rx, pdMS_TO_TICKS(2000)))
        {
          ESP_LOGI(TAG, "WaitAndRespondToCmd28: [%d] timeout — no frame", i);
          break;
        }
        ESP_LOGI(TAG, "WaitAndRespondToCmd28: [%d] CMD 0x%02X from %s data[%u]=%s",
                 i, rx.frame.command_id,
                 buffToHexString(NODE_ID_SIZE, rx.frame.src_node).c_str(),
                 rx.frame.data_len,
                 buffToHexString(rx.frame.data_len, rx.frame.data).c_str());
      }

      vTaskPrioritySet(NULL, savedPriority);
      xSemaphoreGive(sMutex);
    }

    IO_LOGI("WaitAndRespondToCmd28: session ended");
  }

  std::string IoHomeControl::PairAsDevice(const volatile bool *active)
  {
    if (!mInitialized || !mReceiving || mPassiveMode)
    {
      IO_LOGE("PairAsDevice: invalid state!");
      return "";
    }

    // One stable fake node ID for the entire 120-second session.
    // TaHoma must see the same ID on every CMD 28 / CMD 29 exchange to complete the handshake.
    uint8_t fakeNodeId[NODE_ID_SIZE];
    esp_fill_random(fakeNodeId, NODE_ID_SIZE);
    IO_LOGI("PairAsDevice: session node ID {}", buffToHexString(NODE_ID_SIZE, fakeNodeId));

    const int64_t SESSION_US = 120LL * 1000000LL;
    const int64_t deadline   = esp_timer_get_time() + SESSION_US;

    RxFrameQueueItem rxItem;

    // Receive the next non-discovery-broadcast frame within timeout_ms.
    // TaHoma keeps sending CMD 28/2A/2E broadcasts throughout the handshake; skip them.
    auto receive_non_broadcast = [&](RxFrameQueueItem &item, int timeout_ms) -> bool {
      int64_t end_us = esp_timer_get_time() + (int64_t)timeout_ms * 1000LL;
      for (;;) {
        int64_t rem_us = end_us - esp_timer_get_time();
        if (rem_us <= 0) return false;
        TickType_t ticks = (TickType_t)(rem_us / 1000 / portTICK_PERIOD_MS);
        if (ticks == 0) ticks = 1;
        if (!xQueueReceive(sRxIoQueue, &item, ticks)) return false;
        uint8_t cmd = item.frame.command_id;
        if (cmd != CMD_DISCOVER_REQUEST && cmd != CMD_DISCOVER_SPE_REQUEST && cmd != CMD_DISCOVER2E_REQUEST)
          return true;
      }
    };

    // Outer loop: wait for CMD 28, retry the handshake from scratch on partial failures.
    // The mutex is released between CMD 28 polls so the frame processor can keep running.
    while ((active == nullptr || *active) && esp_timer_get_time() < deadline)
    {
      if (!xSemaphoreTake(sMutex, MUTEX_MAX_WAIT_TICKS))
      {
        IO_LOGE("PairAsDevice: failed to take mutex!");
        return "";
      }

      // Wait up to 2 s for the next CMD 28 broadcast
      bool got = xQueueReceive(sRxIoQueue, &rxItem, RECEIVED_IO_DISCOVERY_RESPONSE_WAIT_TICKS);
      if (!got || rxItem.frame.command_id != CMD_DISCOVER_REQUEST)
      {
        xSemaphoreGive(sMutex);
        continue;
      }

      bool to_broadcast = (memcmp(rxItem.frame.dest_node, BROADCAST_DISCOVER_ADDRESS, NODE_ID_SIZE) == 0);
      bool to_us        = (memcmp(rxItem.frame.dest_node, fakeNodeId, NODE_ID_SIZE) == 0);
      if (!to_broadcast && !to_us)
      {
        ESP_LOGW(TAG, "PairAsDevice: CMD 28 not for us — ignoring (dst %s)",
                 buffToHexString(NODE_ID_SIZE, rxItem.frame.dest_node).c_str());
        xSemaphoreGive(sMutex);
        continue;
      }
      ESP_LOGI(TAG, "PairAsDevice: CMD 28 %s from %s",
               to_broadcast ? "broadcast" : "direct",
               buffToHexString(NODE_ID_SIZE, rxItem.frame.src_node).c_str());

      // Step 2: respond with CMD 29 — hold mutex through the rest of the handshake
      IoFrame disc_resp;
      if (!create_discovery_response(disc_resp, fakeNodeId, rxItem.frame.src_node, static_cast<uint8_t>(DeviceType::ROLLER_SHUTTER))
          || !TransmitFrame(disc_resp, rxItem.frequency, SHORT_PREAMBLE_LENGTH))
      {
        ESP_LOGE(TAG, "PairAsDevice: failed to send CMD 29");
        xSemaphoreGive(sMutex);
        continue;
      }
      ESP_LOGI(TAG, "PairAsDevice: CMD 29 sent (as %s ROLLER_SHUTTER)",
               buffToHexString(NODE_ID_SIZE, fakeNodeId).c_str());

      // Step 3: wait for CMD 2C or CMD 31, skipping CMD 28/2A/2E broadcasts.
      // TaHoma sends CMD 2C within ~1.5 s of receiving CMD 29.
      if (!receive_non_broadcast(rxItem, 2000))
      {
        ESP_LOGW(TAG, "PairAsDevice: no CMD 2C/31 — retrying from CMD 28");
        xSemaphoreGive(sMutex);
        continue;
      }

      if (rxItem.frame.command_id == CMD_DISCOVER_CONFIRMATION)
      {
        ESP_LOGI(TAG, "PairAsDevice: CMD 2C received — sending CMD 2D");
        // Step 4: reply with CMD 2D
        IoFrame conf_ack;
        if (!create_discovery_confirmation_ack(conf_ack, fakeNodeId, rxItem.frame.src_node)
            || !TransmitFrame(conf_ack, rxItem.frequency, SHORT_PREAMBLE_LENGTH))
        {
          ESP_LOGE(TAG, "PairAsDevice: failed to send CMD 2D");
          xSemaphoreGive(sMutex);
          continue;
        }
        // After CMD 2D, TaHoma continues broadcasting CMD 28 for ~4 s before sending CMD 31.
        // Skip those broadcasts and wait up to 10 s for CMD 31.
        if (!receive_non_broadcast(rxItem, 10000) || rxItem.frame.command_id != CMD_KEY_INIT_TRANSFER)
        {
          ESP_LOGE(TAG, "PairAsDevice: no CMD 31 after CMD 2D");
          xSemaphoreGive(sMutex);
          continue;
        }
        ESP_LOGI(TAG, "PairAsDevice: CMD 31 received (after 2C/2D)");
      }
      else if (rxItem.frame.command_id == CMD_KEY_INIT_TRANSFER)
      {
        ESP_LOGI(TAG, "PairAsDevice: CMD 31 received (no 2C/2D)");
      }
      else
      {
        ESP_LOGW(TAG, "PairAsDevice: unexpected cmd 0x%02X — retrying from CMD 28", rxItem.frame.command_id);
        xSemaphoreGive(sMutex);
        continue;
      }

      // Steps 5-9: complete the key exchange
      bool handshake_ok = false;
      std::string result;

      IoFrame key_init_frame = rxItem.frame;
      uint8_t controller_node[NODE_ID_SIZE];
      memcpy(controller_node, rxItem.frame.src_node, NODE_ID_SIZE);

      RxFrameQueueItem challenge_item;
      if (!xQueueReceive(sRxIoQueue, &challenge_item, RECEIVED_IO_TREATMENT_WAIT_TICKS)
          || challenge_item.frame.command_id != CMD_CHALLENGE_REQUEST
          || challenge_item.frame.data_len != HMAC_SIZE)
      {
        ESP_LOGE(TAG, "PairAsDevice: no CMD 3C received");
      }
      else
      {
        ESP_LOGI(TAG, "PairAsDevice: CMD 3C received — waiting for CMD 32");

        RxFrameQueueItem key_item;
        if (!xQueueReceive(sRxIoQueue, &key_item, RECEIVED_IO_TREATMENT_WAIT_TICKS)
            || key_item.frame.command_id != CMD_KEY_TRANSFER
            || key_item.frame.data_len != AES_KEY_SIZE)
        {
          ESP_LOGE(TAG, "PairAsDevice: no CMD 32 received");
        }
        else
        {
          ESP_LOGI(TAG, "PairAsDevice: CMD 32 received — decrypting key");
          uint8_t challenge_fd[1 + HMAC_SIZE];
          challenge_fd[0] = challenge_item.frame.command_id;
          memcpy(challenge_fd + 1, challenge_item.frame.data, HMAC_SIZE);
          uint8_t decrypted_key[AES_KEY_SIZE];
          if (!iohome::crypto::crypt_2w_key(challenge_fd, sizeof(challenge_fd),
                                             challenge_item.frame.data,
                                             key_item.frame.data, decrypted_key))
          {
            ESP_LOGE(TAG, "PairAsDevice: key decryption failed");
          }
          else
          {
            result = buffToHexString(AES_KEY_SIZE, decrypted_key);
            ESP_LOGI(TAG, "PairAsDevice: key received: %s", result.c_str());

            IoFrame confirm;
            if (create_key_transfer_confirmation(confirm, fakeNodeId, controller_node))
              TransmitFrame(confirm, key_item.frequency, SHORT_PREAMBLE_LENGTH);
            ESP_LOGI(TAG, "PairAsDevice: CMD 33 sent — complete");
            handshake_ok = true;
          }
        }
      }

      xSemaphoreGive(sMutex);
      if (handshake_ok) return result;
      // Any step after CMD 31 failed — retry from CMD 28
    }

    IO_LOGI("PairAsDevice: session ended (timeout or cancelled)");
    return "";
  }

  bool IoHomeControl::SetDevicePosition(const std::string &deviceID, uint8_t position, bool quiet)
  {
    if (!mInitialized || !mReceiving || mPassiveMode)
    {
      IO_LOGE("SetDevicePosition: invalid state! (not initialized or not listening or passive mode)");
      return false;
    }
    std::map<std::string, IoDevice>::iterator it = sDeviceMap.find(deviceID);
    if (it == sDeviceMap.end())
    {
      IO_LOGE("SetDevicePosition: no device found in list!");
      return false;
    }
    else if (it->second.is_deleted)
    {
      IO_LOGE("SetDevicePosition: device is marked as deleted, add it before using it!");
      return false;
    }
    if (isDeviceOpenCloseOnly(deviceID) && position != 0 && position != 100)
    {
      IO_LOGE("SetDevicePosition: This device ({}) doesn't support this command!", deviceID);
      return false;
    }
    IO_LOGI("Setting position to {}%", position);
    if (xSemaphoreTake(sMutex, MUTEX_MAX_WAIT_TICKS))
    {
      IoFrame request;
      IoFrame response;
      bool ret = false;
      UBaseType_t currentPriority = uxTaskPriorityGet(NULL);
      vTaskPrioritySet(NULL, IO_FRAME_PROCESSING_TASK); // change task priority to higher!
      // Record movement tracking before sending so elapsed time is accurate
      it->second.move_start_us   = esp_timer_get_time();
      it->second.move_start_pos  = it->second.position;
      it->second.move_target_pos = (float)position;
      if (create_execute_request(request, mOwnNodeId, it->second.info.node_id, it->second.info.is_low_power, position, quiet) && SendAndReceive(request, response, FREQUENCY_CHANNEL_2))
      {
        UpdateDeviceStatus(response);
        ret = true;
      }
      else
      {
        it->second.move_start_us = 0; // command failed, clear tracking
        IO_LOGE("SetDevicePosition: failed to send request!");
      }
      vTaskPrioritySet(NULL, currentPriority); // restore task priority
      xSemaphoreGive(sMutex);
      return ret;
    }
    else
    {
      IO_LOGE("SetDevicePosition: failed to take mutex!");
      return false;
    }
  }

  bool IoHomeControl::SetDeviceTilt(const std::string &deviceID, uint8_t tilt)
  {
    if (!mInitialized || !mReceiving || mPassiveMode)
    {
      IO_LOGE("SetDeviceTilt: invalid state! (not initialized or not listening or passive mode)");
      return false;
    }
    std::map<std::string, IoDevice>::iterator it = sDeviceMap.find(deviceID);
    if (it == sDeviceMap.end())
    {
      IO_LOGE("SetDeviceTilt: no device found in list!");
      return false;
    }
    else if (it->second.is_deleted)
    {
      IO_LOGE("SetDeviceTilt: device is marked as deleted, add it before using it!");
      return false;
    }
    if (!deviceTypeSupportsTilt(it->second.info.device_type))
    {
      IO_LOGE("SetDeviceTilt: device ({}) doesn't support tilt!", deviceID);
      return false;
    }
    if (tilt > 100)
    {
      IO_LOGE("SetDeviceTilt: invalid tilt value! (0-100)");
      return false;
    }
    IO_LOGI("Setting tilt to {}%", tilt);
    if (xSemaphoreTake(sMutex, MUTEX_MAX_WAIT_TICKS))
    {
      IoFrame request;
      IoFrame response;
      bool ret = false;
      UBaseType_t currentPriority = uxTaskPriorityGet(NULL);
      vTaskPrioritySet(NULL, IO_FRAME_PROCESSING_TASK); // change task priority to higher!
      if (create_execute_tilt_request(request, mOwnNodeId, it->second.info.node_id, it->second.info.is_low_power, tilt) && SendAndReceive(request, response, FREQUENCY_CHANNEL_2))
      {
        UpdateDeviceStatus(response);
        ret = true;
      }
      else
      {
        IO_LOGE("SetDeviceTilt: failed to send request!");
      }
      vTaskPrioritySet(NULL, currentPriority); // restore task priority
      xSemaphoreGive(sMutex);
      return ret;
    }
    else
    {
      IO_LOGE("SetDeviceTilt: failed to take mutex!");
      return false;
    }
  }

  bool IoHomeControl::isDeviceTiltSupported(const std::string &deviceID)
  {
    std::map<std::string, IoDevice>::iterator it = sDeviceMap.find(deviceID);
    if (it == sDeviceMap.end())
      return false;
    return deviceTypeSupportsTilt(it->second.info.device_type);
  }

  bool IoHomeControl::OpenDevice(const std::string &deviceID, bool quiet)
  {
    if (!mInitialized || !mReceiving || mPassiveMode)
    {
      IO_LOGE("OpenDevice: invalid state! (not initialized or not listening or passive mode)");
      return false;
    }
    std::map<std::string, IoDevice>::iterator it = sDeviceMap.find(deviceID);
    if (it == sDeviceMap.end())
    {
      IO_LOGE("OpenDevice: no device found in list!");
      return false;
    }
    else if (it->second.is_deleted)
    {
      IO_LOGE("OpenDevice: device is marked as deleted, add it before using it!");
      return false;
    }
    return SetDevicePosition(deviceID, it->second.info.is_openclose_inverted ? 100 : 0, quiet);
  }

  bool IoHomeControl::CloseDevice(const std::string &deviceID, bool quiet)
  {
    if (!mInitialized || !mReceiving || mPassiveMode)
    {
      IO_LOGE("CloseDevice: invalid state! (not initialized or not listening or passive mode)");
      return false;
    }
    std::map<std::string, IoDevice>::iterator it = sDeviceMap.find(deviceID);
    if (it == sDeviceMap.end())
    {
      IO_LOGE("CloseDevice: no device found in list!");
      return false;
    }
    else if (it->second.is_deleted)
    {
      IO_LOGE("CloseDevice: device is marked as deleted, add it before using it!");
      return false;
    }
    return SetDevicePosition(deviceID, it->second.info.is_openclose_inverted ? 0 : 100, quiet);
  }

  bool IoHomeControl::SetDeviceName(const std::string &deviceID, const std::string &name)
  {
    if (!mInitialized || !mReceiving || mPassiveMode)
    {
      IO_LOGE("SetDeviceName: invalid state! (not initialized or not listening or passive mode)");
      return false;
    }
    if (name.length() > 15)
    {
      IO_LOGE("SetDeviceName: name must be less than 16 characters! (received {})", name.length());
      return false;
    }
    std::map<std::string, IoDevice>::iterator it = sDeviceMap.find(deviceID);
    if (it == sDeviceMap.end())
    {
      IO_LOGE("SetDeviceName: no device found in list!");
      return false;
    }
    else if (it->second.is_deleted)
    {
      IO_LOGE("SetDeviceName: device is marked as deleted, add it before using it!");
      return false;
    }
    IO_LOGI("Setting device name to {}", name);
    if (xSemaphoreTake(sMutex, MUTEX_MAX_WAIT_TICKS))
    {
      IoFrame request;
      IoFrame response;
      bool ret = false;
      UBaseType_t currentPriority = uxTaskPriorityGet(NULL);
      vTaskPrioritySet(NULL, IO_FRAME_PROCESSING_TASK); // change task priority to higher!
      // Convert UTF-8 name to Latin-1 for the device (IO-Homecontrol uses Latin-1)
      std::string latin1Name = Helpers::EncodingHelpers::Utf8ToLatin1(name);
      if (create_setname_request(request, mOwnNodeId, it->second.info.node_id, latin1Name.c_str(), latin1Name.length() + 1) && SendAndReceive(request, response, FREQUENCY_CHANNEL_2))
      {
        if (response.command_id == CMD_SET_NAME_ANSWER)
        {
          // Update Device Status — store the UTF-8 version in memory
          memset(it->second.info.name, 0, sizeof(it->second.info.name));
          size_t copyLen = name.length() < CMD_PARAM_NAME_MAXSIZE - 1 ? name.length() : CMD_PARAM_NAME_MAXSIZE - 1;
          memcpy(it->second.info.name, name.c_str(), copyLen);
          if (!xQueueSendToBack(sIoDeviceStatusQueue, &it->second, 0))
          {
            IO_LOGE("UpdateDeviceStatus can't add device to queue!");
          }
          ret = true;
        }
        else
        {
          IO_LOGE("SetDeviceName: received unexpected response!");
          ret = false;
        }
      }
      else
      {
        IO_LOGE("SetDeviceName: failed to send request!");
      }
      vTaskPrioritySet(NULL, currentPriority); // restore task priority
      xSemaphoreGive(sMutex);
      return ret;
    }
    else
    {
      IO_LOGE("SetDeviceName: failed to take mutex!");
      return false;
    }
  }

  bool IoHomeControl::ForceDeviceStatusUpdate(const std::string &deviceID)
  {
    if (!mInitialized || !mReceiving || mPassiveMode)
    {
      IO_LOGE("ForceDeviceStatusUpdate: invalid state! (not initialized or not listening or passive mode)");
      return false;
    }
    std::map<std::string, IoDevice>::iterator it = sDeviceMap.find(deviceID);
    if (it == sDeviceMap.end())
    {
      IO_LOGE("ForceDeviceStatusUpdate: no device found in list!");
      return false;
    }
    else if (it->second.is_deleted)
    {
      IO_LOGE("ForceDeviceStatusUpdate: device is marked as deleted, add it before using it!");
      return false;
    }
    it->second.next_status_update_timestamp = 0;
    return true;
  }

  bool IoHomeControl::isDeviceOpenCloseOnly(const std::string &deviceID)
  {
    if (!mInitialized || !mReceiving || mPassiveMode)
      return false;
    std::map<std::string, IoDevice>::iterator it = sDeviceMap.find(deviceID);
    if (it == sDeviceMap.end())
    {
      IO_LOGE("SetDeviceName: no device found in list!");
      return false;
    }
    // See Velux KLF 200 API, Appendix 2
    switch (it->second.info.device_type)
    {
    case DeviceType::VENETIAN_BLIND:
    case DeviceType::ROLLER_SHUTTER:
    case DeviceType::AWNING:
    case DeviceType::WINDOW_OPENER:
    case DeviceType::GARAGE_OPENER:
    case DeviceType::GATE_OPENER:
    case DeviceType::ROLLING_DOOR_OPENER:
    case DeviceType::BLIND:
    case DeviceType::DUAL_SHUTTER:
    case DeviceType::HORIZONTAL_AWNING:
    case DeviceType::EXTERNAL_VENETIAN_BLIND:
    case DeviceType::LOUVRE_BLIND:
    case DeviceType::CURTAIN_TRACK:
    case DeviceType::SWINGING_SHUTTER:
      return false; // These devices are all supporting position and stop
    case DeviceType::LIGHT:
      return it->second.info.device_subtype == 58; // Light only supporting ON/OFF
      break;
    case DeviceType::UNKNOWN:
    case DeviceType::LOCK:
    case DeviceType::UNKNOWN_0B:
    case DeviceType::BEACON:
    case DeviceType::HEATING_TEMPERATURE_INTERFACE:
    case DeviceType::ON_OFF_SWITCH:
    case DeviceType::VENTILATION_POINT:
    case DeviceType::EXTERIOR_HEATING:
    case DeviceType::HEAT_PUMP:
    case DeviceType::INTRUSION_ALARM:
    default:
      return true;
    }
  }

  bool IoHomeControl::LinkRemoteToDevice(const std::string &remoteID, const std::string &deviceID)
  {
    auto it = sDeviceMap.find(deviceID);
    if (it == sDeviceMap.end() || it->second.is_deleted) // unknown device or device is deleted!
      return false;
    std::lock_guard<std::mutex> guard(sRemoteMapMutex); // Take mutex! It will be released when quitting the scope (when returning)
    if (!sRemoteMap.contains(remoteID))
    {
      sRemoteMap.insert({remoteID, {deviceID}});
    }
    else
    {
      std::map<std::string, std::list<std::string>>::iterator it = sRemoteMap.find(remoteID);
      if (it != sRemoteMap.end())
      {
        for (std::string device : it->second)
        {
          if (device == deviceID)
            return false;
        }
        it->second.push_back(deviceID);
      }
      else
        return false;
    }
    return true;
  }

  void IoHomeControl::DeleteRemote(const std::string &remoteID)
  {
    std::lock_guard<std::mutex> guard(sRemoteMapMutex); // Take mutex! It will be released when quitting the scope (when returning)
    sRemoteMap.erase(remoteID);
  }

  bool IoHomeControl::ConfigureDeviceToSendStatus(const std::string &deviceID)
  {
    if (!mInitialized || !mReceiving || mPassiveMode)
    {
      IO_LOGE("ConfigureDeviceToSendStatus: invalid state! (not initialized or not listening or passive mode)");
      return false;
    }
    std::map<std::string, IoDevice>::iterator it = sDeviceMap.find(deviceID);
    if (it == sDeviceMap.end())
    {
      IO_LOGE("ConfigureDeviceToSendStatus: no device found in list!");
      return false;
    }
    else if (it->second.is_deleted)
    {
      IO_LOGE("ConfigureDeviceToSendStatus: device is marked as deleted, add it before using it!");
      return false;
    }
    if (xSemaphoreTake(sMutex, MUTEX_MAX_WAIT_TICKS))
    {
      IoFrame request;
      IoFrame response;
      bool ret = false;
      UBaseType_t currentPriority = uxTaskPriorityGet(NULL);
      vTaskPrioritySet(NULL, IO_FRAME_PROCESSING_TASK); // change task priority to higher!
      if (create_set_config1_command(request, mOwnNodeId, it->second.info.node_id, it->second.info.is_low_power) && SendAndReceive(request, response, FREQUENCY_CHANNEL_2))
      {
        if (response.command_id == CMD_SET_CONFIG1_RESPONSE)
        {
          ret = true;
        }
        else if (response.command_id == CMD_ERROR_RESPONSE)
        {
          IO_LOGE("ConfigureDeviceToSendStatus: it seems device doesn't support this command!");
          ret = false;
        }
        else
        {
          IO_LOGE("ConfigureDeviceToSendStatus: unexpected response!");
          ret = false;
        }
      }
      else
      {
        IO_LOGE("ConfigureDeviceToSendStatus: failed to send request or didn't receive a response!");
      }
      vTaskPrioritySet(NULL, currentPriority); // restore task priority
      xSemaphoreGive(sMutex);
      return ret;
    }
    else
    {
      IO_LOGE("ConfigureDeviceToSendStatus: failed to take mutex!");
      return false;
    }
  }

  bool IoHomeControl::IdentifyDevice(const std::string &deviceID)
  {
    if (!mInitialized || !mReceiving || mPassiveMode)
    {
      IO_LOGE("IdentifyDevice: invalid state! (not initialized or not listening or passive mode)");
      return false;
    }
    std::map<std::string, IoDevice>::iterator it = sDeviceMap.find(deviceID);
    if (it == sDeviceMap.end())
    {
      IO_LOGE("IdentifyDevice: no device found in list!");
      return false;
    }
    else if (it->second.is_deleted)
    {
      IO_LOGE("IdentifyDevice: device is marked as deleted, add it before using it!");
      return false;
    }
    if (xSemaphoreTake(sMutex, MUTEX_MAX_WAIT_TICKS))
    {
      IoFrame request;
      IoFrame response;
      bool ret = false;
      UBaseType_t currentPriority = uxTaskPriorityGet(NULL);
      vTaskPrioritySet(NULL, IO_FRAME_PROCESSING_TASK); // change task priority to higher!
      if (create_identify_request(request, mOwnNodeId, it->second.info.node_id, it->second.info.is_low_power) && SendAndReceive(request, response, FREQUENCY_CHANNEL_2))
      {
        ret = true; // command sent and response received (response may be CMD_ERROR_RESPONSE which is expected for identify)
      }
      else
      {
        IO_LOGE("IdentifyDevice: failed to send request or didn't receive a response!");
      }
      vTaskPrioritySet(NULL, currentPriority); // restore task priority
      xSemaphoreGive(sMutex);
      return ret;
    }
    else
    {
      IO_LOGE("IdentifyDevice: failed to take mutex!");
      return false;
    }
  }

  void IoHomeControl::InvertOpenClosePositionForDevice(const std::string &deviceID)
  {
    if (!mInitialized || !mReceiving || mPassiveMode)
    {
      IO_LOGE("InvertOpenClosePositionForDevice: invalid state! (not initialized or not listening or passive mode)");
      return;
    }
    std::map<std::string, IoDevice>::iterator it = sDeviceMap.find(deviceID);
    if (it == sDeviceMap.end())
    {
      IO_LOGE("InvertOpenClosePositionForDevice: no device found in list!");
      return;
    }
    else if (it->second.is_deleted)
    {
      IO_LOGE("InvertOpenClosePositionForDevice: device is marked as deleted, add it before using it!");
      return;
    }
    else
    {
      it->second.info.is_openclose_inverted = !it->second.info.is_openclose_inverted; // invert
      it->second.next_status_update_timestamp = 0;                                    // force update
    }
  }

  bool IoHomeControl::SendRaw(const std::string &rawFrame, uint32_t frequency)
  {
    if (!mInitialized || !mReceiving || mPassiveMode)
    {
      IO_LOGE("SendRaw: invalid state! (not initialized or not listening or passive mode)");
      return false;
    }
    if ((rawFrame.length() < 2 * FRAME_MIN_SIZE) || (rawFrame.length() > 2 * FRAME_MAX_SIZE))
    {
      IO_LOGE("SendRaw: invalid frame to send, must be between %d and %d bytes!", FRAME_MIN_SIZE, FRAME_MAX_SIZE);
      return false;
    }
    uint8_t buffer[FRAME_MAX_SIZE];
    HexStringToBuff(rawFrame, buffer, FRAME_MAX_SIZE);
    uint8_t actualLen = rawFrame.length() / 2;
    buffer[0] = (buffer[0] & ~CTRL0_LENGTH_MASK) | ((actualLen - 1) & CTRL0_LENGTH_MASK);
    IoFrame request;
    if (parse_frame(buffer, actualLen, request))
    {
      if (xSemaphoreTake(sMutex, MUTEX_MAX_WAIT_TICKS))
      {
        bool ret = false;
        UBaseType_t currentPriority = uxTaskPriorityGet(NULL);
        vTaskPrioritySet(NULL, IO_FRAME_PROCESSING_TASK);
        if (is_end(request))
        {
          ret = TransmitFrame(request, frequency, is_start(request) ? LONG_PREAMBLE_LENGTH : SHORT_PREAMBLE_LENGTH);
        }
        else
        {
          IoFrame response;
          ret = SendAndReceive(request, response, frequency);
        }
        vTaskPrioritySet(NULL, currentPriority);
        xSemaphoreGive(sMutex);
        return ret;
      }
      else
      {
        IO_LOGE("SendRaw: failed to take mutex!");
        return false;
      }
    }
    else
    {
      IO_LOGE("SendRaw: parsing error, will not send frame!");
      return false;
    }
  }

  bool IoHomeControl::TransmitFrame(const IoFrame &ioframe, uint32_t frequency, uint16_t preamble)
  {
    if (!mInitialized || !mReceiving || mPassiveMode)
      return false;
    // Transmit: add to queue
    TxFrameQueueItem item;
    memcpy(&item.frame, &ioframe, sizeof(IoFrame));
    item.preamble = preamble;
    item.frequency = frequency;

    if (!xQueueSendToBack(sTxIoQueue, &item, 0))
    {
      IO_LOGE("TransmitFrame can't add frame to queue!");
      return false;
    }
    xEventGroupSetBits(sEventGroup, EVENT_BIT_TX);
    return true;
  }

  // ============================================================================
  // 2W Mode Features Implementation
  // ============================================================================

  bool IoHomeControl::SendAndReceive(const IoFrame &request, IoFrame &response, uint32_t frequency)
  {
    uint8_t tries = 3;
    bool setStartFlagToAuthentResponse = false;
    while (tries > 0)
    {
      if (tries < 3)
        vTaskDelay(pdMS_TO_TICKS(TIME_BETWEEN_RETRY_MS));
      tries--;

      // IO_LOGI("SendAndReceive");
      if (TransmitFrame(request, frequency, is_start(request) ? LONG_PREAMBLE_LENGTH : SHORT_PREAMBLE_LENGTH))
      {
        RxFrameQueueItem rxItem;
        if (xQueueReceive(sRxIoQueue, &rxItem, RECEIVED_IO_TREATMENT_WAIT_TICKS))
        {
          // We have a response, check it
          if (memcmp(rxItem.frame.src_node, request.dest_node, NODE_ID_SIZE) != 0     // same device?
              || memcmp(rxItem.frame.dest_node, request.src_node, NODE_ID_SIZE) != 0) // for us?
          {
            IO_LOGE("SendAndReceive: received a response not for current exchange!");
            continue;
          }
          if (rxItem.frame.command_id != CMD_CHALLENGE_REQUEST)
          {
            memcpy(&response, &rxItem.frame, sizeof(response));
            return true; // no need for authentication!
          }

          // We have to authenticate!
          IoFrame challengeResponse;
          if (create_challenge_response(challengeResponse, request.dest_node, mOwnNodeId, rxItem.frame.data, request, mSystemKey))
          {
            if (setStartFlagToAuthentResponse)
              challengeResponse.ctrl_byte_0 |= CTRL0_START; // Set Start bit
            if (TransmitFrame(challengeResponse, frequency, SHORT_PREAMBLE_LENGTH))
            {
              // Now wait for final response
              if (xQueueReceive(sRxIoQueue, &rxItem, RECEIVED_IO_TREATMENT_WAIT_TICKS))
              {
                // We have a response, check it
                if (memcmp(rxItem.frame.src_node, request.dest_node, NODE_ID_SIZE) == 0     // same device?
                    && memcmp(rxItem.frame.dest_node, request.src_node, NODE_ID_SIZE) == 0) // for us?
                {
                  memcpy(&response, &rxItem.frame, sizeof(response));
                  return true;
                }
                else
                  IO_LOGE("SendAndReceive: received a final response not for current exchange!");
              }
              setStartFlagToAuthentResponse = true;
              IO_LOGE("SendAndReceive: didn't receive final response!");
              continue;
            }
            else
            {
              IO_LOGE("ProcessReceivedFrameTask - Error: Failed to send challenge request");
              continue;
            }
          }
          else
          {
            IO_LOGE("ProcessReceivedFrameTask - Error: Failed to create challenge request");
            continue;
          }
        }
        else
        {
          IO_LOGE("SendAndReceive: didn't receive response!");
          continue;
        }
      }
      else
      {
        IO_LOGE("SendAndReceive: didn't transmit request!");
        continue;
      }
    }
    return false;
  }

  bool IoHomeControl::AuthenticateReceivedRequest(const IoFrame &request, uint32_t frequency)
  {
    // IO_LOGI("AuthenticateReceivedRequest");
    // Send challenge
    IoFrame challengeFrame;
    if (!create_challenge_request(challengeFrame, request.src_node, mOwnNodeId) || !TransmitFrame(challengeFrame, frequency, SHORT_PREAMBLE_LENGTH))
    {
      IO_LOGE("Error: Failed to create/send challenge request");
      return false;
    }
    // Listen for challenge response
    RxFrameQueueItem rxItem;
    if (xQueueReceive(sRxIoQueue, &rxItem, RECEIVED_IO_TREATMENT_WAIT_TICKS))
    {
      // We have a response, check it
      if (memcmp(rxItem.frame.src_node, request.src_node, NODE_ID_SIZE) != 0 // same device?
          || memcmp(rxItem.frame.dest_node, mOwnNodeId, NODE_ID_SIZE) != 0)  // for us?
      {
        IO_LOGE("AuthenticateReceivedRequest: received a response not for current exchange!");
        return false;
      }
      if (rxItem.frame.command_id != CMD_CHALLENGE_RESPONSE)
      {
        IO_LOGE("AuthenticateReceivedRequest: received a response that is not challenge response!");
        return false;
      }
      // Check challenge response...
      uint8_t data[FRAME_MAX_SIZE];
      data[0] = request.command_id;
      memcpy(data + 1, request.data, request.data_len);
      if (crypto::verify_hmac(data, request.data_len + 1, rxItem.frame.data, challengeFrame.data, mSystemKey))
      {
        // IO_LOGI("AuthenticateReceivedRequest success!");
        return true;
      }
      else
      {
        IO_LOGE("ProcessReceivedFrameTask - Error: HMAC verification failed!");
        return false;
      }
    }
    else
    {
      IO_LOGE("AuthenticateReceivedRequest: didn't receive response!");
      return false;
    }
  }

  void IoHomeControl::UpdateDeviceStatus(const IoFrame &statusFrame)
  {
    std::string srcDevice = buffToHexString(NODE_ID_SIZE, statusFrame.src_node);
    if (!sDeviceMap.contains(srcDevice))
    {
      if (isPassive())
      {
        // Passive mode: we should create the device automatically
        AddDevice(srcDevice);
      }
      else
      {
        IO_LOGE("UpdateDeviceStatus: unknown device!");
        return;
      }
    }
    std::map<std::string, IoDevice>::iterator deviceIt = sDeviceMap.find(srcDevice);
    if (deviceIt->second.is_deleted) // don't update device marked as deleted
    {
      return;
    }
    switch (statusFrame.command_id)
    {
    case CMD_GET_NAME_RESPONSE:
      if (statusFrame.data_len > 1)
      {
        // statusFrame.data[0]: don't know why, on some devices it is 0x00, on others it is first character (eg DexxoSmartio800)
        // last char in data field is 0x00 or 0x20, don't copy it, so copy at most (len - 2) bytes
        uint8_t begin = statusFrame.data[0] > 0x20 ? 0 : 1;
        size_t rawLen = statusFrame.data_len - begin;
        // Strip trailing null/space bytes
        while (rawLen > 0 && (statusFrame.data[begin + rawLen - 1] == 0x00 || statusFrame.data[begin + rawLen - 1] == 0x20))
          rawLen--;
        // Convert Latin-1 (IO-Homecontrol encoding) to UTF-8
        std::string name = Helpers::EncodingHelpers::Latin1ToUtf8(statusFrame.data + begin, rawLen);
        // Truncate to fit buffer (leave room for null terminator)
        if (name.length() >= CMD_PARAM_NAME_MAXSIZE)
          name.resize(CMD_PARAM_NAME_MAXSIZE - 1);
        memset(deviceIt->second.info.name, 0, sizeof(deviceIt->second.info.name));
        memcpy(deviceIt->second.info.name, name.c_str(), name.length());
      }
      // Note: don't notify device update here, as it is a frame received during device add (notification will be sent later)
      break;
    case CMD_GET_GENERAL_INFO1_RESPONSE:
      // Examples:
      //    353136333334304330360400FFFF / 353136333333384230350400FFFF (RS100 SOLAR IO)
      //    0000000000000000000003000000 (Velux Shutter, like "SSL")
      //    0000000000000000000003000000 (DexxoSmartio800)
      if (statusFrame.data_len > 0)
      {
        std::string info = std::string(statusFrame.data, statusFrame.data + statusFrame.data_len).substr(0, CMD_PARAM_INFO1_MAXSIZE - 1);
        memcpy(deviceIt->second.info.info1, info.c_str(), info.length());
      }
      // Note: don't notify device update here, as it is a frame received during device add (notification will be sent later)
      break;
    case CMD_GET_GENERAL_INFO2_RESPONSE:
      // Examples:
      //    353135333734354130380080030B0000 / 353135333734354130370080030B0000 (RS100 SOLAR IO)
      //    000000000000000000020080030B0000 (Velux Shutter, like "SSL")
      //    35313332363836413037017A03140100 / 3531333236383641303701BA03000300 / 3531333236383641303703C003000000 (DexxoSmartio800)
      if (statusFrame.data_len >= 12)
      {
        std::string info = std::string(statusFrame.data, statusFrame.data + statusFrame.data_len).substr(0, CMD_PARAM_INFO2_MAXSIZE - 1);
        memcpy(deviceIt->second.info.info2, info.c_str(), info.length());
        deviceIt->second.info.device_type = static_cast<DeviceType>(statusFrame.data[10] << 2 | statusFrame.data[11] >> 6);
        deviceIt->second.info.device_subtype = statusFrame.data[11] & CMD_PARAM_SUBTYPE_MASK;
        if (deviceIt->second.info.device_type == DeviceType::HORIZONTAL_AWNING)
          deviceIt->second.info.is_openclose_inverted = true;
      }
      // Note: don't notify device update here, as it is a frame received during device add (notification will be sent later)
      break;
    case CMD_GET_GENERAL_INFO3_RESPONSE:
      // Examples:
      //    06000000000000000000000000000000 / 02000000000000000000000000000000 (RS100 SOLAR IO)
      //    FE 08 (not supported) (DexxoSmartio800)
      // So don't use this response for now, see later if someone can say what it contains!
      // Note: don't notify device update here, as it is a frame received during device add (notification will be sent later)
      break;
    case CMD_PRIVATE_RESPONSE:
      if (statusFrame.data_len >= 8)
      {
        deviceIt->second.is_stopped = (statusFrame.data[0] & CMD_PARAM_STATUS_STOPPED) ? true : false;
        deviceIt->second.last_status_timestamp = esp_timer_get_time();
        if (deviceIt->second.is_stopped)
          deviceIt->second.move_start_us = 0; // movement complete, stop interpolation

        // [0] status, [1] flags, [2-3] target position or marker (D2=stop, D4=do nothing),
        // [4-5] current position, [6-7] timer/unknown, [8-10] originator, [11] 01
        uint16_t tmpTargetPos = statusFrame.data[2] << 8 | statusFrame.data[3];
        uint16_t tmpCurrentPos = statusFrame.data[4] << 8 | statusFrame.data[5];
        if (tmpTargetPos <= CMD_PARAM_STATUS_POS_MAX)
          deviceIt->second.target = tmpTargetPos * 100.0 / CMD_PARAM_STATUS_POS_MAX;
        else if (deviceIt->second.is_stopped && tmpCurrentPos <= CMD_PARAM_STATUS_POS_MAX)
          deviceIt->second.target = tmpCurrentPos * 100.0 / CMD_PARAM_STATUS_POS_MAX; // Marker value (D2/D4), use current as target
        else
          deviceIt->second.target = UNKNOWN_POSITION;
        if (tmpCurrentPos <= CMD_PARAM_STATUS_POS_MAX)
          deviceIt->second.position = tmpCurrentPos * 100.0 / CMD_PARAM_STATUS_POS_MAX;
        else
        {
          // Current position is unknown... let's try to guess it
          if (deviceIt->second.is_stopped)
          {
            if (tmpTargetPos <= CMD_PARAM_STATUS_POS_MAX)
              deviceIt->second.position = deviceIt->second.target;
            else
              deviceIt->second.position = UNKNOWN_POSITION;
          }
        }
        if (deviceIt->second.is_stopped && !hasReachedTargetPosition(deviceIt->second.target, deviceIt->second.position))
          deviceIt->second.is_stopped = false; // some devices set 'stopped' flag when moving, force it to update status!

        // Extract tilt value from 16-byte tilt-extended response only (from 03200100 query)
        // 14-byte responses have unreliable tilt data (often 0000 which is ambiguous)
        if (deviceTypeSupportsTilt(deviceIt->second.info.device_type))
        {
          if (statusFrame.data_len >= 16)
          {
            // 16-byte tilt-extended response: tilt at [13-14]
            uint16_t tmpTiltClosed = statusFrame.data[13] << 8 | statusFrame.data[14];
            if (tmpTiltClosed <= CMD_PARAM_STATUS_POS_MAX)
              deviceIt->second.tilt = 100.0 - (tmpTiltClosed * 100.0 / CMD_PARAM_STATUS_POS_MAX);
            else
              deviceIt->second.tilt = UNKNOWN_POSITION;
          }
        }

        // When do we want to update next time?
        if (deviceIt->second.is_stopped)
        {
          if (deviceIt->second.info.device_type == DeviceType::UNKNOWN || strlen(deviceIt->second.info.name) == 0)
          {
            deviceIt->second.next_status_update_timestamp = esp_timer_get_time() + STATUS_UPDATE_NEXT_TRY_US; // missing info, retry later
          }
          else
            deviceIt->second.next_status_update_timestamp = esp_timer_get_time() + STATUS_UPDATE_MAX_TIME_US; // everything is OK, no update required
        }
        else
        {
          // Currently moving, try to guess when we should have next status update
          if ((statusFrame.data[1] & CMD_PARAM_STATUS_EXPECTED) && !mIgnoreAutoUpdate)
            deviceIt->second.next_status_update_timestamp = esp_timer_get_time() + STATUS_UPDATE_AUTO_MARGIN_US;
          else
          {
            // We will not receive new status automatically (or ignoring auto-update), so let's see if device provides an estimated time
            if (statusFrame.data[7] != 0xFF && statusFrame.data[7] != 0x00)
            {
              // We have an estimate in seconds
              deviceIt->second.next_status_update_timestamp = esp_timer_get_time() + std::max(static_cast<int64_t>(statusFrame.data[7]) * 1000000, STATUS_UPDATE_MANUAL_MARGIN_US);
            }
            else
              deviceIt->second.next_status_update_timestamp = esp_timer_get_time() + STATUS_UPDATE_AUTO_MARGIN_US;
          }
        }
        if (!xQueueSendToBack(sIoDeviceStatusQueue, &deviceIt->second, 0))
        {
          IO_LOGE("UpdateDeviceStatus can't add device to queue!");
        }
      }
      else
        IO_LOGE("UpdateDeviceStatus CMD_PRIVATE_RESPONSE is too short! ({})", statusFrame.data_len);
      break;
    case CMD_PRIVATE2_RESPONSE:
      // Will see later, once someone find something interesting in this frame :)
      break;
    case CMD_STATUS_UPDATE:
      if (statusFrame.data_len >= 11)
      {
        deviceIt->second.is_stopped = (statusFrame.data[0] & CMD_PARAM_STATUS_STOPPED) ? true : false;
        deviceIt->second.last_status_timestamp = esp_timer_get_time();
        if (deviceIt->second.is_stopped)
          deviceIt->second.move_start_us = 0; // movement complete, stop interpolation
        uint16_t tmpTargetPos = statusFrame.data[5] << 8 | statusFrame.data[6];
        uint16_t tmpCurrentPos = statusFrame.data[7] << 8 | statusFrame.data[8];
        if (tmpTargetPos <= CMD_PARAM_STATUS_POS_MAX)
          deviceIt->second.target = tmpTargetPos * 100.0 / CMD_PARAM_STATUS_POS_MAX;
        else
          deviceIt->second.target = UNKNOWN_POSITION; // No clue...
        if (tmpCurrentPos <= CMD_PARAM_STATUS_POS_MAX)
          deviceIt->second.position = tmpCurrentPos * 100.0 / CMD_PARAM_STATUS_POS_MAX;
        else
        {
          // Current position is unknown... let's try to guess it
          if (deviceIt->second.is_stopped)
          {
            // let's use target position (if valid) as we have reached it
            if (tmpTargetPos <= CMD_PARAM_STATUS_POS_MAX)
              deviceIt->second.position = deviceIt->second.target;
            else
              deviceIt->second.position = UNKNOWN_POSITION; // No clue...
          }
          // else: we will see when target will be reached. Keep current value for now!
        }
        // When do we want to update next time?
        if (deviceIt->second.is_stopped)
        {
          if (deviceIt->second.info.device_type == DeviceType::UNKNOWN || strlen(deviceIt->second.info.name) == 0)
          {
            deviceIt->second.next_status_update_timestamp = esp_timer_get_time() + STATUS_UPDATE_NEXT_TRY_US; // missing info, retry later
          }
          else
            deviceIt->second.next_status_update_timestamp = esp_timer_get_time() + STATUS_UPDATE_MAX_TIME_US; // everything is OK, no update required
        }
        else
        {
          // Currently moving, try to guess when we should have next status update
          if ((statusFrame.data[1] & CMD_PARAM_STATUS_EXPECTED) && !mIgnoreAutoUpdate)
            deviceIt->second.next_status_update_timestamp = esp_timer_get_time() + STATUS_UPDATE_AUTO_MARGIN_US;
          else
          {
            // We will not receive new status automatically (or ignoring auto-update), so let's see if device provides an estimated time
            if (statusFrame.data[10] != 0xFF && statusFrame.data[10] != 0x00)
            {
              // We have an estimate in seconds
              deviceIt->second.next_status_update_timestamp = esp_timer_get_time() + std::max(static_cast<int64_t>(statusFrame.data[10]) * 1000000, STATUS_UPDATE_MANUAL_MARGIN_US);
            }
            else
              deviceIt->second.next_status_update_timestamp = esp_timer_get_time() + STATUS_UPDATE_AUTO_MARGIN_US;
          }
        }
        if (!xQueueSendToBack(sIoDeviceStatusQueue, &deviceIt->second, 0))
        {
          IO_LOGE("UpdateDeviceStatus can't add device to queue!");
        }
      }
      else
        IO_LOGE("UpdateDeviceStatus CMD_STATUS_UPDATE is too short! ({})", statusFrame.data_len);
      break;
    default:
      IO_LOGE("UpdateDeviceStatus: don't know how to update status from 0x{:02X}!", statusFrame.command_id);
      break;
    }
  }

  bool IoHomeControl::DeviceGetGenericInfo(const std::string &deviceID, uint8_t requestID)
  {
    if (!mInitialized || !mReceiving || mPassiveMode)
    {
      IO_LOGE("DeviceGetGenericInfo: invalid state! (not initialized or not listening or passive mode)");
      return false;
    }
    uint8_t tmpDeviceId[NODE_ID_SIZE];
    HexStringToBuff(deviceID, tmpDeviceId, NODE_ID_SIZE);
    if (xSemaphoreTake(sMutex, MUTEX_MAX_WAIT_TICKS))
    {
      IoFrame request;
      IoFrame response;
      bool ret = false;
      uint8_t expectedResponseID;
      auto devIt = sDeviceMap.find(deviceID);
      bool is_low_power = (devIt != sDeviceMap.end() && devIt->second.info.device_type != DeviceType::UNKNOWN)
                            ? devIt->second.info.is_low_power
                            : true;
      switch (requestID)
      {
      case CMD_GET_NAME:
        ret = create_getname_request(request, mOwnNodeId, tmpDeviceId, is_low_power);
        expectedResponseID = CMD_GET_NAME_RESPONSE;
        break;
      case CMD_GET_GENERAL_INFO1:
        ret = create_getinfo1_request(request, mOwnNodeId, tmpDeviceId, is_low_power);
        expectedResponseID = CMD_GET_GENERAL_INFO1_RESPONSE;
        break;
      case CMD_GET_GENERAL_INFO2:
        ret = create_getinfo2_request(request, mOwnNodeId, tmpDeviceId, is_low_power);
        expectedResponseID = CMD_GET_GENERAL_INFO2_RESPONSE;
        break;
      case CMD_GET_GENERAL_INFO3:
        ret = create_getinfo3_request(request, mOwnNodeId, tmpDeviceId, is_low_power);
        expectedResponseID = CMD_GET_GENERAL_INFO3_RESPONSE;
        break;
      case CMD_PRIVATE:
      {
        // Use tilt-extended status request for devices that support tilt
        auto it = sDeviceMap.find(deviceID);
        if (it != sDeviceMap.end() && deviceTypeSupportsTilt(it->second.info.device_type))
          ret = create_getstatus03_tilt_request(request, mOwnNodeId, tmpDeviceId);
        else
          ret = create_getstatus03_request(request, mOwnNodeId, tmpDeviceId);
        expectedResponseID = CMD_PRIVATE_RESPONSE;
        break;
      }
      default:
        break;
      }
      if (ret && SendAndReceive(request, response, FREQUENCY_CHANNEL_2))
      {
        if (response.command_id == expectedResponseID)
        {
          UpdateDeviceStatus(response);
          ret = true;
        }
        else
        {
          IO_LOGE("DeviceGetGenericInfo: received invalid response!");
        }
      }
      else
      {
        IO_LOGE("DeviceGetGenericInfo: failed to send request or got no response!");
      }
      xSemaphoreGive(sMutex);
      return ret;
    }
    else
    {
      IO_LOGE("DeviceGetGenericInfo: failed to take mutex!");
      return false;
    }
  }

  bool IoHomeControl::DeviceGetBattery(const std::string &deviceID)
  {
    if (!mInitialized || !mReceiving || mPassiveMode)
    {
      IO_LOGE("DeviceGetBattery: invalid state! (not initialized or not listening or passive mode)");
      return false;
    }
    uint8_t tmpDeviceId[NODE_ID_SIZE];
    HexStringToBuff(deviceID, tmpDeviceId, NODE_ID_SIZE);
    if (xSemaphoreTake(sMutex, MUTEX_MAX_WAIT_TICKS))
    {
      IoFrame request, response;
      bool status_ok = false, state_ok = false;
      uint16_t battery_status = 0, battery_state = 0;
      bool is_battery_powered = false;
      UBaseType_t currentPriority = uxTaskPriorityGet(NULL);
      vTaskPrioritySet(NULL, IO_FRAME_PROCESSING_TASK);

      if (create_getbattery_request(request, mOwnNodeId, tmpDeviceId, 0x06) && SendAndReceive(request, response, FREQUENCY_CHANNEL_2) && response.command_id == CMD_PRIVATE_RESPONSE && response.data_len >= 4)
      {
        is_battery_powered = (response.data[1] == 0x60);
        battery_status = (uint16_t)(response.data[2] << 8) | response.data[3];
        status_ok = true;
      }

      if (create_getbattery_request(request, mOwnNodeId, tmpDeviceId, 0x09) && SendAndReceive(request, response, FREQUENCY_CHANNEL_2) && response.command_id == CMD_PRIVATE_RESPONSE && response.data_len >= 4)
      {
        battery_state = (uint16_t)(response.data[2] << 8) | response.data[3];
        state_ok = true;
      }

      vTaskPrioritySet(NULL, currentPriority);
      xSemaphoreGive(sMutex);

      if (status_ok || state_ok)
        IO_LOGI("Battery {}: powered={}, status=0x{:04X}, state=0x{:04X}",
                deviceID, is_battery_powered ? "battery/solar" : "mains",
                battery_status, battery_state);
      else
        IO_LOGE("DeviceGetBattery {}: no response", deviceID);

      return status_ok || state_ok;
    }
    IO_LOGE("DeviceGetBattery: failed to take mutex!");
    return false;
  }

  void IoHomeControl::SetKeySniffCallback(KeySniffCallback cb)
  {
    sKeySniffCallback = cb;
  }

  void IoHomeControl::SetMovementStartedCallback(MovementStartedCallback cb)
  {
    sMovementStartedCallback = cb;
  }

  void IoHomeControl::StartKeySniff()
  {
    memset(sSniffedKey, 0, sizeof(sSniffedKey));
    sSniffStartUs = esp_timer_get_time();
    sSniffKeyActive = true;
    IO_LOGI("Key sniffing started (120 s window)");
  }

  void IoHomeControl::StopKeySniff()
  {
    sSniffKeyActive = false;
    IO_LOGI("Key sniffing stopped");
  }

  bool IoHomeControl::IsKeySniffActive() const
  {
    return sSniffKeyActive;
  }

  std::string IoHomeControl::GetSniffedKey() const
  {
    return std::string(sSniffedKey);
  }

} // namespace iohome
