#include "DeviceStorage.hpp"

#include "esp_littlefs.h"
#include "cJSON.h"
#include "esp_log.h"

#include <algorithm>
#include <cstring>
#include <format>

static const char *TAG = "devStorage";

static constexpr const char *STORAGE_BASE_PATH  = "/devices";
static constexpr const char *STORAGE_PARTITION  = "devices";
static constexpr const char *STORAGE_FILE       = "/devices/devices.json";
static constexpr size_t      STORAGE_MAX_SIZE   = 65536; // 64 KB — fits 250+ devices

namespace Helpers
{
    esp_err_t DeviceStorage::Init()
    {
        esp_vfs_littlefs_conf_t conf = {};
        conf.base_path             = STORAGE_BASE_PATH;
        conf.partition_label       = STORAGE_PARTITION;
        conf.format_if_mount_failed = true;
        conf.dont_mount            = false;

        esp_err_t err = esp_vfs_littlefs_register(&conf);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to mount LittleFS partition '%s' (%s)", STORAGE_PARTITION, esp_err_to_name(err));
            return err;
        }

        size_t total = 0, used = 0;
        if (esp_littlefs_info(STORAGE_PARTITION, &total, &used) == ESP_OK)
            ESP_LOGI(TAG, "LittleFS partition '%s' mounted: total=%u, used=%u", STORAGE_PARTITION, total, used);

