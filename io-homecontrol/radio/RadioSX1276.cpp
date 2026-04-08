#include "RadioSX1276.hpp"
#include "sx1276Regs-Fsk.h"

#include <time.h>
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "esp_log.h"

#include <map>
#include <freertos/FreeRTOS.h>

constexpr uint8_t BOARD_READY_AFTER_POR_MS = 10;
constexpr uint16_t SPI_WRITE = 0x80;
constexpr uint16_t ADDR_MASK = 0x7f;
constexpr uint32_t FXOSC = 32000000;
constexpr uint8_t RF_PACKETCONFIG2_IOHOME_POWERFRAME = 0x10;                      // Missing from SX1276 FSK modem registers and bits definitions
constexpr TickType_t MUTEX_MAX_WAIT_TICKS = ((TickType_t)2 * portTICK_PERIOD_MS); // Time to wait when taking mutex = 2 ms

static QueueHandle_t sGpioEvtQueue = NULL; // Queue containing GPIO triggered (uint32_t)
static SemaphoreHandle_t sMutex;           // Mutex used to protect access to device on SPI bus
static StaticSemaphore_t sMutexBuffer;     // Buffer containing memory allocated to sMutex

/// @brief This function is called by interrupt when GPIO linked to SX1276 are triggered.
/// It puts the GPIO to a queue to be managed by another task
/// @param arg GPIO number
static void IRAM_ATTR gpio_isr_handler(void *arg)
{
    uint32_t gpio_num = (uint32_t)arg;
    if (!xQueueSendFromISR(sGpioEvtQueue, &gpio_num, NULL))
    {
        ESP_DRAM_LOGI("RADIO", "gpio_isr_handler can't add received frame to queue!");
    }
}

namespace RadioLinks
{
    struct regBandWidth
    {
        uint8_t Mant;
        uint8_t Exp;
    };
    // Simplified bandwidth registries evaluation
    std::map<uint32_t, regBandWidth> __bw =
        {
            {25000, {0x01, 0x04}}, // 25KHz
            {50000, {0x01, 0x03}},
            {100000, {0x01, 0x02}},
            {125000, {0x00, 0x02}},
            {200000, {0x01, 0x01}},
            {250000, {0x00, 0x01}} // 250KHz
    };

    /// @brief This task manages all GPIO events received in the queue
    /// @param arg Pointer to RadioSX1276 object that manages the SX1276 radio module
    static void gpio_task(void *arg)
    {
        uint32_t io_num;
        RadioSX1276 *radio = static_cast<RadioSX1276 *>(arg);
        for (;;)
        {
            if (xQueueReceive(sGpioEvtQueue, &io_num, portMAX_DELAY))
            {
                // ESP_LOGI("RADIO", "GPIO[%" PRIu32 "] intr, val: %d", io_num, gpio_get_level(static_cast<gpio_num_t>(io_num)));
                radio->ManageInterrupt(io_num);
            }
        }
    }

    RadioSX1276::RadioSX1276(spi_host_device_t spiHost, int cs, int rst, int dio0, int dio4)
        : mSpiHost(spiHost), mSpiCS(cs), mIoRST(rst), mIoDIO0(dio0), mIoDIO4(dio4), mLastPreambleDetectedTime(0)
    {
    }

    void RadioSX1276::DumpRegisters()
    {
        for (uint8_t i = 0x00; i < 0x7F; i++)
        {
            ESP_LOGI(TAG, "REG 0x%2.2x 0x%2.2x", i, SpiReadByte(i));
        }
    }

    void RadioSX1276::ManageInterrupt(int gpio)
    {
        if (gpio == mIoDIO0)
        {
            // DIO0 = PayloadReady or PacketSent
            if (xSemaphoreTake(sMutex, MUTEX_MAX_WAIT_TICKS))
            {
                uint8_t reg = SpiReadByte(REG_IRQFLAGS2);
                if (reg & RF_IRQFLAGS2_PAYLOADREADY)
                {
                    uint8_t buffer[255];
                    size_t bufferLen = 0;
                    float rssi = SpiReadByte(REG_RSSIVALUE) / -2.0f;
                    while (!(SpiReadByte(REG_IRQFLAGS2) & RF_IRQFLAGS2_FIFOEMPTY) && bufferLen < 255)
                    {
                        buffer[bufferLen] = SpiReadByte(REG_FIFO);
                        bufferLen++;
                    }
                    xSemaphoreGive(sMutex);
                    if (mCallback != nullptr)
                    {
                        mCallback(bufferLen, buffer, mCurrentFrequency, rssi);
                    }
                }
                else if (reg & RF_IRQFLAGS2_PACKETSENT)
                {
                    xSemaphoreGive(sMutex);
                    // ESP_LOGI(TAG, "Packet sent!");
                    if (mIsReceiveMode)
                    {
                        StartReceive();
                    }
                    else
                    {
                        Standby();
                    }
                }
                else
                    xSemaphoreGive(sMutex);
            }
            else
            {
                ESP_LOGE(TAG, "ManageInterrupt - Busy");
            }
        }
        else if (gpio == mIoDIO4)
        {
            // DIO4 = Rssi or PreambleDetect
            if (xSemaphoreTake(sMutex, MUTEX_MAX_WAIT_TICKS))
            {
                if (SpiReadByte(REG_IRQFLAGS1) & RF_IRQFLAGS1_PREAMBLEDETECT)
                {
                    // ESP_LOGI(TAG, "Preamble received!");
                    mLastPreambleDetectedTime = esp_timer_get_time();
                }
                xSemaphoreGive(sMutex);
            }
            else
            {
                ESP_LOGE(TAG, "ManageInterrupt - Busy");
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1)); // Core panic if we don't wait because task has high priority compared to idle task so wdt triggered!
    }

