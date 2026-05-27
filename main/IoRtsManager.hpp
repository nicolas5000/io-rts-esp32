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

        /// @brief Retrieve current configuration about passive / active mode
        /// @return true if currently in passive mode
        bool isIoPassive() { return mIoPassive; }

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