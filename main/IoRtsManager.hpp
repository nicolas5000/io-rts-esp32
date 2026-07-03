#pragma once

#include "MqttHelpers.hpp"
#include "RadioSX1276.hpp"
#include "IoHomeControl.hpp"
#include "DeviceStorage.hpp"

#include <map>
#include <mutex>

namespace IoRts
{
    class IoRtsManager
    {
    public:
        std::mutex mIoDevicesMutex;                         // Mutex to protect IoDevices list
        std::map<std::string, iohome::IoDevice> mIoDevices; // Map of currently managed IoDevices, protected by mIoDevicesMutex, as this list can change in other threads !

        RadioLinks::RadioSX1276 *mSX1276Radio; // Pointer to radio object used in IoHomeControl object
        iohome::IoHomeControl *mIoHome;        // Pointer to IoHomeControl object used to manage Io-HomeControl protocol

        /// @brief Constructor for IoRtsManager
        IoRtsManager();

        /// @brief Ask to reboot ESP32
        void Reboot();

        /// @brief Remove IO device (legacy — calls DeactivateDevice)
        /// @param deviceID device ID (6 characters as hex representation of the 3 bytes, eg "112233")
        void RemoveIoDevice(const std::string &deviceID);

        /// @brief Deactivate device: sets is_deleted=true, stops radio monitoring, keeps NVS file (reversible)
        /// @param deviceID device ID (6 characters as hex representation of the 3 bytes, eg "112233")
        void DeactivateDevice(const std::string &deviceID);

        /// @brief Re-activate a previously deactivated device: clears is_deleted, restores radio monitoring
        /// @param deviceID device ID (6 characters as hex representation of the 3 bytes, eg "112233")
        void ReactivateDevice(const std::string &deviceID);

        /// @brief Permanently delete a device — only allowed when already deactivated (is_deleted==true)
        /// @param deviceID device ID (6 characters as hex representation of the 3 bytes, eg "112233")
        /// @return true on success, false if device is still active (must deactivate first)
        bool DeleteDevice(const std::string &deviceID);

        /// @brief Declare a remote attached to a device. When the remote is used, device status will be monitored.
        /// @param remoteID Remote ID (6 characters as hex representation of the 3 bytes, eg "112233")
        /// @param deviceID Device ID (6 characters as hex representation of the 3 bytes, eg "112233")
        /// @return true if success, false if failed (unknown device ID, deleted device, ...)
        bool LinkRemoteToDevice(const std::string &remoteID, const std::string &deviceID);

        /// @brief Remove IO remote
        /// @param remoteID remote ID (6 characters as hex representation of the 3 bytes, eg "112233")
        void RemoveIoRemote(const std::string &remoteID);

        /// @brief Start remote capture window — next frame from an unregistered sender triggers a broadcast
        void StartRemoteCapture();

        /// @brief Cancel an active remote capture window
        void StopRemoteCapture();

        /// @brief Returns true if a remote capture window is currently open
        bool IsCaptureActive() const;

        /// @brief Set transit time for a device (persists to NVS)
        /// @param deviceID Device ID
        /// @param transit_time_ms Transit time in milliseconds (0 = uncalibrated)
        /// @return true on success
        bool SetTransitTime(const std::string &deviceID, uint32_t transit_time_ms);

        /// @brief Set quiet mode for a device (persists to storage)
        /// @param deviceID Device ID
        /// @param quiet true for slower, quieter motor operation
        /// @return true on success
        bool SetQuiet(const std::string &deviceID, bool quiet);

        /// @brief Schedule a confirmation poll for a device after its estimated stop time
        /// @param deviceID Device ID
        /// @param transit_time_ms Transit time (0 = use 60 s fallback)
        /// @param distance_fraction Fraction of full range being traveled (0.0-1.0)
        void ScheduleConfirmationPoll(const std::string &deviceID, uint32_t transit_time_ms, float distance_fraction);

        /// @brief Start passive key sniffing — captures the IO system key from the next pairing handshake
        void StartKeySniff();

        /// @brief Stop passive key sniffing
        void StopKeySniff();

        /// @brief Returns true if key sniffing is currently active
        bool IsKeySniffActive() const;

        /// @brief Returns the last captured key as a 32-char hex string, or empty if none yet
        std::string GetSniffedKey() const;

        /// @brief Retrieve current configuration about passive / active mode
        /// @return true if currently in passive mode
        bool isIoPassive() { return mIoPassive; }

        /// @brief Returns true if MQTT broker is currently connected
        bool GetMqttConnected() const;

        /// @brief Returns current MQTT status as a string: disabled/connecting/connected/disconnected/error
        const char *GetMqttStatusString() const;

        /// @brief Start MQTT client immediately (if enabled and not already started)
        void TriggerMqttStart();

        /// @brief Restart MQTT client with current NVS config — safe to call when already running
        void TriggerMqttRestart();

    private:
        bool mIoPassive = false; // current configuration, initialized at boot

        /// @brief Load devices and remotes from flash storage, register them in IoHomeControl
        void LoadIoDevicesFromStorage();

        /// @brief Initialize Io objects members (mSX1276Radio, mIoHome)
        void InitializeIo();

        /// @brief Initialize MQTT objects members (mMqttHelper)
        void InitializeMqtt();
    };

}