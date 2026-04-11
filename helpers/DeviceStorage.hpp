#pragma once

#include <string>
#include <map>
#include <list>

#include "esp_err.h"
#include "iohome_device.hpp"

namespace Helpers
{
    /// @brief Stored device data (static information + linked remotes)
    struct StoredDevice
    {
        iohome::IoDevice device;                // Device information
        std::list<std::string> linked_remotes;  // Remote IDs linked to this device
    };

    class DeviceStorage
    {
    public:
        /// @brief Initialize LittleFS partition for device storage. Must be called before any read/write operation.
        /// @return ESP_OK if no error
        static esp_err_t Init();

        /// @brief Load a single device from flash storage
        /// @param deviceID Device ID (6 characters as hex representation of the 3 bytes, eg "112233")
        /// @param storedDevice Will be filled with device data
        /// @return ESP_OK if no error, ESP_ERR_NOT_FOUND if device doesn't exist
        static esp_err_t LoadDevice(const std::string &deviceID, StoredDevice &storedDevice);

        /// @brief Load all devices from flash storage
        /// @param devices Map that will be filled with all stored devices (deviceID -> StoredDevice)
        /// @return ESP_OK if no error
        static esp_err_t LoadAll(std::map<std::string, StoredDevice> &devices);

        /// @brief Save a device to flash storage (creates or overwrites)
        /// @param deviceID Device ID (6 characters as hex representation of the 3 bytes, eg "112233")
        /// @param device Device data to store
        /// @return ESP_OK if no error
        static esp_err_t SaveDevice(const std::string &deviceID, const StoredDevice &device);

        /// @brief Remove a device file from flash storage
        /// @param deviceID Device ID (6 characters as hex representation of the 3 bytes, eg "112233")
        /// @return ESP_OK if no error, ESP_ERR_NOT_FOUND if device doesn't exist
        static esp_err_t RemoveDevice(const std::string &deviceID);

        /// @brief Add a remote link to an existing stored device
        /// @param remoteID Remote ID (6 characters hex)
        /// @param deviceID Device ID (6 characters hex)
        /// @return ESP_OK if no error
        static esp_err_t AddRemoteToDevice(const std::string &remoteID, const std::string &deviceID);

        /// @brief Remove a remote link from all stored devices
        /// @param remoteID Remote ID (6 characters hex)
        /// @return ESP_OK if no error
        static esp_err_t RemoveRemote(const std::string &remoteID);

    private:
        /// @brief Build the file path for a device
        /// @param deviceID Device ID
        /// @return Full path to the device file (eg "/devices/112233.json")
        static std::string GetDeviceFilePath(const std::string &deviceID);

        /// @brief Serialize a StoredDevice to JSON and write to file
        /// @param filePath Path to file
        /// @param deviceID Device ID
        /// @param storedDevice Device data to serialize
        /// @return ESP_OK if no error
        static esp_err_t WriteDeviceFile(const std::string &filePath, const std::string &deviceID, const StoredDevice &storedDevice);

        /// @brief Read and deserialize a device from a JSON file
        /// @param filePath Path to file
        /// @param deviceID Will be set to the device ID from the file
        /// @param storedDevice Will be filled with deserialized device data
        /// @return ESP_OK if no error
        static esp_err_t ReadDeviceFile(const std::string &filePath, std::string &deviceID, StoredDevice &storedDevice);
    };
}
