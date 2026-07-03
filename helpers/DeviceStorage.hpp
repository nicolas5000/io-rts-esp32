#pragma once

#include <string>
#include <map>
#include <list>

#include "esp_err.h"
#include "cJSON.h"
#include "iohome_device.hpp"

namespace Helpers
{
    /// @brief Stored IO device data (static information + linked remotes)
    struct StoredIoDevice
    {
        iohome::IoDevice device;               // Device information
        std::list<std::string> linked_remotes; // Remote IDs linked to this device
        uint32_t transit_time_ms = 0;          // Time to travel full range (0 = uncalibrated)
        bool quiet = false;                    // Slower, quieter motor operation
    };

    class DeviceStorage
    {
    public:
        /// @brief Initialize LittleFS partition for device storage. Must be called before any read/write operation.
        static esp_err_t Init();

        /// @brief Load all IO devices from flash storage
        static esp_err_t LoadAllIoDevices(std::map<std::string, StoredIoDevice> &devices);

        /// @brief Load a single IO device from flash storage
        static esp_err_t LoadIoDevice(const std::string &deviceID, StoredIoDevice &storedDevice);

        /// @brief Save a single IO device (read-modify-write of devices.json)
        static esp_err_t SaveIoDevice(const std::string &deviceID, const StoredIoDevice &device);

        /// @brief Replace the entire device store in one write — use for bulk operations
        static esp_err_t SaveAllIoDevices(const std::map<std::string, StoredIoDevice> &devices);

        /// @brief Remove an IO device from storage
        static esp_err_t RemoveIoDevice(const std::string &deviceID);

        /// @brief Add a remote link to an existing stored IO device
        static esp_err_t AddRemoteToIoDevice(const std::string &remoteID, const std::string &deviceID);

        /// @brief Remove a remote link from all stored IO devices
        static esp_err_t RemoveRemoteFromIoDevices(const std::string &remoteID);

    private:
        static cJSON *DeviceToJson(const std::string &deviceID, const StoredIoDevice &storedDevice);
        static bool   JsonToDevice(const cJSON *obj, std::string &deviceID, StoredIoDevice &storedDevice);
        static esp_err_t ReadAllDevices(std::map<std::string, StoredIoDevice> &devices);
        static esp_err_t WriteAllDevices(const std::map<std::string, StoredIoDevice> &devices);
    };
}
