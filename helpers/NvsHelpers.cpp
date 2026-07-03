#include "NvsHelpers.hpp"

#include "nvs_flash.h"
#include "nvs.h"
#include "nvs_handle.hpp"

#include "esp_log.h"

static const char *TAG = "helpers";

namespace Helpers
{
    esp_err_t NvsHelpers::InitNvs()
    {
        esp_err_t err = nvs_flash_init();
        if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
        {
            ESP_ERROR_CHECK(nvs_flash_erase());
            err = nvs_flash_init();
        }
        ESP_ERROR_CHECK(err);
        return err;
    }

    static std::unique_ptr<nvs::NVSHandle> open_handle(const std::string &name_space, esp_err_t &err)
    {
        auto h = nvs::open_nvs_handle(name_space.c_str(), NVS_READWRITE, &err);
        if (err != ESP_OK)
            ESP_LOGE(TAG, "Error (%s) opening NVS handle for namespace %s!\n", esp_err_to_name(err), name_space.c_str());
        return h;
    }

    template <typename T>
    esp_err_t NvsHelpers::GetValue(const std::string &name_space, const std::string &key, T &value)
    {
        esp_err_t err = ESP_OK;
        auto h = open_handle(name_space, err);
        if (err != ESP_OK) return err;
        err = h->get_item(key.c_str(), value);
        if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND)
            ESP_LOGE(TAG, "Error (%s) reading key %s in namespace %s!\n", esp_err_to_name(err), key.c_str(), name_space.c_str());
        return err;
    }

    // Explicit instanciation for authorized types
    template esp_err_t NvsHelpers::GetValue<uint8_t>(const std::string &name_space, const std::string &key, uint8_t &value);
    template esp_err_t NvsHelpers::GetValue<int8_t>(const std::string &name_space, const std::string &key, int8_t &value);
    template esp_err_t NvsHelpers::GetValue<uint16_t>(const std::string &name_space, const std::string &key, uint16_t &value);
    template esp_err_t NvsHelpers::GetValue<int16_t>(const std::string &name_space, const std::string &key, int16_t &value);
    template esp_err_t NvsHelpers::GetValue<uint32_t>(const std::string &name_space, const std::string &key, uint32_t &value);
    template esp_err_t NvsHelpers::GetValue<int32_t>(const std::string &name_space, const std::string &key, int32_t &value);
    template esp_err_t NvsHelpers::GetValue<uint64_t>(const std::string &name_space, const std::string &key, uint64_t &value);
    template esp_err_t NvsHelpers::GetValue<int64_t>(const std::string &name_space, const std::string &key, int64_t &value);

    template <typename T>
    esp_err_t NvsHelpers::SetValue(const std::string &name_space, const std::string &key, const T &value)
    {
        esp_err_t err = ESP_OK;
        auto h = open_handle(name_space, err);
        if (err != ESP_OK) return err;
        err = h->set_item(key.c_str(), value);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Error (%s) writing key %s in namespace %s!\n", esp_err_to_name(err), key.c_str(), name_space.c_str());
            return err;
        }
        err = h->commit();
        if (err != ESP_OK)
            ESP_LOGE(TAG, "Error (%s) committing key %s in namespace %s!\n", esp_err_to_name(err), key.c_str(), name_space.c_str());
        return err;
    }

    // Explicit instanciation for authorized types
    template esp_err_t NvsHelpers::SetValue<uint8_t>(const std::string &name_space, const std::string &key, const uint8_t &value);
    template esp_err_t NvsHelpers::SetValue<int8_t>(const std::string &name_space, const std::string &key, const int8_t &value);
    template esp_err_t NvsHelpers::SetValue<uint16_t>(const std::string &name_space, const std::string &key, const uint16_t &value);
    template esp_err_t NvsHelpers::SetValue<int16_t>(const std::string &name_space, const std::string &key, const int16_t &value);
    template esp_err_t NvsHelpers::SetValue<uint32_t>(const std::string &name_space, const std::string &key, const uint32_t &value);
    template esp_err_t NvsHelpers::SetValue<int32_t>(const std::string &name_space, const std::string &key, const int32_t &value);
    template esp_err_t NvsHelpers::SetValue<uint64_t>(const std::string &name_space, const std::string &key, const uint64_t &value);
    template esp_err_t NvsHelpers::SetValue<int64_t>(const std::string &name_space, const std::string &key, const int64_t &value);

    esp_err_t NvsHelpers::DeleteValue(const std::string &name_space, const std::string &key)
    {
        esp_err_t err = ESP_OK;
        auto h = open_handle(name_space, err);
        if (err != ESP_OK) return err;
        nvs_type_t type;
        if (h->find_key(key.c_str(), type) != ESP_OK) return ESP_OK; // Key doesn't exist
        err = h->erase_item(key.c_str());
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Error (%s) deleting key %s in namespace %s!\n", esp_err_to_name(err), key.c_str(), name_space.c_str());
            return err;
        }
        err = h->commit();
        if (err != ESP_OK)
            ESP_LOGE(TAG, "Error (%s) committing delete of key %s in namespace %s!\n", esp_err_to_name(err), key.c_str(), name_space.c_str());
        return err;
    }

    esp_err_t NvsHelpers::GetString(const std::string &name_space, const std::string &key, std::string &value)
    {
        esp_err_t err = ESP_OK;
        auto h = open_handle(name_space, err);
        if (err != ESP_OK) return err;
        size_t paramSize;
        err = h->get_item_size(nvs::ItemType::SZ, key.c_str(), paramSize);
        if (err == ESP_ERR_NVS_NOT_FOUND) return err;
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Error (%s) reading size of key %s in namespace %s!\n", esp_err_to_name(err), key.c_str(), name_space.c_str());
            return err;
        }
        char *tmp = new char[paramSize];
        err = h->get_string(key.c_str(), tmp, paramSize);
        if (err == ESP_OK)
            value.assign(tmp);
        else
            ESP_LOGE(TAG, "Error (%s) reading key %s in namespace %s!\n", esp_err_to_name(err), key.c_str(), name_space.c_str());
        delete[] tmp;
        return err;
    }

    esp_err_t NvsHelpers::SetString(const std::string &name_space, const std::string &key, const std::string &value)
    {
        esp_err_t err = ESP_OK;
        auto h = open_handle(name_space, err);
        if (err != ESP_OK) return err;
        err = h->set_string(key.c_str(), value.c_str());
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Error (%s) writing key %s in namespace %s!\n", esp_err_to_name(err), key.c_str(), name_space.c_str());
            return err;
        }
        err = h->commit();
        if (err != ESP_OK)
            ESP_LOGE(TAG, "Error (%s) committing key %s in namespace %s!\n", esp_err_to_name(err), key.c_str(), name_space.c_str());
        return err;
    }
}
