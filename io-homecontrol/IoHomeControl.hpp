/**
 * @file IoHomeControl.hpp
 * @brief io-homecontrol Node Controller
 * @author iown-homecontrol project
 *
 * High-level controller for io-homecontrol devices supporting only 2W mode.
 */

#pragma once

#include <string>

#include "esp_log_level.h"

#include "radio/RadioModule.hpp"
#include "protocol/iohome_constants.h"
#include "protocol/iohome_crypto.h"
#include "protocol/iohome_frame.hpp"
#include "protocol/iohome_commands.hpp"
#include "protocol/iohome_device.hpp"

namespace iohome
{

  typedef void (*LoggerCallback)(esp_log_level_t log_level, const char *tag, std::string log); // Callback to receive logs from the IO controller (if verbose)
  typedef void (*UpdatedDeviceCallback)(const std::string deviceID, const IoDevice &device);   // Callback to receive status update of devices

  /**
   * @brief io-homecontrol Node Controller
   *
   * This class provides a high-level interface for controlling io-homecontrol
   * devices. It handles Radio communication, encryption, and protocol details in 2W mode.
   */
  class IoHomeControl
  {
  public:
    /// @brief handle to RadioModule.
    /// @attention Do not use directly, it is used internally.
    RadioLinks::RadioModule *mRadio;

    /// @brief Construct a new IoHomeControl object
    /// @param radio Pointer to Radio PhysicalLayer (e.g., RadioSX1276)
    /// @param logger Logging function
    IoHomeControl(RadioLinks::RadioModule *radio, LoggerCallback logger, UpdatedDeviceCallback deviceStatusCallback);

    /// @brief Initialize the controller
    /// @param own_node_id This controller's node ID (6 characters as hex representation of the 3 bytes, eg "112233")
    /// @param system_key System key for encryption (32 characters as hex representation of the 16 bytes, eg "0011...EEFF")
    /// @param isPassive true to not process received frames internally (listening mode)
    /// @return true on success, false on error
    bool Begin(const std::string &own_node_id,
               const std::string &system_key,
               bool isPassive = false);

    /// @brief Configure physical layer parameters
    /// @details Configure frequency, modulation, data rate, etc.
    /// @param power Power in dBm
    /// @return 0 on success, error code otherwise
    int16_t ConfigureRadio(uint8_t power = 20);

    /// @brief Start receiving frames
    /// @return 0 on success, error code otherwise
    int16_t StartReceive();

    /// @brief Stop receiving frames
    void StopReceive();

    /// @brief Enable/disable verbose logging
    /// @param enable true to enable, false to disable
    void SetVerbose(bool enable) { mVerbose = enable; }

    /// @brief Get verbose logging
    /// @return true if verbose logging enabled
    bool isVerbose() { return mVerbose; }

    /// @brief Set ignore auto-update flag. When enabled, the timer value from device response is used
    ///        instead of trusting the device to send a proactive status update (0x71).
    /// @param enable true to ignore auto-update flag and use timer, false to trust auto-update
    void SetIgnoreAutoUpdate(bool enable) { mIgnoreAutoUpdate = enable; }

    /// @brief Get ignore auto-update flag
    /// @return true if auto-update flag is ignored
    bool isIgnoreAutoUpdate() { return mIgnoreAutoUpdate; }

    /// @brief Get active/passive mode. Was set by calling Begin().
    /// @return true if active, false if passive (no radio transmission, only listening)
    bool isPassive() { return mPassiveMode; }

    /// @brief Get Listening status
    /// @return true if listening for incoming frames on radio, false otherwise.
    bool isReceiving() { return mReceiving; }

    /// @brief Process received frames
    /// @attention Do not use directly, it is used by internal thread.
    void ProcessReceivedFrameTask();

    /// @brief Update status of the devices when necessary or after periodic time
    /// @attention Do not use directly, it is used by internal thread.
    void UpdateDevicesStatusTask();

    // ========================================================================
    // 2W Mode Features
    // ========================================================================

    /// @brief Add 2W device to controller
    /// @param deviceID Device ID (6 characters as hex representation of the 3 bytes, eg "112233")
    void AddDevice(const std::string &deviceID);

    /// @brief Restore 2W device to controller by providing an IoDevice object
    /// @param deviceID Device ID (6 characters as hex representation of the 3 bytes, eg "112233")
    /// @param device IoDevice object to restore (read from storage for example)
    void RestoreDevice(const std::string &deviceID, const iohome::IoDevice &device);

    /// @brief Delete 2W device from controller
    /// @param deviceID Device ID (6 characters as hex representation of the 3 bytes, eg "112233")
    void DeleteDevice(const std::string &deviceID);

