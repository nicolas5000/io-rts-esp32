#include "DeviceStorage.hpp"

#include "esp_littlefs.h"
#include "cJSON.h"
#include "esp_log.h"

#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include <format>

static const char *TAG = "devStorage";

static constexpr const char *STORAGE_BASE_PATH = "/devices";
static constexpr const char *STORAGE_PARTITION = "devices";
static constexpr const char *STORAGE_DEVICE_SUFFIX = ".json";
static constexpr const char *STORAGE_IO_DEVICE_PREFIX = "io_";
static constexpr size_t IO_DEVICE_FILENAME_LENGTH = 14; // io_XXXXXX.json

namespace Helpers
{
    esp_err_t DeviceStorage::Init()
    {
        esp_vfs_littlefs_conf_t conf = {};
        conf.base_path = STORAGE_BASE_PATH;
        conf.partition_label = STORAGE_PARTITION;
        conf.format_if_mount_failed = true;
        conf.dont_mount = false;

        esp_err_t err = esp_vfs_littlefs_register(&conf);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to mount LittleFS partition '%s' (%s)", STORAGE_PARTITION, esp_err_to_name(err));
            return err;
        }

        size_t total = 0, used = 0;
        err = esp_littlefs_info(STORAGE_PARTITION, &total, &used);
        if (err == ESP_OK)
        {
            ESP_LOGI(TAG, "LittleFS partition '%s' mounted: total=%u, used=%u", STORAGE_PARTITION, total, used);
        }

