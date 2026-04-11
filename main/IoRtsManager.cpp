#include "IoRtsManager.hpp"
#include "HardwareConfig.hpp"
#include "MqttConfig.hpp"
#include "DeviceStorage.hpp"

#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "ioRtsMan";

using namespace Helpers;
using namespace Config;

namespace IoRts
{
    static IoRtsManager *sIoRtsManager; // Pointer to IoRtsManager isntance
    static MqttHelpers *sMqttHelper;    // Pointer to MQTT instance to manage MQTT communication layer

#ifdef CONFIG_ENABLE_IOHOMECONTROL
    static void loggerCallback(esp_log_level_t log_level, const char *tag, std::string log)
    {
        switch (log_level)
        {
        case ESP_LOG_ERROR:
            ESP_LOGE(tag, "%s", log.c_str());
            break;
        case ESP_LOG_INFO:
            ESP_LOGI(tag, "%s", log.c_str());
            break;
        default:
            break;
        }
    }

    static void deviceStatusCallback(const std::string deviceID, const iohome::IoDevice &device)
    {
        ESP_LOGI(TAG, "Callback received device status for %s: %s (0x%02X/0x%02X) / Position %.1f / Target %0.1f / Moving: %s / Deleted: %s",
                 deviceID.c_str(), device.info.name, device.info.device_type, device.info.device_subtype,
                 device.position, device.target,
                 device.is_stopped ? "No" : "Yes", device.is_deleted ? "Yes" : "No");
        if (sIoRtsManager == nullptr)
            return;
        if (!sIoRtsManager->isIoPassive()                                                                 // not passive mode
            && (device.info.device_type != iohome::DeviceType::UNKNOWN) && (strlen(device.info.name) > 0) // we have at least device name and type
            && (!device.is_deleted))                                                                      // device is not deleted
        {
            bool sendDiscovery = false;
            // So we can process device status
            sIoRtsManager->mIoDevicesMutex.lock(); // Take mutex
            if (!sIoRtsManager->mIoDevices.contains(deviceID))
            {
                // We have a new device detected!
                // add device to mIoDevices
                sIoRtsManager->mIoDevices.insert({deviceID, device});
                // ask for discovery message
                sendDiscovery = true;
                // add device to flash storage
                Helpers::StoredDevice storedDevice = {};
                storedDevice.device = device;
                Helpers::DeviceStorage::SaveDevice(deviceID, storedDevice);
            }
            else
            {
                // update mIoDevices!
                auto it = sIoRtsManager->mIoDevices.find(deviceID); // we have something as we found it just before
                if (it->second.is_deleted)
                {
                    // revert 'deleted' status and send MQTT discovery as device has been added again
                    sendDiscovery = true;
                    it->second.is_deleted = false;
                }
                if (strcmp(it->second.info.name, device.info.name) != 0)
                {
                    sendDiscovery = true;
                    memcpy(it->second.info.name, device.info.name, sizeof(device.info.name)); // could be update by "SetName"
                }
                // Update device type/subtype if they were unknown and now available
                if (it->second.info.device_type == iohome::DeviceType::UNKNOWN && device.info.device_type != iohome::DeviceType::UNKNOWN)
                {
                    sendDiscovery = true;
                    it->second.info.device_type = device.info.device_type;
                    it->second.info.device_subtype = device.info.device_subtype;
                    it->second.info.manufacturer = device.info.manufacturer;
                }
                it->second.is_stopped = device.is_stopped;
                it->second.last_status_timestamp = device.last_status_timestamp;
                it->second.next_status_update_timestamp = device.next_status_update_timestamp;
                it->second.position = device.position;
                it->second.target = device.target;
                // Update storage when static device info changed (name, type, ...)
                if (sendDiscovery)
                {
                    // Load existing file to preserve linked remotes, then update device info
                    Helpers::StoredDevice storedDevice = {};
                    storedDevice.device = it->second;
                    Helpers::StoredDevice existing;
                    if (Helpers::DeviceStorage::LoadDevice(deviceID, existing) == ESP_OK)
                        storedDevice.linked_remotes = existing.linked_remotes;
                    Helpers::DeviceStorage::SaveDevice(deviceID, storedDevice);
                }
            }
            sIoRtsManager->mIoDevicesMutex.unlock(); // release mutex as MQTT needs it!
            // send MQTT messages
            if (sMqttHelper != nullptr)
            {
                // send MQTT discovery message
                if (sendDiscovery)
                    sMqttHelper->SendDiscovery();
                // send status update
                sMqttHelper->SendIoDeviceStatus(deviceID);
            }
        }
    }
#endif // ENABLE_IOHOMECONTROL

