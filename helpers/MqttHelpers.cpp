#include "MqttHelpers.hpp"
#include "MqttConfig.hpp"

#include "cJSON.h"

#include "esp_app_desc.h"
#include "esp_log.h"

using namespace Config;
using namespace iohome;

static const std::string MQTT_CERTIFICATE_BEGIN = "-----BEGIN CERTIFICATE-----\n";
static const std::string MQTT_CERTIFICATE_END = "\n-----END CERTIFICATE-----";
static const std::string MQTT_CLIENT_COMMAND_TOPIC = "/set";                   // command topic
static const std::string MQTT_CLIENT_COMMAND_POSITION_TOPIC = "/set_position"; // command topic
static const std::string MQTT_CLIENT_STATE_TOPIC = "/state";                   // state topic
static const std::string MQTT_CLIENT_POSITION_TOPIC = "/position";             // position topic
static const std::string MQTT_CLIENT_DISCOVERY_TOPIC = "/config";              // discovery topic
static const std::string MQTT_CLIENT_BIRTH_WILL_TOPIC = "/status";             // birth and last will topic
static const std::string MQTT_CLIENT_REBOOT_ID = "button_reboot";              // unique id and topic for "reboot" button
static const std::string MQTT_CLIENT_PREFIX_IO = "io_";                        // unique_id prefix for IO devices
static const std::string MQTT_CLIENT_BIRTH_MSG = "online";                     // last will message
static const std::string MQTT_CLIENT_WILL_MSG = "offline";                     // last will message

static const char *TAG = "MQTTHelper";