        return ESP_OK;
    }

    // ─── Private helpers ──────────────────────────────────────────────────────

    cJSON *DeviceStorage::DeviceToJson(const std::string &deviceID, const StoredIoDevice &sd)
    {
        const iohome::IoDevice &dev = sd.device;

        cJSON *obj = cJSON_CreateObject();
        if (!obj) return nullptr;

        cJSON_AddStringToObject(obj, "id",   deviceID.c_str());
        cJSON_AddStringToObject(obj, "name", dev.info.name);

        std::string nodeIdHex;
        for (int i = 0; i < iohome::NODE_ID_SIZE; i++)
            nodeIdHex += std::format("{:02X}", dev.info.node_id[i]);
        cJSON_AddStringToObject(obj, "node_id", nodeIdHex.c_str());

        cJSON_AddNumberToObject(obj, "device_type",    static_cast<uint8_t>(dev.info.device_type));
        cJSON_AddNumberToObject(obj, "device_subtype", dev.info.device_subtype);
        cJSON_AddNumberToObject(obj, "manufacturer",   static_cast<uint8_t>(dev.info.manufacturer));
        cJSON_AddStringToObject(obj, "info1", dev.info.info1);
        cJSON_AddStringToObject(obj, "info2", dev.info.info2);
        cJSON_AddBoolToObject(obj, "open_close_inverted", dev.info.is_openclose_inverted);
        cJSON_AddBoolToObject(obj, "is_low_power",        dev.info.is_low_power);
        cJSON_AddNumberToObject(obj, "transit_ms", sd.transit_time_ms);

        cJSON *remotes = cJSON_AddArrayToObject(obj, "remotes");
        if (remotes)
        {
            for (const std::string &remoteID : sd.linked_remotes)
                cJSON_AddItemToArray(remotes, cJSON_CreateString(remoteID.c_str()));
        }

        return obj;
    }

    bool DeviceStorage::JsonToDevice(const cJSON *obj, std::string &deviceID, StoredIoDevice &sd)
    {
        if (!cJSON_IsObject(obj)) return false;

        cJSON *idItem = cJSON_GetObjectItem(obj, "id");
        if (!cJSON_IsString(idItem) || !idItem->valuestring[0]) return false;
        deviceID = idItem->valuestring;

        iohome::IoDevice &dev = sd.device;
        memset(&dev, 0, sizeof(iohome::IoDevice));
        dev.position              = iohome::UNKNOWN_POSITION;
        dev.target                = iohome::UNKNOWN_POSITION;
        dev.tilt                  = iohome::UNKNOWN_POSITION;
        dev.is_stopped            = true;
        dev.is_deleted            = false;
        dev.last_status_timestamp = 0;
        dev.next_status_update_timestamp = 0;

        cJSON *nameItem = cJSON_GetObjectItem(obj, "name");
        if (cJSON_IsString(nameItem))
            strncpy(dev.info.name, nameItem->valuestring, iohome::CMD_PARAM_NAME_MAXSIZE - 1);

        cJSON *nodeIdItem = cJSON_GetObjectItem(obj, "node_id");
        if (cJSON_IsString(nodeIdItem))
        {
            std::string hex(nodeIdItem->valuestring);
            for (int i = 0; i < iohome::NODE_ID_SIZE && (i * 2 + 1) < (int)hex.length(); i++)
                dev.info.node_id[i] = (uint8_t)strtol(hex.substr(i * 2, 2).c_str(), nullptr, 16);
        }

        cJSON *typeItem = cJSON_GetObjectItem(obj, "device_type");
        if (cJSON_IsNumber(typeItem))
            dev.info.device_type = static_cast<iohome::DeviceType>((uint8_t)typeItem->valuedouble);

        cJSON *subtypeItem = cJSON_GetObjectItem(obj, "device_subtype");
        if (cJSON_IsNumber(subtypeItem))
            dev.info.device_subtype = (uint8_t)subtypeItem->valuedouble;

        cJSON *mfItem = cJSON_GetObjectItem(obj, "manufacturer");
        if (cJSON_IsNumber(mfItem))
            dev.info.manufacturer = static_cast<iohome::Manufacturer>((uint8_t)mfItem->valuedouble);

        cJSON *info1Item = cJSON_GetObjectItem(obj, "info1");
        if (cJSON_IsString(info1Item))
            strncpy(dev.info.info1, info1Item->valuestring, iohome::CMD_PARAM_INFO1_MAXSIZE - 1);

        cJSON *info2Item = cJSON_GetObjectItem(obj, "info2");
        if (cJSON_IsString(info2Item))
            strncpy(dev.info.info2, info2Item->valuestring, iohome::CMD_PARAM_INFO2_MAXSIZE - 1);

        cJSON *openCloseItem = cJSON_GetObjectItem(obj, "open_close_inverted");
        if (cJSON_IsBool(openCloseItem))
            dev.info.is_openclose_inverted = cJSON_IsTrue(openCloseItem);

        cJSON *lowPowerItem = cJSON_GetObjectItem(obj, "is_low_power");
        dev.info.is_low_power = cJSON_IsBool(lowPowerItem) ? cJSON_IsTrue(lowPowerItem) : true;

        cJSON *transitItem = cJSON_GetObjectItem(obj, "transit_ms");
        sd.transit_time_ms = cJSON_IsNumber(transitItem) ? (uint32_t)transitItem->valuedouble : 0;

        sd.linked_remotes.clear();
        cJSON *remotesArr = cJSON_GetObjectItem(obj, "remotes");
        if (cJSON_IsArray(remotesArr))
        {
            cJSON *r = nullptr;
            cJSON_ArrayForEach(r, remotesArr)
                if (cJSON_IsString(r)) sd.linked_remotes.push_back(r->valuestring);
        }

        return true;
    }

    esp_err_t DeviceStorage::ReadAllDevices(std::map<std::string, StoredIoDevice> &devices)
    {
        devices.clear();

        FILE *f = fopen(STORAGE_FILE, "r");
        if (!f)
        {
            ESP_LOGD(TAG, "devices.json not found — starting fresh");
            return ESP_OK;
        }

        fseek(f, 0, SEEK_END);
        long fileSize = ftell(f);
        fseek(f, 0, SEEK_SET);

        if (fileSize <= 0 || fileSize > (long)STORAGE_MAX_SIZE)
        {
            ESP_LOGE(TAG, "devices.json has invalid size: %ld", fileSize);
            fclose(f);
            return ESP_ERR_INVALID_SIZE;
        }

        char *buf = new char[fileSize + 1];
        size_t bytesRead = fread(buf, 1, fileSize, f);
        fclose(f);
        buf[bytesRead] = '\0';

        cJSON *arr = cJSON_Parse(buf);
        delete[] buf;

        if (!cJSON_IsArray(arr))
        {
            ESP_LOGE(TAG, "devices.json is not a JSON array");
            cJSON_Delete(arr);
            return ESP_ERR_INVALID_ARG;
        }

        cJSON *item = nullptr;
        cJSON_ArrayForEach(item, arr)
        {
            std::string deviceID;
            StoredIoDevice sd;
            if (JsonToDevice(item, deviceID, sd))
            {
                devices.insert({deviceID, sd});
                ESP_LOGI(TAG, "Loaded device %s (%s)", deviceID.c_str(), sd.device.info.name);
            }
            else
            {
                ESP_LOGW(TAG, "Skipping invalid device entry in devices.json");
            }
        }

        cJSON_Delete(arr);
        ESP_LOGI(TAG, "Loaded %u device(s) from storage", devices.size());
        return ESP_OK;
    }

    esp_err_t DeviceStorage::WriteAllDevices(const std::map<std::string, StoredIoDevice> &devices)
    {
        cJSON *arr = cJSON_CreateArray();
        if (!arr) return ESP_ERR_NO_MEM;

        for (const auto &[deviceID, sd] : devices)
        {
            cJSON *obj = DeviceToJson(deviceID, sd);
            if (obj) cJSON_AddItemToArray(arr, obj);
        }

        const char *jsonStr = cJSON_PrintUnformatted(arr);
        cJSON_Delete(arr);

        if (!jsonStr)
        {
            ESP_LOGE(TAG, "Failed to serialize devices JSON");
            return ESP_ERR_NO_MEM;
        }

        FILE *f = fopen(STORAGE_FILE, "w");
        if (!f)
        {
            ESP_LOGE(TAG, "Failed to open devices.json for writing");
            cJSON_free((void *)jsonStr);
            return ESP_FAIL;
        }

        fprintf(f, "%s", jsonStr);
        fclose(f);
        cJSON_free((void *)jsonStr);

        ESP_LOGI(TAG, "Saved %u device(s) to storage", devices.size());
        return ESP_OK;
    }

    // ─── Public API ────────────────────────────────────────────────────────────

    esp_err_t DeviceStorage::LoadAllIoDevices(std::map<std::string, StoredIoDevice> &devices)
    {
        return ReadAllDevices(devices);
    }

    esp_err_t DeviceStorage::LoadIoDevice(const std::string &deviceID, StoredIoDevice &storedDevice)
    {
        std::map<std::string, StoredIoDevice> all;
        esp_err_t err = ReadAllDevices(all);
        if (err != ESP_OK) return err;

        auto it = all.find(deviceID);
        if (it == all.end()) return ESP_ERR_NOT_FOUND;
        storedDevice = it->second;
        return ESP_OK;
    }

    esp_err_t DeviceStorage::SaveIoDevice(const std::string &deviceID, const StoredIoDevice &device)
    {
        std::map<std::string, StoredIoDevice> all;
        ReadAllDevices(all); // ignore error — start fresh if no file yet
        all[deviceID] = device;
        return WriteAllDevices(all);
    }

    esp_err_t DeviceStorage::SaveAllIoDevices(const std::map<std::string, StoredIoDevice> &devices)
    {
        return WriteAllDevices(devices);
    }

    esp_err_t DeviceStorage::RemoveIoDevice(const std::string &deviceID)
    {
        std::map<std::string, StoredIoDevice> all;
        esp_err_t err = ReadAllDevices(all);
        if (err != ESP_OK) return err;

        if (all.erase(deviceID) == 0)
        {
            ESP_LOGW(TAG, "RemoveIoDevice: device %s not found", deviceID.c_str());
            return ESP_ERR_NOT_FOUND;
        }

        return WriteAllDevices(all);
    }

    esp_err_t DeviceStorage::AddRemoteToIoDevice(const std::string &remoteID, const std::string &deviceID)
    {
        std::map<std::string, StoredIoDevice> all;
        esp_err_t err = ReadAllDevices(all);
        if (err != ESP_OK) return err;

        auto it = all.find(deviceID);
        if (it == all.end())
        {
            ESP_LOGE(TAG, "AddRemoteToIoDevice: device %s not found", deviceID.c_str());
            return ESP_ERR_NOT_FOUND;
        }

        auto &remotes = it->second.linked_remotes;
        if (std::find(remotes.begin(), remotes.end(), remoteID) != remotes.end())
            return ESP_OK; // already linked

        remotes.push_back(remoteID);
        return WriteAllDevices(all);
    }

    esp_err_t DeviceStorage::RemoveRemoteFromIoDevices(const std::string &remoteID)
    {
        std::map<std::string, StoredIoDevice> all;
        esp_err_t err = ReadAllDevices(all);
        if (err != ESP_OK) return ESP_OK;

        bool changed = false;
        for (auto &[id, sd] : all)
        {
            auto &remotes = sd.linked_remotes;
            auto it = std::find(remotes.begin(), remotes.end(), remoteID);
            if (it != remotes.end())
            {
                remotes.erase(it);
                changed = true;
                ESP_LOGI(TAG, "Removed remote %s from device %s", remoteID.c_str(), id.c_str());
            }
        }

        return changed ? WriteAllDevices(all) : ESP_OK;
    }
}