    IoRtsManager::IoRtsManager()
    {
        sIoRtsManager = this;
        // Initialize device storage (mount LittleFS)
        InitializeStorage();
        // Initialize IO objects
        InitializeIo();
        // Load devices from flash storage
        LoadDevicesFromStorage();
        // Initialize MQTT objects
        InitializeMqtt();
        // Start everything
        if (mIoHome != nullptr)
        {
            mIoHome->StartReceive();
        }
        if (sMqttHelper != nullptr)
        {
            sMqttHelper->StartMqttClient();
        }
    }
    void IoRtsManager::Reboot()
    {
        if (mIoHome != nullptr)
            mIoHome->StopReceive();
        esp_restart();
    }
    void IoRtsManager::RemoveIoDevice(const std::string &deviceID)
    {
        sIoRtsManager->mIoDevicesMutex.lock(); // Take mutex
        auto it = mIoDevices.find(deviceID);
        if (it != mIoDevices.end())
        {
            if (it->second.is_deleted)
            {
                sIoRtsManager->mIoDevicesMutex.unlock(); // release mutex
                return;
            }
            // Remove from Io controller
            if (mIoHome != nullptr)
                mIoHome->DeleteDevice(deviceID);
            // Update mIODevices
            it->second.is_deleted = true;
            // Remove from storage
            Helpers::DeviceStorage::RemoveDevice(deviceID);
            sIoRtsManager->mIoDevicesMutex.unlock(); // release mutex as MQTT needs it!;
            if (sMqttHelper != nullptr)
            {
                // send MQTT discovery message
                sMqttHelper->SendDiscovery();
                // delete retained topics
                sMqttHelper->SendIoDeviceStatus(deviceID);
            }
        }
        else
        {
            sIoRtsManager->mIoDevicesMutex.unlock(); // release mutex
            ESP_LOGE(TAG, "RemoveIoDevice: unknown %s", deviceID.c_str());
        }
    }
    bool IoRtsManager::LinkRemoteToDevice(const std::string &remoteID, const std::string &deviceID)
    {
        bool success = mIoHome->LinkRemoteToDevice(remoteID, deviceID);
        if (success)
        {
            // Add remote to storage
            Helpers::DeviceStorage::AddRemoteToDevice(remoteID, deviceID);
        }
        return success;
    }
    void IoRtsManager::RemoveIoRemote(const std::string &remoteID)
    {
        // Remove from Io controller
        if (mIoHome != nullptr)
            mIoHome->DeleteRemote(remoteID);
        // Remove from storage
        Helpers::DeviceStorage::RemoveRemote(remoteID);
    }
    void IoRtsManager::InitializeStorage()
    {
        esp_err_t err = Helpers::DeviceStorage::Init();
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to initialize device storage (%s)", esp_err_to_name(err));
        }
    }
    void IoRtsManager::LoadDevicesFromStorage()
    {
#ifdef CONFIG_ENABLE_IOHOMECONTROL
        if (mIoHome == nullptr)
            return;

        std::map<std::string, Helpers::StoredDevice> storedDevices;
        esp_err_t err = Helpers::DeviceStorage::LoadAll(storedDevices);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to load devices from storage (%s)", esp_err_to_name(err));
            return;
        }

        for (const auto &[deviceID, storedDevice] : storedDevices)
        {
            // Register device in protocol layer
            mIoHome->AddDevice(deviceID);
            // Add to our local map
            mIoDevicesMutex.lock();
            mIoDevices.insert({deviceID, storedDevice.device});
            mIoDevicesMutex.unlock();
            // Restore remote links
            for (const std::string &remoteID : storedDevice.linked_remotes)
            {
                mIoHome->LinkRemoteToDevice(remoteID, deviceID);
            }
            ESP_LOGI(TAG, "Restored device %s (%s) with %u remote(s)",
                     deviceID.c_str(), storedDevice.device.info.name, storedDevice.linked_remotes.size());
        }
#endif // ENABLE_IOHOMECONTROL
    }
    void IoRtsManager::InitializeIo()
    {
// Initialize IO-HOMECONTROL
#ifdef CONFIG_ENABLE_IOHOMECONTROL
#ifdef CONFIG_IOHOMECONTROL_LOGGING_ENABLED
        bool logging = true;
#else
        bool logging = false;
#endif
#ifdef CONFIG_IOHOMECONTROL_PASSIVE_MODE
        mIoPassive = true;
#else
        mIoPassive = false;
#endif
        mSX1276Radio = new RadioLinks::RadioSX1276(Config::GetSX1276SpiHost(),
                                                   CONFIG_IOHOMECONTROL_SX1276_SPI_CS, CONFIG_IOHOMECONTROL_SX1276_RST,
                                                   CONFIG_IOHOMECONTROL_SX1276_DIO0, CONFIG_IOHOMECONTROL_SX1276_DIO4);
        if (mSX1276Radio != nullptr)
        {
            mIoHome = new iohome::IoHomeControl(mSX1276Radio, loggerCallback, deviceStatusCallback);
            if (mIoHome != nullptr)
            {
                mIoHome->SetVerbose(logging);
                mIoHome->Begin(CONFIG_IOHOMECONTROL_DEFAULT_NODEID, CONFIG_IOHOMECONTROL_DEFAULT_KEY, mIoPassive);
                mIoHome->ConfigureRadio(CONFIG_IOHOMECONTROL_DEFAULT_TX_POWER);
            }
        }
#endif // ENABLE_IOHOMECONTROL
    }
    void IoRtsManager::InitializeMqtt()
    {
        if (MqttConfig::isEnabled())
        {
            // Create MQTT helper
            sMqttHelper = new MqttHelpers(this);
        }
        else
        {
            sMqttHelper = nullptr;
        }
    }
}