namespace Helpers
{
    /// @brief Event handler registered to receive MQTT events
    /// @param handler_args user data registered to the event => MqttHelpers object
    /// @param base Event base for the handler(always MQTT Base)
    /// @param event_id The id for the received event.
    /// @param event_data The data for the event, esp_mqtt_event_handle_t
    static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
    {
        ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%" PRIi32, base, event_id);
        MqttHelpers *mqttHelper = static_cast<MqttHelpers *>(handler_args);
        esp_mqtt_event_handle_t event = static_cast<esp_mqtt_event_handle_t>(event_data);
        esp_mqtt_client_handle_t client = event->client;
        int msg_id;
        switch ((esp_mqtt_event_id_t)event_id)
        {
        case MQTT_EVENT_CONNECTED:
        {
            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
            // send birth message
            std::string topic = mqttHelper->GetTopicPrefix() + MQTT_CLIENT_BIRTH_WILL_TOPIC;
            const char *data = MQTT_CLIENT_BIRTH_MSG.c_str();
            msg_id = esp_mqtt_client_publish(client, topic.c_str(), data, 0, 0, 1);
            ESP_LOGD(TAG, "sent publish successful, msg_id=%d", msg_id);
            // Send discovery
            mqttHelper->SendDiscovery();
            // subscribe to all command topics
            topic = mqttHelper->GetTopicPrefix() + "/+" + MQTT_CLIENT_COMMAND_TOPIC;
            msg_id = esp_mqtt_client_subscribe(client, topic.c_str(), 0);
            topic = mqttHelper->GetTopicPrefix() + "/+" + MQTT_CLIENT_COMMAND_POSITION_TOPIC;
            msg_id = esp_mqtt_client_subscribe(client, topic.c_str(), 0);
            break;
        }
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
            break;

        case MQTT_EVENT_SUBSCRIBED:
            // ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d, return code=0x%02x ", event->msg_id, (uint8_t)*event->data);
            break;
        case MQTT_EVENT_UNSUBSCRIBED:
            // ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_PUBLISHED:
            // ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_DATA:
        {
            // ESP_LOGI(TAG, "MQTT_EVENT_DATA");
            // ESP_LOGI(TAG, "TOPIC=%.*s\r\n", event->topic_len, event->topic);
            // ESP_LOGI(TAG, "DATA=%.*s\r\n", event->data_len, event->data);
            // Parse command and do the job!
            // Is it a command for us?
            const std::string topic_str(event->topic, event->topic_len);
            if (topic_str.starts_with(mqttHelper->GetTopicPrefix()) && (topic_str.ends_with(MQTT_CLIENT_COMMAND_TOPIC) || topic_str.ends_with(MQTT_CLIENT_COMMAND_POSITION_TOPIC)))
            {
                size_t id_len;
                if (topic_str.ends_with(MQTT_CLIENT_COMMAND_TOPIC))
                    id_len = topic_str.length() - mqttHelper->GetTopicPrefix().length() - MQTT_CLIENT_COMMAND_TOPIC.length() - 1;
                else
                    id_len = topic_str.length() - mqttHelper->GetTopicPrefix().length() - MQTT_CLIENT_COMMAND_POSITION_TOPIC.length() - 1;
                if (id_len > 0)
                {
                    std::string entity_id = topic_str.substr(mqttHelper->GetTopicPrefix().length() + 1, id_len);
                    if (entity_id.compare(MQTT_CLIENT_REBOOT_ID) == 0)
                    {
                        ESP_LOGI(TAG, "REBOOT requested from MQTT!");
                        mqttHelper->GetIoRtsManager()->Reboot();
                    }
                    else if (entity_id.starts_with(MQTT_CLIENT_PREFIX_IO))
                    {
#ifdef CONFIG_IOHOMECONTROL_PASSIVE_MODE
                        break;
#endif
                        // Parse and execute command on targeted IO device
                        const std::string deviceID = entity_id.substr(MQTT_CLIENT_PREFIX_IO.length());
                        const std::string command(event->data, event->data_len);
                        if (deviceID.length() == NODE_ID_SIZE * 2)
                        {
                            // ESP_LOGI(TAG, "Received command %s for device %s", command.c_str(), deviceID.c_str());
                            if (command.compare("CLOSE") == 0)
                            {
                                mqttHelper->GetIoRtsManager()->mIoHome->CloseDevice(deviceID);
                            }
                            else if (command.compare("OPEN") == 0)
                            {
                                mqttHelper->GetIoRtsManager()->mIoHome->OpenDevice(deviceID);
                            }
                            else if (command.compare("STOP") == 0)
                            {
                                mqttHelper->GetIoRtsManager()->mIoHome->StopDevice(deviceID);
                            }
                            else if (command.compare("ON") == 0)
                            {
                                mqttHelper->GetIoRtsManager()->mIoHome->SetDevicePosition(deviceID, SWITCH_LIGHT_ON_POSITION);
                            }
                            else if (command.compare("OFF") == 0)
                            {
                                mqttHelper->GetIoRtsManager()->mIoHome->SetDevicePosition(deviceID, SWITCH_LIGHT_OFF_POSITION);
                            }
                            else if (command.compare("LOCK") == 0)
                            {
                                mqttHelper->GetIoRtsManager()->mIoHome->SetDevicePosition(deviceID, SWITCH_LIGHT_OFF_POSITION); // not sure, wait for feedback
                            }
                            else if (command.compare("UNLOCK") == 0)
                            {
                                mqttHelper->GetIoRtsManager()->mIoHome->SetDevicePosition(deviceID, SWITCH_LIGHT_ON_POSITION); // not sure, wait for feedback
                            }
                            else if (topic_str.ends_with(MQTT_CLIENT_COMMAND_POSITION_TOPIC)) // it should be a position between 0 and 100
                            {
                                float position = strtof(command.c_str(), NULL);
                                if (position >= (float)0.0 || position <= (float)100.0)
                                {
                                    mqttHelper->GetIoRtsManager()->mIoHome->SetDevicePosition(deviceID, position);
                                }
                                else
                                    ESP_LOGE(TAG, "Received command %s for device %s -> invalid position!", command.c_str(), deviceID.c_str());
                            }
                            else
                            {
                                ESP_LOGE(TAG, "Received command %s for device %s -> invalid data!", command.c_str(), deviceID.c_str());
                            }
                        }
                        else
                            ESP_LOGE(TAG, "Received command %s for device %s -> invalid device ID length!", command.c_str(), deviceID.c_str());
                    }
                }
            }
            break;
        }
        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT_EVENT_ERROR");
            if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT)
            {
                ESP_LOGE(TAG, "Last error code reported from esp-tls: 0x%x", event->error_handle->esp_tls_last_esp_err);
                ESP_LOGE(TAG, "Last tls stack error number: 0x%x", event->error_handle->esp_tls_stack_err);
                ESP_LOGE(TAG, "Last captured errno : %d (%s)", event->error_handle->esp_transport_sock_errno,
                         strerror(event->error_handle->esp_transport_sock_errno));
            }
            else if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED)
            {
                ESP_LOGE(TAG, "Connection refused error: 0x%x", event->error_handle->connect_return_code);
            }
            else
            {
                ESP_LOGE(TAG, "Unknown error type: 0x%x", event->error_handle->error_type);
            }
            break;
        default:
            ESP_LOGI(TAG, "Other event id:%d", event->event_id);
            break;
        }
    }

    MqttHelpers::MqttHelpers(IoRts::IoRtsManager *manager)
        : mIoRtsManager(manager), mStarted(false)
    {
        mTopicPrefix = MqttConfig::GetTopicPrefix();
        mDiscoveryPrefix = MqttConfig::GetDiscoveryPrefix();
    }
    esp_err_t MqttHelpers::StartMqttClient()
    {
        if (!MqttConfig::isEnabled() || mStarted)
            return ESP_ERR_NOT_ALLOWED;
        esp_err_t err = ESP_OK;
        // Configure client
        esp_mqtt_client_config_t mqtt_cfg;
        memset(&mqtt_cfg, 0, sizeof(esp_mqtt_client_config_t));
        mqtt_cfg.broker.address.hostname = MqttConfig::GetBrokerAddress().c_str();
        mqtt_cfg.broker.address.port = MqttConfig::GetBrokerPort();
        bool tls_enabled = MqttConfig::isTLSEnabled();
        mqtt_cfg.broker.address.transport = tls_enabled ? MQTT_TRANSPORT_OVER_SSL : MQTT_TRANSPORT_OVER_TCP;
        if (tls_enabled)
        {
            mqtt_cfg.broker.verification.certificate = std::string(MQTT_CERTIFICATE_BEGIN + MqttConfig::GetBrokerCertificate() + MQTT_CERTIFICATE_END).c_str();
            mqtt_cfg.broker.verification.skip_cert_common_name_check = true;
        }
        mqtt_cfg.credentials.client_id = MqttConfig::GetClientId().c_str();
        mqtt_cfg.credentials.username = MqttConfig::GetClientUsername().c_str();
        mqtt_cfg.credentials.authentication.password = MqttConfig::GetClientPassword().c_str();
        mqtt_cfg.session.protocol_ver = MQTT_PROTOCOL_V_3_1_1;
        mqtt_cfg.session.last_will.topic = std::string(MqttConfig::GetTopicPrefix() + MQTT_CLIENT_BIRTH_WILL_TOPIC).c_str();
        mqtt_cfg.session.last_will.msg = MQTT_CLIENT_WILL_MSG.c_str();
        mqtt_cfg.session.last_will.msg_len = MQTT_CLIENT_WILL_MSG.length();
        mqtt_cfg.session.last_will.qos = 1;
        mqtt_cfg.session.last_will.retain = true;
        mqtt_cfg.network.disable_auto_reconnect = false;
        mMqttClientHandle = esp_mqtt_client_init(&mqtt_cfg);
        if (mMqttClientHandle == NULL)
        {
            ESP_LOGE(TAG, "Failed to create MQTT client!");
            return ESP_FAIL;
        }
        // Register event handler
        err = esp_mqtt_client_register_event(mMqttClientHandle, MQTT_EVENT_ANY, mqtt_event_handler, this);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to register MQTT event handler! (%d)", err);
            esp_mqtt_client_destroy(mMqttClientHandle);
            return ESP_FAIL;
        }
        // Start
        err = esp_mqtt_client_start(mMqttClientHandle);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to start MQTT client! (%d)", err);
            esp_mqtt_client_destroy(mMqttClientHandle);
            return ESP_FAIL;
        }
        mStarted = true;
        return err;
    }
    void MqttHelpers::SendDiscovery()
    {
        // See https://www.home-assistant.io/integrations/mqtt/#mqtt-discovery
#ifdef CONFIG_IOHOMECONTROL_PASSIVE_MODE
        bool passive = true;
#else
        bool passive = false;
#endif
        bool error = false;
        cJSON *discovery = NULL;
        cJSON *dev = NULL;
        cJSON *o = NULL;
        cJSON *cmps = NULL;
        cJSON *cmp = NULL;
        std::string clientId = MqttConfig::GetClientId();
        // Create Discovery JSON
        discovery = cJSON_CreateObject();
        if (discovery == NULL)
            error = true;
        // Create "dev" (device) section
        if (!error)
        {
            dev = cJSON_AddObjectToObject(discovery, "dev");
            if (dev == NULL)
                error = true;
            else
            {
                error = error || (cJSON_AddStringToObject(dev, "ids", clientId.c_str()) == NULL);  // identifiers
                error = error || (cJSON_AddStringToObject(dev, "name", clientId.c_str()) == NULL); // name
                error = error || (cJSON_AddStringToObject(dev, "mf", "nicolas5000") == NULL);      // manufacturer
            }
        }
        // Create "o" (origin) section
        if (!error)
        {
            o = cJSON_AddObjectToObject(discovery, "o");
            if (o == NULL)
                error = true;
            else
            {
                error = error || (cJSON_AddStringToObject(o, "name", clientId.c_str()) == NULL);                             // name
                error = error || (cJSON_AddStringToObject(o, "url", "https://github.com/nicolas5000/io-rts-esp32") == NULL); // support_url
                const esp_app_desc_t *desc = esp_app_get_description();
                error = error || (cJSON_AddStringToObject(o, "sw", desc->version) == NULL); // sw_version
            }
        }
        // Create "cmps" section
        if (!error)
        {
            cmps = cJSON_AddObjectToObject(discovery, "cmps");
            if (cmps == NULL)
                error = true;
            else
            {
                // Add reboot button
                cmp = cJSON_AddObjectToObject(cmps, "reboot");
                if (cmp == NULL)
                    error = true;
                else
                {
                    error = error || (cJSON_AddStringToObject(cmp, "p", "button") == NULL);                              // platform
                    error = error || (cJSON_AddStringToObject(cmp, "unique_id", MQTT_CLIENT_REBOOT_ID.c_str()) == NULL); // unique_id
                    error = error || (cJSON_AddStringToObject(cmp, "name", "Reboot") == NULL);                           // name
                    std::string reboot_topic = mTopicPrefix + "/" + MQTT_CLIENT_REBOOT_ID + MQTT_CLIENT_COMMAND_TOPIC;
                    error = error || (cJSON_AddStringToObject(cmp, "command_topic", reboot_topic.c_str()) == NULL); // command_topic
                }
                if (!passive)
                {
                    // Add IO devices
                    std::lock_guard<std::mutex> guard(mIoRtsManager->mIoDevicesMutex); // Take mutex! It will be released when quitting the scope (after for loop)
                    for (auto it = mIoRtsManager->mIoDevices.begin(); it != mIoRtsManager->mIoDevices.end(); ++it)
                    {
                        // Add this device to JSON
                        std::string device_id = MQTT_CLIENT_PREFIX_IO + it->first;
                        std::string device_cmd_topic = GetTopicPrefix() + "/" + device_id + MQTT_CLIENT_COMMAND_TOPIC;
                        std::string device_state_topic = GetTopicPrefix() + "/" + device_id + MQTT_CLIENT_STATE_TOPIC;
                        std::string device_position_topic = GetTopicPrefix() + "/" + device_id + MQTT_CLIENT_POSITION_TOPIC;
                        std::string device_cmd_position_topic = GetTopicPrefix() + "/" + device_id + MQTT_CLIENT_COMMAND_POSITION_TOPIC;
                        std::string device_plateform;
                        cmp = cJSON_AddObjectToObject(cmps, device_id.c_str());
                        if (cmp == NULL)
                            error = true;
                        else
                        {
                            switch (it->second.info.device_type)
                            {
                            case DeviceType::VENETIAN_BLIND:
                            case DeviceType::ROLLER_SHUTTER:
                            case DeviceType::AWNING:
                            case DeviceType::WINDOW_OPENER:
                            case DeviceType::GARAGE_OPENER:
                            case DeviceType::GATE_OPENER:
                            case DeviceType::ROLLING_DOOR_OPENER:
                            case DeviceType::BLIND:
                            case DeviceType::DUAL_SHUTTER:
                            case DeviceType::HORIZONTAL_AWNING:
                            case DeviceType::EXTERNAL_VENETIAN_BLIND:
                            case DeviceType::LOUVRE_BLIND:
                            case DeviceType::CURTAIN_TRACK:
                            case DeviceType::SWINGING_SHUTTER:
                            {
                                // https://www.home-assistant.io/integrations/cover.mqtt/
                                device_plateform = "cover";
                                if (!it->second.is_deleted)
                                {
                                    // add attributes only if not deleted
                                    error = error || (cJSON_AddNumberToObject(cmp, "position_closed", 100) == NULL);                                  // position_closed
                                    error = error || (cJSON_AddNumberToObject(cmp, "position_open", 0) == NULL);                                      // position_open
                                    error = error || (cJSON_AddStringToObject(cmp, "position_topic", device_position_topic.c_str()) == NULL);         // position_topic
                                    error = error || (cJSON_AddStringToObject(cmp, "set_position_topic", device_cmd_position_topic.c_str()) == NULL); // set_position_topic
                                    // add device_class https://www.home-assistant.io/integrations/cover/#device_class
                                    std::string type = "shutter";
                                    switch (it->second.info.device_type)
                                    {
                                    case DeviceType::VENETIAN_BLIND:
                                    case DeviceType::BLIND:
                                    case DeviceType::EXTERNAL_VENETIAN_BLIND:
                                    case DeviceType::LOUVRE_BLIND:
                                        type = "blind";
                                        break;
                                    case DeviceType::ROLLER_SHUTTER:
                                    case DeviceType::DUAL_SHUTTER:
                                    case DeviceType::SWINGING_SHUTTER:
                                        type = "shutter";
                                        break;
                                    case DeviceType::AWNING:
                                    case DeviceType::HORIZONTAL_AWNING:
                                        type = "awning";
                                        break;
                                    case DeviceType::WINDOW_OPENER:
                                        type = "window";
                                        break;
                                    case DeviceType::GARAGE_OPENER:
                                        type = "garage";
                                        break;
                                    case DeviceType::GATE_OPENER:
                                        type = "gate";
                                        break;
                                    case DeviceType::ROLLING_DOOR_OPENER:
                                        type = "door";
                                        break;
                                    case DeviceType::CURTAIN_TRACK:
                                        type = "curtain";
                                        break;
                                    default:
                                        type = "None";
                                        break;
                                    }
                                    error = error || (cJSON_AddStringToObject(cmp, "device_class", type.c_str()) == NULL); // device_class
                                    // TODO: add "favorite position" button
                                }
                                else
                                {
                                    // TODO: remove "favorite position" button
                                }
                                break;
                            }
                            case DeviceType::LIGHT:
                                // https://www.home-assistant.io/integrations/light.mqtt/
                                device_plateform = "light";
                                if (!it->second.is_deleted)
                                {
                                    error = error || (cJSON_AddStringToObject(cmp, "optimistic", "true") == NULL); // optimistic
                                    // TODO: manage brightness, waiting for feedback
                                }
                                break;
                            case DeviceType::ON_OFF_SWITCH:
                                // https://www.home-assistant.io/integrations/switch.mqtt/
                                device_plateform = "switch";
                                if (!it->second.is_deleted)
                                {
                                    error = error || (cJSON_AddStringToObject(cmp, "optimistic", "true") == NULL); // optimistic
                                }
                                break;
                            case DeviceType::LOCK:
                                // https://www.home-assistant.io/integrations/lock.mqtt/
                                device_plateform = "lock";
                                break;
                            // other device types we don't support yet so don't send MQTT discovery for now
                            case DeviceType::UNKNOWN:
                            case DeviceType::UNKNOWN_0B:
                            case DeviceType::BEACON:
                            case DeviceType::HEATING_TEMPERATURE_INTERFACE: // https://www.home-assistant.io/integrations/climate.mqtt/ ?
                            case DeviceType::VENTILATION_POINT:             // https://www.home-assistant.io/integrations/fan.mqtt/ ?
                            case DeviceType::EXTERIOR_HEATING:              // https://www.home-assistant.io/integrations/climate.mqtt/?
                            case DeviceType::HEAT_PUMP:                     // https://www.home-assistant.io/integrations/climate.mqtt/ ?
                            case DeviceType::INTRUSION_ALARM:               // https://www.home-assistant.io/integrations/alarm_control_panel.mqtt/ ?
                            default:
                                error = true;
                                cJSON_DeleteItemFromObject(cmps, device_id.c_str());
                                ESP_LOGE(TAG, "Failed to add device to MQTT discovery: type not managed! (%d)", it->second.info.device_type);
                                break;
                            }
                            // add 'platform' even if device is deleted to inform Home Assistant that this component is deleted
                            error = error || (cJSON_AddStringToObject(cmp, "p", device_plateform.c_str()) == NULL); // platform
                            if (!it->second.is_deleted)
                            {
                                // add attributes only if not deleted
                                error = error || (cJSON_AddStringToObject(cmp, "unique_id", device_id.c_str()) == NULL);            // unique_id
                                error = error || (cJSON_AddStringToObject(cmp, "name", it->second.info.name) == NULL);              // name
                                error = error || (cJSON_AddStringToObject(cmp, "command_topic", device_cmd_topic.c_str()) == NULL); // command_topic
                                error = error || (cJSON_AddStringToObject(cmp, "state_topic", device_state_topic.c_str()) == NULL); // state_topic
                            }
                        }
                    }
                    // Mutex automatically released!
                }
            }
        }
        // Add shared options: availability, command_topic, state_topic, ...
        std::string availability_topic = mTopicPrefix + MQTT_CLIENT_BIRTH_WILL_TOPIC;
        error = error || (cJSON_AddStringToObject(discovery, "availability_topic", availability_topic.c_str()) == NULL);
        // Send discovery to MQTT topic
        if (!error)
        {
            std::string topic = mDiscoveryPrefix + MQTT_CLIENT_DISCOVERY_TOPIC;
            const char *data = cJSON_Print(discovery);
            if (data == NULL)
            {
                ESP_LOGE(TAG, "Failed to create discovery string");
            }
            else
            {
                esp_mqtt_client_publish(mMqttClientHandle, topic.c_str(), data, 0, 0, 1);
                ESP_LOGI(TAG, "Sent discovery successfully");
            }
        }
        cJSON_Delete(discovery);
    }
    void MqttHelpers::SendIoDeviceStatus(const std::string &deviceId)
    {
#ifdef CONFIG_IOHOMECONTROL_PASSIVE_MODE
        return;
#endif
        std::lock_guard<std::mutex> guard(mIoRtsManager->mIoDevicesMutex); // Take mutex! It will be released when quitting the scope (after for loop)
        // send device status
        if (mIoRtsManager != nullptr && mIoRtsManager->mIoDevices.contains(deviceId))
        {
            auto device = mIoRtsManager->mIoDevices.find(deviceId);
            std::string stateTopic = GetTopicPrefix() + "/" + MQTT_CLIENT_PREFIX_IO + device->first + MQTT_CLIENT_STATE_TOPIC;
            std::string positionTopic = GetTopicPrefix() + "/" + MQTT_CLIENT_PREFIX_IO + device->first + MQTT_CLIENT_POSITION_TOPIC;
            std::string data;
            if (device->second.is_deleted)
            {
                // device is marked as deleted, send empty messages to remove all retained messages for this device
                esp_mqtt_client_publish(mMqttClientHandle, stateTopic.c_str(), NULL, 0, 0, 1);
                esp_mqtt_client_publish(mMqttClientHandle, positionTopic.c_str(), NULL, 0, 0, 1);
                return;
            }
            switch (device->second.info.device_type)
            {
            case DeviceType::VENETIAN_BLIND:
            case DeviceType::ROLLER_SHUTTER:
            case DeviceType::AWNING:
            case DeviceType::WINDOW_OPENER:
            case DeviceType::GARAGE_OPENER:
            case DeviceType::GATE_OPENER:
            case DeviceType::ROLLING_DOOR_OPENER:
            case DeviceType::BLIND:
            case DeviceType::DUAL_SHUTTER:
            case DeviceType::HORIZONTAL_AWNING:
            case DeviceType::EXTERNAL_VENETIAN_BLIND:
            case DeviceType::LOUVRE_BLIND:
            case DeviceType::CURTAIN_TRACK:
            case DeviceType::SWINGING_SHUTTER:
                // send position
                if (device->second.position != UNKNOWN_POSITION)
                {
                    data = std::to_string((int)device->second.position);
                    esp_mqtt_client_publish(mMqttClientHandle, positionTopic.c_str(), data.c_str(), 0, 0, 1);
                    // send state
                    if (device->second.is_stopped)
                    {
                        if (device->second.position > (float)99.9)
                            data = "closed";
                        else
                            data = "open";
                    }
                    else
                    {
                        if (device->second.position > device->second.target)
                            data = "opening";
                        else
                            data = "closing";
                    }
                    esp_mqtt_client_publish(mMqttClientHandle, stateTopic.c_str(), data.c_str(), 0, 0, 1);
                }
                else
                {
                    data = "None";
                    esp_mqtt_client_publish(mMqttClientHandle, positionTopic.c_str(), data.c_str(), 0, 0, 1);
                    data = "open";
                    esp_mqtt_client_publish(mMqttClientHandle, stateTopic.c_str(), data.c_str(), 0, 0, 1);
                }
                break;
            case DeviceType::LIGHT:
                if (!device->second.is_stopped)
                    return; // don't send current position as it is not correct
                if (device->second.position == 0)
                    data = "ON";
                else if (device->second.position == UNKNOWN_POSITION)
                    data = "None";
                else
                    data = "OFF";
                esp_mqtt_client_publish(mMqttClientHandle, stateTopic.c_str(), data.c_str(), 0, 0, 1);
                // TODO: manage brightness, waiting for feedback
                break;
            case DeviceType::ON_OFF_SWITCH:
                if (!device->second.is_stopped)
                    return; // don't send current position as it is not correct
                if (device->second.position == 0)
                    data = "ON";
                else if (device->second.position == UNKNOWN_POSITION)
                    data = "None";
                else
                    data = "OFF";
                esp_mqtt_client_publish(mMqttClientHandle, stateTopic.c_str(), data.c_str(), 0, 0, 1);
                break;
            case DeviceType::LOCK:
                if (device->second.position != UNKNOWN_POSITION)
                {
                    // find state
                    if (device->second.is_stopped)
                    {
                        if (device->second.position > (float)99.9)
                            data = "LOCKED";
                        else
                            data = "UNLOCKED";
                    }
                    else // not sure this can happen, waiting for feedback
                    {
                        if (device->second.position > device->second.target)
                            data = "UNLOCKING";
                        else
                            data = "LOCKING";
                    }
                }
                else
                {
                    data = "None";
                }
                esp_mqtt_client_publish(mMqttClientHandle, stateTopic.c_str(), data.c_str(), 0, 0, 1);
                break;
            // other device types we don't support yet so don't send MQTT discovery for now
            case DeviceType::UNKNOWN:
            case DeviceType::UNKNOWN_0B:
            case DeviceType::BEACON:
            case DeviceType::HEATING_TEMPERATURE_INTERFACE:
            case DeviceType::VENTILATION_POINT:
            case DeviceType::EXTERIOR_HEATING:
            case DeviceType::HEAT_PUMP:
            case DeviceType::INTRUSION_ALARM:
            default:
                break;
            }
        }
        // Mutex automatically released!
    }
}