    /// @brief Start device discovery and pair any discovered device
    bool DiscoverAndPairDevice();

    /// @brief Set position of an actuator (e.g., blind, shutter)
    /// @param deviceID Device ID (6 characters as hex representation of the 3 bytes, eg "112233")
    /// @param position Position value (0 is open / 0% closed / light on / switch on, 100 is 100% closed / light off / switch off)
    /// @return true on success, false on error
    bool SetDevicePosition(const std::string &deviceID, uint8_t position, bool quiet = false);

    /// @brief Set tilt of an actuator (e.g., venetian blind)
    /// @param deviceID Device ID (6 characters as hex representation of the 3 bytes, eg "112233")
    /// @param tilt Tilt value (0 = closed, 100 = fully open)
    /// @return true on success, false on error
    bool SetDeviceTilt(const std::string &deviceID, uint8_t tilt);

    /// @brief Check if device supports tilt based on device type
    /// @param deviceID Device ID (6 characters as hex representation of the 3 bytes, eg "112233")
    /// @return true if device supports tilt, false otherwise
    bool isDeviceTiltSupported(const std::string &deviceID);

    /// @brief Set an actuator (e.g., blind, shutter) to its favorite position (like "My" button on Somfy remote)
    /// @param deviceID Device ID (6 characters as hex representation of the 3 bytes, eg "112233")
    /// @return
    inline bool SetDeviceToFavoritePosition(const std::string &deviceID, bool quiet = false)
    {
      return SetDevicePosition(deviceID, CMD_PARAM_POSITION_FAVORITE, quiet);
    }

    /// @brief Open an actuator (0% closed / light on / switch on)
    /// @param deviceID Device ID (6 characters as hex representation of the 3 bytes, eg "112233")
    /// @return true on success, false on error
    bool OpenDevice(const std::string &deviceID, bool quiet = false);

    /// @brief Close an actuator (100% closed / light off / switch off)
    /// @param deviceID Device ID (6 characters as hex representation of the 3 bytes, eg "112233")
    /// @return true on success, false on error
    bool CloseDevice(const std::string &deviceID, bool quiet = false);

    /// @brief Stop actuator movement
    /// @param deviceID Device ID (6 characters as hex representation of the 3 bytes, eg "112233")
    /// @return true on success, false on error
    inline bool StopDevice(const std::string &deviceID)
    {
      return SetDevicePosition(deviceID, CMD_PARAM_POSITION_STOP);
    }

    /// @brief Change device name
    /// @param deviceID Device ID (6 characters as hex representation of the 3 bytes, eg "112233")
    /// @param name Name to set, 15 character max
    /// @return true on success, false on error
    bool SetDeviceName(const std::string &deviceID, const std::string &name);

    /// @brief Will force status update of the device (position, moving...)
    /// @param deviceID Device ID (6 characters as hex representation of the 3 bytes, eg "112233")
    /// @return true on success, false on error
    bool ForceDeviceStatusUpdate(const std::string &deviceID);

    /// @brief Depending on device information (type, subtype), says if only Open/Close (or On/Off) only.
    /// @param deviceID
    /// @return true if only Open/Close, false if Stop/SetPosition also available.
    bool isDeviceOpenCloseOnly(const std::string &deviceID);

    /// @brief Declare a remote attached to a device. When the remote is used, device status will be monitored.
    /// @param remoteID Remote ID (6 characters as hex representation of the 3 bytes, eg "112233")
    /// @param deviceID Device ID (6 characters as hex representation of the 3 bytes, eg "112233")
    /// @return true if success, false if failed (unknown device ID, ...)
    bool LinkRemoteToDevice(const std::string &remoteID, const std::string &deviceID);

    /// @brief Delete a previously declared remote
    /// @param remoteID Remote ID (6 characters as hex representation of the 3 bytes, eg "112233")
    void DeleteRemote(const std::string &remoteID);

    /// @brief Send configuration to device to ask for automatic status update if device receives any 1W/2W command
    /// @param deviceID Device ID (6 characters as hex representation of the 3 bytes, eg "112233")
    /// @return true if device was successfully configured, false otherwise (some devices don't support this configuration command)
    bool ConfigureDeviceToSendStatus(const std::string &deviceID);

    /// @brief Invert OPEN/CLOSE position for given device
    /// @param deviceID Device ID (6 characters as hex representation of the 3 bytes, eg "112233")
    void InvertOpenClosePositionForDevice(const std::string &deviceID);