    RadioSX1276::~RadioSX1276()
    {
        Standby();
        spiEnd();
    }

    void RadioSX1276::Init(bool ioMode)
    {
        ESP_LOGI(TAG, "Init...");
        // initialize GPIO for DIO0/DIO4/MISO/RST...
        pinMode(mIoDIO0, GPIO_MODE_INPUT, GPIO_PULLUP_ENABLE, GPIO_PULLDOWN_DISABLE);
        pinMode(mIoDIO4, GPIO_MODE_INPUT, GPIO_PULLUP_ENABLE, GPIO_PULLDOWN_DISABLE);

        // See §5.2.1. POR
        pinMode(mIoRST, GPIO_MODE_INPUT, GPIO_PULLUP_DISABLE, GPIO_PULLDOWN_DISABLE); // Connected to Reset; floating for POR
        // Check the availability of the Radio
        while (!digitalRead(mIoRST))
        {
            // esp_task_wdt_reset();
            vTaskDelay(pdMS_TO_TICKS(1));
        }
        vTaskDelay(pdMS_TO_TICKS(BOARD_READY_AFTER_POR_MS));
        pinMode(mIoRST, GPIO_MODE_OUTPUT, GPIO_PULLUP_ENABLE, GPIO_PULLDOWN_DISABLE);
        digitalWrite(mIoRST, 1);
        vTaskDelay(pdMS_TO_TICKS(BOARD_READY_AFTER_POR_MS));

        sMutex = xSemaphoreCreateMutexStatic(&sMutexBuffer);

        // Initialize SPI
        spiBegin();
        // Initialize SX1276
        Standby(); // Put Radio in Standby mode before init
        Calibrate();
        InitRegisters(ioMode);

        // create a queue to handle gpio event from isr
        sGpioEvtQueue = xQueueCreate(10, sizeof(uint32_t));
        // start gpio task
        xTaskCreate(gpio_task, "radio_gpio_task", 4096, this, 10, NULL); // priority 0 to avoid wdt triggering if priority 10 for example

        // hook isr handler for specific gpio pin
        gpio_isr_handler_add(static_cast<gpio_num_t>(mIoDIO0), gpio_isr_handler, (void *)mIoDIO0);
        // hook isr handler for specific gpio pin
        gpio_isr_handler_add(static_cast<gpio_num_t>(mIoDIO4), gpio_isr_handler, (void *)mIoDIO4);

        // change gpio interrupt type for DIO0/DIO4 pins
        gpio_set_intr_type(static_cast<gpio_num_t>(mIoDIO0), GPIO_INTR_POSEDGE);
        gpio_set_intr_type(static_cast<gpio_num_t>(mIoDIO4), GPIO_INTR_ANYEDGE);

        ESP_LOGI(TAG, "Init... end");
    }

    void RadioSX1276::RegisterReceiveCallback(void (*func_ptr)(uint8_t, uint8_t[], uint32_t, float))
    {
        mCallback = func_ptr;
    }

    RADIO_ERRCODE RadioSX1276::SetFrequency(uint32_t frequency)
    {
        if (mCurrentFrequency == frequency)
            return RADIO_ERR_NONE;
        // ESP_LOGI(TAG, "SetFrequency - %d", frequency);
        /*uint32_t FRF = (newFreq * (uint32_t(1) << RADIOLIB_SX127X_DIV_EXPONENT)) / RADIOLIB_SX127X_CRYSTAL_FREQ;*/
        uint32_t tmpVal = static_cast<uint32_t>((static_cast<float>(frequency) / FXOSC) * (1 << 19));
        uint8_t out[3];
        out[0] = (tmpVal & 0x00ff0000) >> 16;
        out[1] = (tmpVal & 0x0000ff00) >> 8;
        out[2] = (tmpVal & 0x000000ff); // If Radio is active writing LSB triggers frequency change
        if (xSemaphoreTake(sMutex, MUTEX_MAX_WAIT_TICKS))
        {
            RADIO_ERRCODE ret = SpiWriteBytes(REG_FRFMSB, out, 3);
            xSemaphoreGive(sMutex);
            if (ret == RADIO_ERR_NONE)
                mCurrentFrequency = frequency;
            return ret;
        }
        else
        {
            ESP_LOGE(TAG, "SetFrequency - No mutex available!");
            return RADIO_ERR_BUSY;
        }
    }