        return ESP_OK;
    }

    std::string DeviceStorage::GetIoDeviceFilePath(const std::string &deviceID)
    {
        return std::format("{}/{}{}.json", STORAGE_BASE_PATH, STORAGE_IO_DEVICE_PREFIX, deviceID);
    }

    esp_err_t DeviceStorage::WriteIoDeviceFile(const std::string &filePath, const std::string &deviceID, const StoredIoDevice &storedDevice)
    {
        cJSON *root = cJSON_CreateObject();
        if (root == nullptr)
        {
            ESP_LOGE(TAG, "Failed to create JSON object for %s", deviceID.c_str());
            return ESP_ERR_NO_MEM;
        }

        const iohome::IoDevice &dev = storedDevice.device;

        // Device ID (for reference, same as filename)
        cJSON_AddStringToObject(root, "id", deviceID.c_str());

        // Device name
        cJSON_AddStringToObject(root, "name", dev.info.name);

        // Node ID as hex string (redundant with filename, but useful for integrity)
        std::string nodeIdHex;
        for (int i = 0; i < iohome::NODE_ID_SIZE; i++)
            nodeIdHex += std::format("{:02X}", dev.info.node_id[i]);
        cJSON_AddStringToObject(root, "node_id", nodeIdHex.c_str());

        // Device type and subtype
        cJSON_AddNumberToObject(root, "device_type", static_cast<uint8_t>(dev.info.device_type));
        cJSON_AddNumberToObject(root, "device_subtype", dev.info.device_subtype);

        // Manufacturer
        cJSON_AddNumberToObject(root, "manufacturer", static_cast<uint8_t>(dev.info.manufacturer));

        // Info strings
        cJSON_AddStringToObject(root, "info1", dev.info.info1);
        cJSON_AddStringToObject(root, "info2", dev.info.info2);

        // Flags
        cJSON_AddBoolToObject(root, "open_close_inverted", dev.info.is_openclose_inverted);
        cJSON_AddBoolToObject(root, "is_low_power", dev.info.is_low_power);

        // Linked remotes
        cJSON *remotes = cJSON_AddArrayToObject(root, "remotes");
        if (remotes != nullptr)
        {
            for (const std::string &remoteID : storedDevice.linked_remotes)
            {
                cJSON_AddItemToArray(remotes, cJSON_CreateString(remoteID.c_str()));
            }
        }

        // Write to file
        const char *jsonStr = cJSON_Print(root);
        esp_err_t err = ESP_OK;
        if (jsonStr == nullptr)
        {
            ESP_LOGE(TAG, "Failed to serialize JSON for %s", deviceID.c_str());
            err = ESP_ERR_NO_MEM;
        }
        else
        {
            FILE *f = fopen(filePath.c_str(), "w");
            if (f == nullptr)
            {
                ESP_LOGE(TAG, "Failed to open file for writing: %s", filePath.c_str());
                err = ESP_FAIL;
            }
            else
            {
                fprintf(f, "%s", jsonStr);
                fclose(f);
                ESP_LOGI(TAG, "Saved device %s", deviceID.c_str());
            }
            cJSON_free((void *)jsonStr);
        }

        cJSON_Delete(root);
        return err;
    }

    esp_err_t DeviceStorage::ReadIoDeviceFile(const std::string &filePath, std::string &deviceID, StoredIoDevice &storedDevice)
    {
        FILE *f = fopen(filePath.c_str(), "r");
        if (f == nullptr)
        {
            ESP_LOGD(TAG, "Device file not found: %s", filePath.c_str());
            return ESP_ERR_NOT_FOUND;
        }

        // Get file size
        fseek(f, 0, SEEK_END);
        long fileSize = ftell(f);
        fseek(f, 0, SEEK_SET);

        if (fileSize <= 0 || fileSize > 4096) // sanity check
        {
            ESP_LOGE(TAG, "Invalid file size (%ld) for %s", fileSize, filePath.c_str());
            fclose(f);
            return ESP_ERR_INVALID_SIZE;
        }

        char *buf = new char[fileSize + 1];
        size_t bytesRead = fread(buf, 1, fileSize, f);
        fclose(f);
        buf[bytesRead] = '\0';

        cJSON *root = cJSON_Parse(buf);
        delete[] buf;

        if (root == nullptr)
        {
            ESP_LOGE(TAG, "Failed to parse JSON from %s", filePath.c_str());
            return ESP_ERR_INVALID_ARG;
        }

        // Parse device ID
        cJSON *idItem = cJSON_GetObjectItem(root, "id");
        if (cJSON_IsString(idItem))
            deviceID = idItem->valuestring;

        iohome::IoDevice &dev = storedDevice.device;
        memset(&dev, 0, sizeof(iohome::IoDevice));
        dev.position = iohome::UNKNOWN_POSITION;
        dev.target = iohome::UNKNOWN_POSITION;
        dev.tilt = iohome::UNKNOWN_POSITION;

        // Parse name
        cJSON *nameItem = cJSON_GetObjectItem(root, "name");
        if (cJSON_IsString(nameItem))
            strncpy(dev.info.name, nameItem->valuestring, iohome::CMD_PARAM_NAME_MAXSIZE - 1);

        // Parse node_id
        cJSON *nodeIdItem = cJSON_GetObjectItem(root, "node_id");
        if (cJSON_IsString(nodeIdItem))
        {
            std::string hex(nodeIdItem->valuestring);
            for (int i = 0; i < iohome::NODE_ID_SIZE && (i * 2 + 1) < (int)hex.length(); i++)
                dev.info.node_id[i] = (uint8_t)strtol(hex.substr(i * 2, 2).c_str(), nullptr, 16);
        }

        // Parse device type and subtype
        cJSON *typeItem = cJSON_GetObjectItem(root, "device_type");
        if (cJSON_IsNumber(typeItem))
            dev.info.device_type = static_cast<iohome::DeviceType>((uint8_t)typeItem->valuedouble);

        cJSON *subtypeItem = cJSON_GetObjectItem(root, "device_subtype");
        if (cJSON_IsNumber(subtypeItem))
            dev.info.device_subtype = (uint8_t)subtypeItem->valuedouble;

        // Parse manufacturer
        cJSON *mfItem = cJSON_GetObjectItem(root, "manufacturer");
        if (cJSON_IsNumber(mfItem))
            dev.info.manufacturer = static_cast<iohome::Manufacturer>((uint8_t)mfItem->valuedouble);

        // Parse info strings
        cJSON *info1Item = cJSON_GetObjectItem(root, "info1");
        if (cJSON_IsString(info1Item))
            strncpy(dev.info.info1, info1Item->valuestring, iohome::CMD_PARAM_INFO1_MAXSIZE - 1);

        cJSON *info2Item = cJSON_GetObjectItem(root, "info2");
        if (cJSON_IsString(info2Item))
            strncpy(dev.info.info2, info2Item->valuestring, iohome::CMD_PARAM_INFO2_MAXSIZE - 1);
        
        // Parse flags
        cJSON *openCloseInvertedItem = cJSON_GetObjectItem(root, "open_close_inverted");
        if (cJSON_IsBool(openCloseInvertedItem))
            dev.info.is_openclose_inverted = cJSON_IsTrue(openCloseInvertedItem);

        cJSON *isLowPowerItem = cJSON_GetObjectItem(root, "is_low_power");
        dev.info.is_low_power = cJSON_IsBool(isLowPowerItem) ? cJSON_IsTrue(isLowPowerItem) : true;

        // Initialize runtime fields
        dev.is_stopped = true;
        dev.is_deleted = false;
        dev.position = iohome::UNKNOWN_POSITION;
        dev.target = iohome::UNKNOWN_POSITION;
        dev.last_status_timestamp = 0;
        dev.next_status_update_timestamp = 0;

        // Parse linked remotes
        storedDevice.linked_remotes.clear();
        cJSON *remotesArray = cJSON_GetObjectItem(root, "remotes");
        if (cJSON_IsArray(remotesArray))
        {
            cJSON *remoteItem = nullptr;
            cJSON_ArrayForEach(remoteItem, remotesArray)
            {
                if (cJSON_IsString(remoteItem))
                    storedDevice.linked_remotes.push_back(remoteItem->valuestring);
            }
        }

        cJSON_Delete(root);
        return ESP_OK;
    }

    esp_err_t DeviceStorage::LoadIoDevice(const std::string &deviceID, StoredIoDevice &storedDevice)
    {
        std::string filePath = GetIoDeviceFilePath(deviceID);
        std::string readDeviceID;
        return ReadIoDeviceFile(filePath, readDeviceID, storedDevice);
    }

    esp_err_t DeviceStorage::LoadAllIoDevices(std::map<std::string, StoredIoDevice> &devices)
    {
        devices.clear();

        DIR *dir = opendir(STORAGE_BASE_PATH);
        if (dir == nullptr)
        {
            ESP_LOGW(TAG, "No device storage directory found, starting fresh");
            return ESP_OK;
        }

        struct dirent *entry;
        while ((entry = readdir(dir)) != nullptr)
        {
            std::string filename(entry->d_name);
            // Only process io_XXXXXX.json files
            if (filename.length() != IO_DEVICE_FILENAME_LENGTH
                || filename.substr(0, 3) != STORAGE_IO_DEVICE_PREFIX
                || filename.substr(filename.length() - 5) != STORAGE_DEVICE_SUFFIX)
                continue;

            std::string filePath = std::format("{}/{}", STORAGE_BASE_PATH, filename);
            std::string deviceID;
            StoredIoDevice storedDevice;

            esp_err_t err = ReadIoDeviceFile(filePath, deviceID, storedDevice);
            if (err == ESP_OK && !deviceID.empty())
            {
                devices.insert({deviceID, storedDevice});
                ESP_LOGI(TAG, "Loaded device %s (%s)", deviceID.c_str(), storedDevice.device.info.name);
            }
            else
            {
                ESP_LOGW(TAG, "Skipping invalid device file: %s", filename.c_str());
            }
        }

        closedir(dir);
        ESP_LOGI(TAG, "Loaded %u device(s) from storage", devices.size());
        return ESP_OK;
    }

    esp_err_t DeviceStorage::SaveIoDevice(const std::string &deviceID, const StoredIoDevice &device)
    {
        std::string filePath = GetIoDeviceFilePath(deviceID);
        return WriteIoDeviceFile(filePath, deviceID, device);
    }

    esp_err_t DeviceStorage::RemoveIoDevice(const std::string &deviceID)
    {
        std::string filePath = GetIoDeviceFilePath(deviceID);
        if (remove(filePath.c_str()) != 0)
        {
            ESP_LOGW(TAG, "Could not remove device file %s (may not exist)", deviceID.c_str());
            return ESP_ERR_NOT_FOUND;
        }
        ESP_LOGI(TAG, "Removed device %s from storage", deviceID.c_str());
        return ESP_OK;
    }

    esp_err_t DeviceStorage::AddRemoteToIoDevice(const std::string &remoteID, const std::string &deviceID)
    {
        std::string filePath = GetIoDeviceFilePath(deviceID);
        std::string readDeviceID;
        StoredIoDevice storedDevice;

        esp_err_t err = ReadIoDeviceFile(filePath, readDeviceID, storedDevice);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Cannot add remote %s: device %s not found in storage", remoteID.c_str(), deviceID.c_str());
            return err;
        }

        // Check if remote already linked
        for (const std::string &existing : storedDevice.linked_remotes)
        {
            if (existing == remoteID)
                return ESP_OK; // already linked
        }

        storedDevice.linked_remotes.push_back(remoteID);
        return WriteIoDeviceFile(filePath, deviceID, storedDevice);
    }

    esp_err_t DeviceStorage::RemoveRemoteFromIoDevices(const std::string &remoteID)
    {
        // Iterate over all device files and remove this remote from each
        DIR *dir = opendir(STORAGE_BASE_PATH);
        if (dir == nullptr)
            return ESP_OK;

        struct dirent *entry;
        while ((entry = readdir(dir)) != nullptr)
        {
            std::string filename(entry->d_name);
            // Only process io_XXXXXX.json files
            if (filename.length() != IO_DEVICE_FILENAME_LENGTH
                || filename.substr(0, 3) != STORAGE_IO_DEVICE_PREFIX
                || filename.substr(filename.length() - 5) != STORAGE_DEVICE_SUFFIX)
                continue;

            std::string filePath = std::format("{}/{}", STORAGE_BASE_PATH, filename);
            std::string deviceID;
            StoredIoDevice storedDevice;

            if (ReadIoDeviceFile(filePath, deviceID, storedDevice) == ESP_OK)
            {
                auto it = std::find(storedDevice.linked_remotes.begin(), storedDevice.linked_remotes.end(), remoteID);
                if (it != storedDevice.linked_remotes.end())
                {
                    storedDevice.linked_remotes.erase(it);
                    WriteIoDeviceFile(filePath, deviceID, storedDevice);
                    ESP_LOGI(TAG, "Removed remote %s from device %s", remoteID.c_str(), deviceID.c_str());
                }
            }
        }

        closedir(dir);
        return ESP_OK;
    }
}