    /// @brief Send an IO frame and wait for a response. Handles authentication automatically if required by remote.
    /// @param rawFrame string representation of the IO frame, from CTRL0 byte to last byte of data (without CRC)
    /// @param frequency frequency to use to send the frame, in Hz
    /// @return true if no error
    bool SendRaw(const std::string &rawFrame, uint32_t frequency);

  protected:
    uint8_t mOwnNodeId[NODE_ID_SIZE]; // Our NodeID (3 bytes)
    uint8_t mSystemKey[AES_KEY_SIZE]; // Our system key (16 bytes)

    bool mInitialized; // true if Init is done
    bool mReceiving;   // true if StartReceive has been called (and no call to StopReceive!)
    bool mVerbose;     // true if verbose mode (logs are sent to registered callback)
    bool mPassiveMode; // true if passive mode (will not send frames to radio, only listening)
    bool mIgnoreAutoUpdate; // true to ignore auto-update flag (0x80) and use timer value instead

    /// @brief Transmit a frame
    /// @param frame IoFrame to transmit
    /// @param frequency Frequency to transmit the frame
    /// @param preamble preamble length before frame data
    /// @return true on success, false on error
    bool TransmitFrame(const IoFrame &frame, uint32_t frequency, uint16_t preamble);

    /// @brief Sends a provided request on specified frequency and provide a response in return. Manages authentication automatically.
    /// @warning You must take sMutex before calling!
    /// @param request Request to send
    /// @param response Response received (only if returning true)
    /// @param frequency Frequency to use to send request
    /// @return true if success (response available), false otherwise.
    bool SendAndReceive(const IoFrame &request, IoFrame &response, uint32_t frequency);

    /// @brief Manages the authentication process related to received request.
    /// @warning You must take sMutex before calling!
    /// @param request The request to authenticate
    /// @param frequency Frequency to use to send challenge
    /// @return true if remote successfully authenticated, false otherwise.
    bool AuthenticateReceivedRequest(const IoFrame &request, uint32_t frequency);

    /// @brief Update internal device status from received frame and send to callback
    /// @param statusFrame Received status frame (can be CMD_PRIVATE_RESPONSE, CMD_PRIVATE2_RESPONSE or CMD_STATUS_UPDATE)
    void UpdateDeviceStatus(const IoFrame &statusFrame);

    /// @brief Send a request and use UpdateDeviceStatus to handle the response
    /// @param deviceID Device ID (6 characters as hex representation of the 3 bytes, eg "112233")
    /// @param requestID ID of the request to send (GetInfo, GetName, GetStatus...)
    /// @return true on success, false on error
    bool DeviceGetGenericInfo(const std::string &deviceID, uint8_t requestID);

    /// @brief Get device name (0x50) - Fill name in IoDeviceInformation struct
    /// @param deviceID Device ID (6 characters as hex representation of the 3 bytes, eg "112233")
    /// @return true on success, false on error
    inline bool DeviceGetName(const std::string &deviceID)
    {
      return DeviceGetGenericInfo(deviceID, CMD_GET_NAME);
    }

    /// @brief Get DeviceInfo1 (0x54) - Fill info1 in IoDeviceInformation struct
    /// @param deviceID Device ID (6 characters as hex representation of the 3 bytes, eg "112233")
    /// @return true on success, false on error
    inline bool DeviceGetGeneralInfo1(const std::string &deviceID)
    {
      return DeviceGetGenericInfo(deviceID, CMD_GET_GENERAL_INFO1);
    }

    /// @brief Get DeviceInfo2 (0x56) - Fill info2, device_type, device_subtype in IoDeviceInformation struct
    /// @param deviceID Device ID (6 characters as hex representation of the 3 bytes, eg "112233")
    /// @return true on success, false on error
    inline bool DeviceGetGeneralInfo2(const std::string &deviceID)
    {
      return DeviceGetGenericInfo(deviceID, CMD_GET_GENERAL_INFO2);
    }

    /// @brief Get DeviceInfo3 (0x58) - Content TBD
    /// @param deviceID Device ID (6 characters as hex representation of the 3 bytes, eg "112233")
    /// @return true on success, false on error
    inline bool DeviceGetGeneralInfo3(const std::string &deviceID)
    {
      return DeviceGetGenericInfo(deviceID, CMD_GET_GENERAL_INFO3);
    }

    /// @brief Get DeviceStatus (0x03) - Fill position and is_stopped in IoDevice struct
    /// @param deviceID Device ID (6 characters as hex representation of the 3 bytes, eg "112233")
    /// @return true on success, false on error
    inline bool DeviceGetStatus03(const std::string &deviceID)
    {
      return DeviceGetGenericInfo(deviceID, CMD_PRIVATE);
    }
  };

} // namespace iohome