    RADIO_ERRCODE RadioSX1276::SetModulation(Modulation modulation)
    {
        switch (modulation)
        {
        case Modulation::FSK:
        {
            uint8_t rfOpMode = SpiReadByte(REG_OPMODE);
            rfOpMode &= RF_OPMODE_LONGRANGEMODE_MASK;
            rfOpMode |= RF_OPMODE_LONGRANGEMODE_OFF;
            rfOpMode &= RF_OPMODE_MODULATIONTYPE_MASK;
            rfOpMode |= RF_OPMODE_MODULATIONTYPE_FSK;
            rfOpMode &= RF_OPMODE_MASK;
            rfOpMode |= RF_OPMODE_STANDBY;
            rfOpMode &= ~0x08;
            return SpiWriteByte(REG_OPMODE, rfOpMode);
            break;
        }
        case Modulation::LoRa:
        case Modulation::OOK:
        default:
            break;
        }
        return RADIO_ERR_INVALID_MODULATION;
    }

    RADIO_ERRCODE RadioSX1276::SetOutputPower(uint8_t txPower)
    {
        RADIO_ERRCODE err = RADIO_ERR_NONE;
        // Set Output power
        if (txPower > 17)
        {
            err = SpiWriteByte(REG_PACONFIG, RF_PACONFIG_PASELECT_MASK | RF_PACONFIG_PASELECT_PABOOST);
            if (err != RADIO_ERR_NONE)
            {
                ESP_LOGE(TAG, "SetOutputPower - SpiWriteByte error!");
                return err;
            }
            err = SpiWriteByte(REG_PADAC, 0x87); //  RF_PADAC_20DBM_MASK | RF_PADAC_20DBM_ON); // turn 20dBm mode on
            if (err != RADIO_ERR_NONE)
            {
                ESP_LOGE(TAG, "SetOutputPower - SpiWriteByte error!");
                return err;
            }
        }
        else if (txPower > 1)
        {
            uint8_t OutputPower = txPower - 2; // Pout=17-(15-OutputPower) if PaSelect = 1 (PA_BOOST pin)
            err = SpiWriteByte(REG_PACONFIG, RF_PACONFIG_PASELECT_PABOOST | OutputPower);
            if (err != RADIO_ERR_NONE)
            {
                ESP_LOGE(TAG, "SetOutputPower - SpiWriteByte error!");
                return err;
            }
        }
        else
            return RADIO_ERR_INVALID_OUTPUT_POWER;
        // Set Output Current Protection
        err = SpiWriteByte(REG_OCP, RF_OCP_ON | RF_OCP_TRIM_240_MA); // Could be changed depending on power output?
        if (err != RADIO_ERR_NONE)
        {
            ESP_LOGE(TAG, "SetOutputPower - SpiWriteByte error!");
            return err;
        }
        return err;
    }

    RADIO_ERRCODE RadioSX1276::SetBitRate(uint32_t bitrate)
    {
        // Note: we don't implement BitrateFrac part as we use standard bitrate (REG_BITRATEFRAC)
        uint32_t tmpVal = FXOSC / bitrate;
        uint8_t out[2];
        out[0] = (tmpVal & 0x0000ff00) >> 8;
        out[1] = (tmpVal & 0x000000ff);
        return SpiWriteBytes(REG_BITRATEMSB, out, 2);
    }

    RADIO_ERRCODE RadioSX1276::SetFrequencyDeviation(uint32_t deviation)
    {
        uint32_t tmpVal = static_cast<uint32_t>((static_cast<float>(deviation) / FXOSC) * (1 << 19));
        uint8_t out[2];
        out[0] = (tmpVal & 0x0000ff00) >> 8;
        out[1] = (tmpVal & 0x000000ff);
        return SpiWriteBytes(REG_FDEVMSB, out, 2);
    }

    RADIO_ERRCODE RadioSX1276::SetSyncWord(uint8_t size, uint8_t *syncWord)
    {
        if (size > 8)
            return RADIO_ERR_INVALID_SYNCWORD;
        RADIO_ERRCODE err = RADIO_ERR_NONE;
        err = SpiWriteByte(REG_SYNCCONFIG, (SpiReadByte(REG_SYNCCONFIG) & RF_SYNCCONFIG_SYNCSIZE_MASK) | (size - 1));
        if (err != RADIO_ERR_NONE)
        {
            ESP_LOGE(TAG, "SetSyncWord - SpiWriteByte error!");
            return err;
        }
        err = SpiWriteBytes(REG_SYNCVALUE1, syncWord, size);
        if (err != RADIO_ERR_NONE)
        {
            ESP_LOGE(TAG, "SetSyncWord - SpiWriteByte error!");
            return err;
        }
        return err;
    }

