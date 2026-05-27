#pragma once

#include <string>

#include "esp_err.h"

#include "IoRtsManager.hpp"
#include "mqtt_client.h"

// forward declaration
namespace IoRts
{
    class IoRtsManager;
}

namespace Helpers
{
    class MqttHelpers
    {
    public:
        /// @brief Construct a new MqttHelpers object
        /// @param manager Pointer to IoRtsManager object
        MqttHelpers(IoRts::IoRtsManager *manager);
        /// @brief Start MQTT client
        /// @return ESP_OK if no error, ESP_ERR_NOT_ALLOWED if MQTT is not enabled in configuration or already started, ...
        esp_err_t StartMqttClient();

        /// @brief Send discovery messages compatible with Home Assistant
        /// Sends a controller device discovery and a separate discovery for each IO device (linked via via_device)
        void SendDiscovery();

        /// @brief Send MQTT device status message for IO device
        /// @param deviceId IO device ID
        void SendIoDeviceStatus(const std::string &deviceId);

        const std::string &GetTopicPrefix() { return mTopicPrefix; }

        /// @brief Returns pointer to IoRtsManager instance
        /// @return pointer to IoRtsManager instance
        IoRts::IoRtsManager *GetIoRtsManager() { return mIoRtsManager; }

        /// @brief Returns IO Home passive mode
        /// @return true if IO Home is in passive mode
        bool isIoHomePassive() { return mIsIoHomePassive; }

        /// @brief Send log message to MQTT topic
        /// @param log log message to send
        void SendLog(const std::string &log);

        /// @brief Publish the list of inactive devices to the MQTT state topic and refresh controller discovery
        void PublishInactiveDevicesList();

        /// @brief Publish the linked remotes sensor state for a specific IO device
        /// @param deviceID IO device ID
        void PublishDeviceRemotesList(const std::string &deviceID);

    private:
        /// @brief Send controller device discovery message (reboot, config, management components)
        void SendControllerDiscovery();

        /// @brief Send individual discovery messages for each connected IO device (linked to controller via via_device)
        void SendIoDevicesDiscovery();

        IoRts::IoRtsManager *mIoRtsManager;         // Pointer to IoRtsManager object
        bool mStarted;                              // true if client is started
        bool mIsIoHomePassive;                      // true if IO Home is in passive mode
        std::string mTopicPrefix;                   // Topic prefix, initialized from configuration storage at boot (avoid to read it from storage everytime!)
        std::string mDiscoveryPrefix;               // Discovery prefix, initialized from configuration storage at boot (avoid to read it from storage everytime!)
        esp_mqtt_client_handle_t mMqttClientHandle; // Handle on MQTT client
    };
}