    RADIO_ERRCODE RadioSX1276::SetBandwidth(uint32_t bandwidth)
    {
        RADIO_ERRCODE err = RADIO_ERR_INVALID_BANDWIDTH;
        for (auto &it : __bw)
        {
            if (it.first == bandwidth)
            {
                err = SpiWriteByte(REG_RXBW, it.second.Mant | it.second.Exp);
                if (err != RADIO_ERR_NONE)
                {
                    ESP_LOGE(TAG, "SetBandwidth - SpiWriteByte error!");
                    return err;
                }
                err = SpiWriteByte(REG_AFCBW, it.second.Mant | it.second.Exp);
                if (err != RADIO_ERR_NONE)
                {
                    ESP_LOGE(TAG, "SetBandwidth - SpiWriteByte error!");
                    return err;
                }
            }
        }
        return err;
    }

    RADIO_ERRCODE RadioSX1276::SetPreambleLength(uint16_t preambleLen)
    {
        // ESP_LOGI(TAG, "SetPreambleLength - %d", preambleLen);
        RADIO_ERRCODE err = RADIO_ERR_NONE;
        if (xSemaphoreTake(sMutex, MUTEX_MAX_WAIT_TICKS))
        {
            err = SpiWriteByte(REG_PREAMBLEMSB, preambleLen >> 8);
            if (err != RADIO_ERR_NONE)
            {
                ESP_LOGE(TAG, "SetPreambleLength - SpiWriteByte error!");
                xSemaphoreGive(sMutex);
                return err;
            }
            err = SpiWriteByte(REG_PREAMBLELSB, preambleLen);
            if (err != RADIO_ERR_NONE)
            {
                ESP_LOGE(TAG, "SetPreambleLength - SpiWriteByte error!");
                xSemaphoreGive(sMutex);
                return err;
            }
            xSemaphoreGive(sMutex);
        }
        else
        {
            ESP_LOGE(TAG, "SetPreambleLength - No mutex available!");
            return RADIO_ERR_BUSY;
        }
        return err;
    }

    RADIO_ERRCODE RadioSX1276::StartReceive()
    {
        RADIO_ERRCODE err = RADIO_ERR_NONE;
        err = Standby(); // Stop everything
        if (err != RADIO_ERR_NONE)
        {
            ESP_LOGE(TAG, "StartReceive - Standby error!");
            return err;
        }
        if (xSemaphoreTake(sMutex, MUTEX_MAX_WAIT_TICKS))
        {
            err = SpiWriteByte(REG_OPMODE, (SpiReadByte(REG_OPMODE) & RF_OPMODE_MASK) | RF_OPMODE_RECEIVER); // Start RX
            xSemaphoreGive(sMutex);
            if (err != RADIO_ERR_NONE)
            {
                ESP_LOGE(TAG, "StartReceive - SpiWriteByte error!");
                return err;
            }
            // ESP_LOGI(TAG, "StartReceive");
        }
        else
        {
            ESP_LOGE(TAG, "StartReceive - No mutex available!");
            return RADIO_ERR_BUSY;
        }
        mIsReceiveMode = true;
        return err;
    }

    RADIO_ERRCODE RadioSX1276::StopReceive()
    {
        // ESP_LOGI(TAG, "StopReceive");
        RADIO_ERRCODE err = Standby(); // Stop everything
        if (err == RADIO_ERR_NONE)
            mIsReceiveMode = false;
        return err;
    }

    RADIO_ERRCODE RadioSX1276::Send(uint8_t len, uint8_t *buffer, uint16_t preambleLen, uint32_t frequency)
    {
        RADIO_ERRCODE err = RADIO_ERR_NONE;
        err = Standby(); // Stop everything
        if (err != RADIO_ERR_NONE)
        {
            ESP_LOGE(TAG, "Send - Standby error!");
            return err;
        }
        err = SetPreambleLength(preambleLen);
        if (err != RADIO_ERR_NONE)
        {
            ESP_LOGE(TAG, "Send - SetPreambleLength error!");
            return err;
        }
        err = SetFrequency(frequency);
        if (err != RADIO_ERR_NONE)
        {
            ESP_LOGE(TAG, "Send - SetFrequency error!");
            return err;
        }
        if (xSemaphoreTake(sMutex, MUTEX_MAX_WAIT_TICKS))
        {
            err = SpiWriteBytes(REG_FIFO, buffer, len); // Write buffer to FIFO
            if (err != RADIO_ERR_NONE)
            {
                ESP_LOGE(TAG, "Send - Standby error!");
                xSemaphoreGive(sMutex);
                return err;
            }
            err = SpiWriteByte(REG_OPMODE, (SpiReadByte(REG_OPMODE) & RF_OPMODE_MASK) | RF_OPMODE_TRANSMITTER); // Send
            xSemaphoreGive(sMutex);
            if (err != RADIO_ERR_NONE)
            {
                ESP_LOGE(TAG, "Send - Standby error!");
                return err;
            }
            // ESP_LOGI(TAG, "Send %d bytes", len);
        }
        else
        {
            ESP_LOGE(TAG, "Send - Busy");
            err = RADIO_ERR_BUSY;
        }
        return err;
    }

    int64_t RadioSX1276::GetLastPreambleDetectedTime()
    {
        return mLastPreambleDetectedTime;
    }

    bool RadioSX1276::isPreambleDetected()
    {
        if (xSemaphoreTake(sMutex, MUTEX_MAX_WAIT_TICKS))
        {
            uint8_t tmp = SpiReadByte(REG_IRQFLAGS1);

            if (tmp & RF_IRQFLAGS1_PREAMBLEDETECT)
            {
                // ESP_LOGI(TAG, "isPreambleDetected - 1");
                uint8_t err = SpiWriteByte(REG_IRQFLAGS1, tmp & RF_IRQFLAGS1_PREAMBLEDETECT);
                if (err != RADIO_ERR_NONE)
                {
                    ESP_LOGE(TAG, "isPreambleDetected - SpiWriteByte error!");
                }
            }
            xSemaphoreGive(sMutex);
            return (tmp & RF_IRQFLAGS1_PREAMBLEDETECT) != 0;
        }
        else
        {
            ESP_LOGE(TAG, "isPreambleDetected - Busy");
        }
        return false;
    }

    void RadioSX1276::spiBegin()
    {
        spi_device_interface_config_t dev = {};
        dev.command_bits = 8;                    // Bit 7 = WRITE flag | Bits [0-6] = register address
        dev.address_bits = 0;                    // register address included in command bits
        dev.mode = 0;                            // synchronous full-duplex protocol corresponding to CPOL = 0 and CPHA = 0 in Motorola/Freescale nomenclature. See datasheet §2.2
        dev.clock_speed_hz = SPI_MASTER_FREQ_8M; // SX1276 supports up to 10MHz. See datasheet §2.5.6 Digital Specification / Fsck
        dev.spics_io_num = mSpiCS;
        dev.queue_size = 1;

        esp_err_t err = spi_bus_add_device(mSpiHost, &dev, &mSpiHandle);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "spi_bus_add_device: %s", esp_err_to_name(err));
        }
    }

    void RadioSX1276::spiEnd()
    {
        if (mSpiHandle)
        {
            (void)spi_bus_remove_device(mSpiHandle);
            mSpiHandle = nullptr;
        }
        //(void)spi_bus_free(mSpiHost); // Don't do that if there are other devices on the same bus!
    }

    void RadioSX1276::pinMode(int pin, gpio_mode_t mode, gpio_pullup_t pullup, gpio_pulldown_t pulldown)
    {
        if (pin == GPIO_NUM_NC)
            return;
        gpio_config_t cfg = {};
        cfg.pin_bit_mask = (1ULL << pin);
        cfg.mode = mode;
        cfg.pull_up_en = pullup;
        cfg.pull_down_en = pulldown;
        cfg.intr_type = GPIO_INTR_DISABLE;
        esp_err_t err = gpio_config(&cfg);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "pinMode - gpio_config failed : %s", esp_err_to_name(err));
            return;
        }
    }

    RADIO_ERRCODE RadioSX1276::digitalWrite(int pin, uint32_t value)
    {
        if (pin == GPIO_NUM_NC)
            return RADIO_ERR_INVALID_GPIO;
        esp_err_t err = gpio_set_level(static_cast<gpio_num_t>(pin), value != 0);
        return (err != ESP_OK) ? RADIO_ERR_HARDWARE : RADIO_ERR_NONE;
    }

    uint32_t RadioSX1276::digitalRead(int pin)
    {
        if (pin == GPIO_NUM_NC)
            return 0;
        return static_cast<uint32_t>(gpio_get_level(static_cast<gpio_num_t>(pin)));
    }

    RADIO_ERRCODE RadioSX1276::SpiWriteByte(uint8_t regAddr, uint8_t data)
    {
        return SpiWriteBytes(regAddr, &data, 1);
    }

    RADIO_ERRCODE RadioSX1276::SpiWriteBytes(uint8_t regAddr, uint8_t *data, uint8_t len)
    {
        if (mSpiHandle == nullptr)
            return RADIO_ERR_NOT_INITIALIZED;
        esp_err_t err;
        err = spi_device_acquire_bus(mSpiHandle, portMAX_DELAY);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "SpiWriteBytes - spi_device_acquire_bus failed : %s", esp_err_to_name(err));
            return RADIO_ERR_HARDWARE;
        }

        spi_transaction_t t = {};
        t.cmd = SPI_WRITE | (regAddr & ADDR_MASK);
        t.length = (size_t)len * 8; // length in bits

        if (len > 4) // more than 4 bytes
        {
            t.tx_buffer = data;
        }
        else if (len > 0) // 1 to 4 bytes
        {
            t.flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA;
            t.rxlength = 0; // length in bits
            memcpy(t.tx_data, data, len);
        }
        // ESP_LOGI(TAG, "SpiWriteBytes - txlength = %d, rxlength = %d", t.length, t.rxlength);
        err = spi_device_polling_transmit(mSpiHandle, &t);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "SpiWriteBytes - spi_device_polling_transmit failed : %s", esp_err_to_name(err));
            spi_device_release_bus(mSpiHandle);
            return RADIO_ERR_HARDWARE;
        }

        spi_device_release_bus(mSpiHandle);
        return RADIO_ERR_NONE;
    }

    RADIO_ERRCODE RadioSX1276::SpiReadBytes(uint8_t regAddr, uint8_t *data, uint8_t len)
    {
        if (mSpiHandle == nullptr)
            return RADIO_ERR_NOT_INITIALIZED;
        esp_err_t err;
        err = spi_device_acquire_bus(mSpiHandle, portMAX_DELAY);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "SpiReadBytes - spi_device_acquire_bus failed : %s", esp_err_to_name(err));
            return RADIO_ERR_HARDWARE;
        }
        spi_transaction_t t = {};
        t.cmd = regAddr & ADDR_MASK;
        t.length = (size_t)8 * len;
        t.rxlength = (size_t)8 * len;
        if (len > 4) // more than 4 bytes
        {
            t.rx_buffer = data;
        }
        else if (len > 0) // 1 to 4 bytes
        {
            t.flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA;
        }
        // ESP_LOGI(TAG, "SpiReadBytes - txlength = %d, rxlength = %d", t.length, t.rxlength);
        err = spi_device_polling_transmit(mSpiHandle, &t);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "SpiReadBytes - spi_device_polling_transmit failed : %s", esp_err_to_name(err));
            spi_device_release_bus(mSpiHandle);
            return RADIO_ERR_HARDWARE;
        }

        if (len > 0) // 1 to 4 bytes
        {
            memcpy(data, t.rx_data, len);
        }
        spi_device_release_bus(mSpiHandle);
        return RADIO_ERR_NONE;
    }

    uint8_t RadioSX1276::SpiReadByte(uint8_t regAddr)
    {
        uint8_t tmp = 0xFF;
        SpiReadBytes(regAddr, &tmp, 1);
        return tmp;
    }

    RADIO_ERRCODE RadioSX1276::Calibrate()
    {
        RADIO_ERRCODE err = RADIO_ERR_NONE;

        // Save context
        uint8_t regPaConfigInitVal = SpiReadByte(REG_PACONFIG);

        // Cut the PA just in case, RFO output, power = -1 dBm
        err = SpiWriteByte(REG_PACONFIG, RF_PACONFIG_PASELECT_RFO);
        if (err != RADIO_ERR_NONE)
        {
            ESP_LOGE(TAG, "Calibrate - SpiWriteByte error!");
            return err;
        }
        // RC Calibration (only call after setting correct frequency band)
        err = SpiWriteByte(REG_OSC, RF_OSC_RCCALSTART);
        if (err != RADIO_ERR_NONE)
        {
            ESP_LOGE(TAG, "Calibrate - SpiWriteByte error!");
            return err;
        }
        // Start image and RSSI calibration
        err = SpiWriteByte(
            REG_IMAGECAL, (RF_IMAGECAL_AUTOIMAGECAL_MASK & RF_IMAGECAL_IMAGECAL_MASK) | RF_IMAGECAL_IMAGECAL_START);
        if (err != RADIO_ERR_NONE)
        {
            ESP_LOGE(TAG, "Calibrate - SpiWriteByte error!");
            return err;
        }
        // Wait end of calibration
        while ((SpiReadByte(REG_IMAGECAL) & RF_IMAGECAL_IMAGECAL_RUNNING) == RF_IMAGECAL_IMAGECAL_RUNNING)
        {
        }
        // Set a Frequency in HF band
        err = SetFrequency(868000000);
        if (err != RADIO_ERR_NONE)
        {
            ESP_LOGE(TAG, "Calibrate - SpiWriteByte error!");
            return err;
        }
        // Start image and RSSI calibration
        err = SpiWriteByte(
            REG_IMAGECAL, (RF_IMAGECAL_AUTOIMAGECAL_MASK & RF_IMAGECAL_IMAGECAL_MASK) | RF_IMAGECAL_IMAGECAL_START);
        if (err != RADIO_ERR_NONE)
        {
            ESP_LOGE(TAG, "Calibrate - SpiWriteByte error!");
            return err;
        }
        // Wait end of calibration
        while ((SpiReadByte(REG_IMAGECAL) & RF_IMAGECAL_IMAGECAL_RUNNING) == RF_IMAGECAL_IMAGECAL_RUNNING)
        {
        }

        // Restore context
        err = SpiWriteByte(REG_PACONFIG, regPaConfigInitVal);
        if (err != RADIO_ERR_NONE)
        {
            ESP_LOGE(TAG, "Calibrate - SpiWriteByte error!");
            return err;
        }
        return err;
    }

    RADIO_ERRCODE RadioSX1276::InitRegisters(bool ioMode)
    {
        RADIO_ERRCODE err = RADIO_ERR_NONE;

        // Firstly put radio in StandBy mode as some parameters cannot be changed differently
        err = Standby();
        if (err != RADIO_ERR_NONE)
        {
            ESP_LOGE(TAG, "InitRegisters - Standby error!");
            return err;
        }

        // ---------------- Common Register init section ----------------
        // Switch-off clockout
        err = SpiWriteByte(REG_OSC, RF_OSC_CLKOUT_OFF); // This only give power saveing maybe we can use it as ticker µs
        if (err != RADIO_ERR_NONE)
        {
            ESP_LOGE(TAG, "InitRegisters - SpiWriteByte error!");
            return err;
        }

        if (ioMode)
        {
            // Variable packet length, generates working CRC.
            // Packet mode, IoHomeOn, IoHomePowerFrame to be added (0x10) to avoid rx to newly detect the preamble during tx radio shutdown
            // Must CRCAUTOCLEAR_ON or do full clean FIFO !
            err = SpiWriteByte(
                REG_PACKETCONFIG1,
                RF_PACKETCONFIG1_PACKETFORMAT_VARIABLE | RF_PACKETCONFIG1_DCFREE_OFF | RF_PACKETCONFIG1_CRC_ON |
                    RF_PACKETCONFIG1_CRCAUTOCLEAR_ON | RF_PACKETCONFIG1_CRCWHITENINGTYPE_CCITT |
                    RF_PACKETCONFIG1_ADDRSFILTERING_OFF);
            if (err != RADIO_ERR_NONE)
            {
                ESP_LOGE(TAG, "InitRegisters - SpiWriteByte error!");
                return err;
            }

            err = SpiWriteByte(
                REG_PACKETCONFIG2,
                RF_PACKETCONFIG2_DATAMODE_PACKET | RF_PACKETCONFIG2_IOHOME_ON | RF_PACKETCONFIG2_IOHOME_POWERFRAME);
            if (err != RADIO_ERR_NONE)
            {
                ESP_LOGE(TAG, "InitRegisters - SpiWriteByte error!");
                return err;
            }
        }
        else
        {
            err = SpiWriteByte(
                REG_PACKETCONFIG1,
                RF_PACKETCONFIG1_PACKETFORMAT_VARIABLE | RF_PACKETCONFIG1_DCFREE_OFF | RF_PACKETCONFIG1_CRC_OFF |
                    RF_PACKETCONFIG1_CRCAUTOCLEAR_OFF | RF_PACKETCONFIG1_CRCWHITENINGTYPE_CCITT |
                    RF_PACKETCONFIG1_ADDRSFILTERING_OFF);
            if (err != RADIO_ERR_NONE)
            {
                ESP_LOGE(TAG, "InitRegisters - SpiWriteByte error!");
                return err;
            }
            err = SpiWriteByte(
                REG_PACKETCONFIG2,
                RF_PACKETCONFIG2_DATAMODE_PACKET);
            if (err != RADIO_ERR_NONE)
            {
                ESP_LOGE(TAG, "InitRegisters - SpiWriteByte error!");
                return err;
            }
        }
        // Is IoHomePowerFrame useful ?

        // Preamble shall be set to AA for packets to be received by appliances. Sync word shall be set with different values if Rx or Tx
        err = SpiWriteByte(
            REG_SYNCCONFIG,
            RF_SYNCCONFIG_AUTORESTARTRXMODE_WAITPLL_OFF | RF_SYNCCONFIG_PREAMBLEPOLARITY_AA | RF_SYNCCONFIG_SYNC_ON);
        if (err != RADIO_ERR_NONE)
        {
            ESP_LOGE(TAG, "InitRegisters - SpiWriteByte error!");
            return err;
        }

        // Mapping of pins DIO0 to DIO3
        // DIO0: PayloadReady|PacketSent    DIO1: -    DIO2: TimeOut   | DIO3: -
        // Mapping of pins DIO4 and DIO5
        // DIO4: PreambleDetect  DIO5: ModeReady (not used)
        // DIO Mapping Data Packet Table 30 Page 69
        err = SpiWriteByte(
            REG_DIOMAPPING1,
            RF_DIOMAPPING1_DIO0_00 | RF_DIOMAPPING1_DIO1_11 | RF_DIOMAPPING1_DIO2_10 | RF_DIOMAPPING1_DIO3_01);
        if (err != RADIO_ERR_NONE)
        {
            ESP_LOGE(TAG, "InitRegisters - SpiWriteByte error!");
            return err;
        }
        err = SpiWriteByte(REG_DIOMAPPING2, RF_DIOMAPPING2_MAP_PREAMBLEDETECT | RF_DIOMAPPING2_DIO4_11 | RF_DIOMAPPING2_DIO5_11);
        if (err != RADIO_ERR_NONE)
        {
            ESP_LOGE(TAG, "InitRegisters - SpiWriteByte error!");
            return err;
        }

        // Enable Fast Hoping (frequency change) // Not needed all the time
        SpiWriteByte(REG_PLLHOP, SpiReadByte(REG_PLLHOP) | RF_PLLHOP_FASTHOP_ON);
        if (err != RADIO_ERR_NONE)
        {
            ESP_LOGE(TAG, "InitRegisters - SpiWriteByte error!");
            return err;
        }

        // ---------------- TX Register init section ----------------

        // PA Ramp: No Shaping, Ramp up/down 12us
        err = SpiWriteByte(REG_PARAMP, RF_PARAMP_MODULATIONSHAPING_00 | RF_PARAMP_0012_US);
        if (err != RADIO_ERR_NONE)
        {
            ESP_LOGE(TAG, "InitRegisters - SpiWriteByte error!");
            return err;
        }
        // FIFO Threshold - currently useless
        err = SpiWriteByte(REG_FIFOTHRESH, RF_FIFOTHRESH_TXSTARTCONDITION_FIFONOTEMPTY);
        if (err != RADIO_ERR_NONE)
        {
            ESP_LOGE(TAG, "InitRegisters - SpiWriteByte error!");
            return err;
        }

        // ---------------- RX Register init section ----------------
        // Set length checking if passed as parameter
        // The use of maxPayloadLength is not working. Prevents generating PayloadReady signal
        err = SpiWriteByte(REG_PAYLOADLENGTH, 0xff);
        if (err != RADIO_ERR_NONE)
        {
            ESP_LOGE(TAG, "InitRegisters - SpiWriteByte error!");
            return err;
        }
        // RSSI precision +-2dBm
        err = SpiWriteByte(REG_RSSICONFIG, RF_RSSICONFIG_SMOOTHING_8); // 8->0.512 ms // _128); // _32); //_256); //
        if (err != RADIO_ERR_NONE)
        {
            ESP_LOGE(TAG, "InitRegisters - SpiWriteByte error!");
            return err;
        }
        // Activates Timeout interrupt on Preamble
        err = SpiWriteByte(REG_RXCONFIG, RF_RXCONFIG_AFCAUTO_ON | RF_RXCONFIG_AGCAUTO_ON | RF_RXCONFIG_RXTRIGER_PREAMBLEDETECT | RF_RXCONFIG_RESTARTRXONCOLLISION_ON);
        if (err != RADIO_ERR_NONE)
        {
            ESP_LOGE(TAG, "InitRegisters - SpiWriteByte error!");
            return err;
        }

        err = SpiWriteByte(REG_AFCFEI, 0x01);
        if (err != RADIO_ERR_NONE)
        {
            ESP_LOGE(TAG, "InitRegisters - SpiWriteByte error!");
            return err;
        }
        // if AGC_AUTO_ON, RF_LNA_GAIN_XX do nothing
        err = SpiWriteByte(REG_LNA, RF_LNA_BOOST_ON | RF_LNA_GAIN_G1);
        if (err != RADIO_ERR_NONE)
        {
            ESP_LOGE(TAG, "InitRegisters - SpiWriteByte error!");
            return err;
        }

        // Enables Preamble Detect, 2 bytes
        err = SpiWriteByte(
            REG_PREAMBLEDETECT,
            RF_PREAMBLEDETECT_DETECTOR_ON | RF_PREAMBLEDETECT_DETECTORSIZE_2 | RF_PREAMBLEDETECT_DETECTORTOL_10);
        if (err != RADIO_ERR_NONE)
        {
            ESP_LOGE(TAG, "InitRegisters - SpiWriteByte error!");
            return err;
        }
        return err;
    }

    RADIO_ERRCODE RadioSX1276::Standby()
    {
        RADIO_ERRCODE err = RADIO_ERR_NONE;
        if (xSemaphoreTake(sMutex, MUTEX_MAX_WAIT_TICKS))
        {
            err = SpiWriteByte(REG_OPMODE, (SpiReadByte(REG_OPMODE) & RF_OPMODE_MASK) | RF_OPMODE_STANDBY);
            xSemaphoreGive(sMutex);
            if (err != RADIO_ERR_NONE)
            {
                ESP_LOGE(TAG, "Standby - SpiWriteByte error!");
                return err;
            }
        }
        else
        {
            ESP_LOGE(TAG, "Standby - Busy");
            err = RADIO_ERR_BUSY;
        }
        return err;
    }
}