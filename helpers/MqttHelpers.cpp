#include "MqttHelpers.hpp"
#include "MqttConfig.hpp"
#include "IoHomeConfig.hpp"

#include <algorithm>

#include "cJSON.h"

#include "esp_app_desc.h"
#include "esp_log.h"

using namespace Config;
using namespace iohome;

static const std::string MQTT_CERTIFICATE_BEGIN = "-----BEGIN CERTIFICATE-----\n";
static const std::string MQTT_CERTIFICATE_END = "\n-----END CERTIFICATE-----";
static const std::string MQTT_CLIENT_COMMAND_TOPIC = "/set";                   // command topic
static const std::string MQTT_CLIENT_COMMAND_POSITION_TOPIC = "/set_position"; // position topic
static const std::string MQTT_CLIENT_COMMAND_TILT_TOPIC = "/set_tilt";         // tilt topic
static const std::string MQTT_CLIENT_COMMAND_FAV_POS_TOPIC = "/set_fav_pos";   // set favorite position topic
static const std::string MQTT_CLIENT_STATE_TOPIC = "/state";                   // state topic
static const std::string MQTT_CLIENT_POSITION_TOPIC = "/position";             // position topic
static const std::string MQTT_CLIENT_TILT_TOPIC = "/tilt";                     // tilt topic
static const std::string MQTT_CLIENT_DISCOVERY_TOPIC = "/config";              // discovery topic

static const std::string MQTT_CLIENT_REBOOT_ID = "button_reboot";     // unique id and topic for "reboot" button
static const std::string MQTT_CLIENT_DISCOVER_ID = "button_discover"; // unique id and topic for "Discover" button

static const std::string MQTT_CLIENT_CONFIG_IO_ID = "config_io";   // topic for IO layer configuration components
static const std::string MQTT_CLIENT_IO_LOGGING_ID = "IoLogging";  // unique id and action for "IoLogging" component
static const std::string MQTT_CLIENT_IO_PASSIVE_ID = "IoPassive";  // unique id and action for "IoPassive" component
static const std::string MQTT_CLIENT_IO_TX_POWER_ID = "IoTxPower"; // unique id and action for "IoTxPower" component

static const std::string MQTT_CLIENT_MANAGE_IO_ID = "manage_io";             // topic for IO devices management components
static const std::string MQTT_CLIENT_ADD_DEVICE_ID = "AddIoDevice";          // unique id and action for "AddDevice" component
static const std::string MQTT_CLIENT_REM_DEVICE_ID = "RemoveIoDevice";       // unique id and action for "RemoveDevice" component
static const std::string MQTT_CLIENT_INV_DEVICE_ID = "InvertIoDevice";       // unique id and action for "InvertDevice" component
static const std::string MQTT_CLIENT_LINK_REMOTE_ID = "LinkIoRemote";        // unique id and action for "LinkRemote" component
static const std::string MQTT_CLIENT_REM_REMOTE_ID = "RemoveIoRemote";       // unique id and action for "RemoveRemote" component
static const std::string MQTT_CLIENT_SET_DEVICE_NAME_ID = "SetIoDeviceName"; // unique id and action for "SetDeviceName" component

static const std::string MQTT_CLIENT_PREFIX_IO = "io_";          // unique_id prefix for IO devices
static const std::string MQTT_CLIENT_SUFFIX_FAV_IO = "_fav";    // unique_id suffix for IO devices "favorite" button
static const std::string MQTT_CLIENT_SUFFIX_IDENTIFY = "_ident"; // unique_id suffix for IO devices "identify" button

static const std::string MQTT_CLIENT_BIRTH_WILL_TOPIC = "/status"; // birth and last will topic
static const std::string MQTT_CLIENT_BIRTH_MSG = "online";         // last will message - birth
static const std::string MQTT_CLIENT_WILL_MSG = "offline";         // last will message - death

static const std::string MQTT_CLIENT_LOG_TOPIC = "/log"; // log topic

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
            // Send current IO config (logging, passive mode, Tx power)
            topic = mqttHelper->GetTopicPrefix() + "/" + MQTT_CLIENT_CONFIG_IO_ID + MQTT_CLIENT_STATE_TOPIC;
            cJSON *ioConfig = cJSON_CreateObject();
            if (ioConfig != NULL)
            {
                bool error = false;
                std::string logging = IoHomeConfig::isLoggingEnabled() ? "ON" : "OFF";
                std::string passive = IoHomeConfig::isPassiveModeEnabled() ? "ON" : "OFF";
                error = error || (cJSON_AddStringToObject(ioConfig, MQTT_CLIENT_IO_LOGGING_ID.c_str(), logging.c_str()) == NULL);             // IoLogging
                error = error || (cJSON_AddStringToObject(ioConfig, MQTT_CLIENT_IO_PASSIVE_ID.c_str(), passive.c_str()) == NULL);             // IoPassive
                error = error || (cJSON_AddNumberToObject(ioConfig, MQTT_CLIENT_IO_TX_POWER_ID.c_str(), IoHomeConfig::GetTxPower()) == NULL); // IoTxPower
                const char *config = cJSON_Print(ioConfig);
                esp_mqtt_client_publish(client, topic.c_str(), config, 0, 0, 1);
                cJSON_free((void *)config);
                cJSON_Delete(ioConfig);
            }
            // Send discovery
            mqttHelper->SendDiscovery();
            // subscribe to all command topics
            topic = mqttHelper->GetTopicPrefix() + "/+" + MQTT_CLIENT_COMMAND_TOPIC;
            msg_id = esp_mqtt_client_subscribe(client, topic.c_str(), 0);
            topic = mqttHelper->GetTopicPrefix() + "/+" + MQTT_CLIENT_COMMAND_POSITION_TOPIC;
            msg_id = esp_mqtt_client_subscribe(client, topic.c_str(), 0);
            topic = mqttHelper->GetTopicPrefix() + "/+" + MQTT_CLIENT_COMMAND_FAV_POS_TOPIC;
            msg_id = esp_mqtt_client_subscribe(client, topic.c_str(), 0);
            topic = mqttHelper->GetTopicPrefix() + "/+" + MQTT_CLIENT_COMMAND_TILT_TOPIC;
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
            if (topic_str.starts_with(mqttHelper->GetTopicPrefix()) &&
                (topic_str.ends_with(MQTT_CLIENT_COMMAND_TOPIC) || topic_str.ends_with(MQTT_CLIENT_COMMAND_POSITION_TOPIC) || topic_str.ends_with(MQTT_CLIENT_COMMAND_FAV_POS_TOPIC) || topic_str.ends_with(MQTT_CLIENT_COMMAND_TILT_TOPIC)))
            {
                size_t id_len = 0;
                if (topic_str.ends_with(MQTT_CLIENT_COMMAND_TOPIC))
                    id_len = topic_str.length() - mqttHelper->GetTopicPrefix().length() - MQTT_CLIENT_COMMAND_TOPIC.length() - 1;
                else if (topic_str.ends_with(MQTT_CLIENT_COMMAND_POSITION_TOPIC))
                    id_len = topic_str.length() - mqttHelper->GetTopicPrefix().length() - MQTT_CLIENT_COMMAND_POSITION_TOPIC.length() - 1;
                else if (topic_str.ends_with(MQTT_CLIENT_COMMAND_FAV_POS_TOPIC))
                    id_len = topic_str.length() - mqttHelper->GetTopicPrefix().length() - MQTT_CLIENT_COMMAND_FAV_POS_TOPIC.length() - 1;
                else if (topic_str.ends_with(MQTT_CLIENT_COMMAND_TILT_TOPIC))
                    id_len = topic_str.length() - mqttHelper->GetTopicPrefix().length() - MQTT_CLIENT_COMMAND_TILT_TOPIC.length() - 1;
                if (id_len > 0)
                {
                    std::string entity_id = topic_str.substr(mqttHelper->GetTopicPrefix().length() + 1, id_len);
                    if (entity_id.compare(MQTT_CLIENT_REBOOT_ID) == 0)
                    {
                        ESP_LOGI(TAG, "REBOOT requested from MQTT!");
                        mqttHelper->GetIoRtsManager()->Reboot();
                    }
                    else if (entity_id.compare(MQTT_CLIENT_CONFIG_IO_ID) == 0 && topic_str.ends_with(MQTT_CLIENT_COMMAND_TOPIC))
                    {
                        ESP_LOGI(TAG, "IO CONFIG requested from MQTT!");
                        // Let's parse the JSON
                        char *buf = new char[event->data_len + 1];
                        memcpy(buf, event->data, event->data_len);
                        buf[event->data_len] = '\0';
                        cJSON *root = cJSON_Parse(buf);
                        delete[] buf;
                        if (root == nullptr)
                        {
                            ESP_LOGE(TAG, "Failed to parse JSON from IO CONFIG requested from MQTT!");
                            break;
                        }
                        // Let's check what we have to do
                        cJSON *loggingItem = cJSON_GetObjectItem(root, MQTT_CLIENT_IO_LOGGING_ID.c_str());
                        if (cJSON_IsString(loggingItem))
                        {
                            std::string value(loggingItem->valuestring);
                            if (value.compare("ON") == 0)
                            {
                                IoHomeConfig::ActivateLogging(true);
                            }
                            else if (value.compare("OFF") == 0)
                            {
                                IoHomeConfig::ActivateLogging(false);
                            }
                        }
                        cJSON *passiveItem = cJSON_GetObjectItem(root, MQTT_CLIENT_IO_PASSIVE_ID.c_str());
                        if (cJSON_IsString(passiveItem))
                        {
                            std::string value(passiveItem->valuestring);
                            if (value.compare("ON") == 0)
                            {
                                IoHomeConfig::ActivatePassiveMode(true);
                            }
                            else if (value.compare("OFF") == 0)
                            {
                                IoHomeConfig::ActivatePassiveMode(false);
                            }
                        }
                        cJSON *txPowerItem = cJSON_GetObjectItem(root, MQTT_CLIENT_IO_TX_POWER_ID.c_str());
                        if (cJSON_IsNumber(txPowerItem))
                        {
                            IoHomeConfig::SetTxPower(cJSON_GetNumberValue(txPowerItem));
                        }
                        // Don't forget to delete JSON object to free memory!
                        cJSON_Delete(root);
                    }
                    else if (entity_id.compare(MQTT_CLIENT_MANAGE_IO_ID) == 0 && topic_str.ends_with(MQTT_CLIENT_COMMAND_TOPIC))
                    {
                        ESP_LOGI(TAG, "IO MANAGE requested from MQTT!");
                        // Let's parse the JSON
                        char *buf = new char[event->data_len + 1];
                        memcpy(buf, event->data, event->data_len);
                        buf[event->data_len] = '\0';
                        cJSON *root = cJSON_Parse(buf);
                        delete[] buf;
                        if (root == nullptr)
                        {
                            ESP_LOGE(TAG, "Failed to parse JSON from IO MANAGE requested from MQTT!");
                            break;
                        }
                        // Let's check what we have to do
                        cJSON *addDeviceItem = cJSON_GetObjectItem(root, MQTT_CLIENT_ADD_DEVICE_ID.c_str());
                        if (cJSON_IsString(addDeviceItem))
                        {
                            std::string deviceID(addDeviceItem->valuestring);
                            if (deviceID.length() == NODE_ID_SIZE * 2)
                            {
                                std::transform(deviceID.begin(), deviceID.end(), deviceID.begin(), [](unsigned char c)
                                               { return std::toupper(c); }); // convert to uppercase
                                mqttHelper->GetIoRtsManager()->mIoHome->AddDevice(deviceID);
                            }
                        }
                        cJSON *removeDeviceItem = cJSON_GetObjectItem(root, MQTT_CLIENT_REM_DEVICE_ID.c_str());
                        if (cJSON_IsString(removeDeviceItem))
                        {
                            std::string deviceID(removeDeviceItem->valuestring);
                            if (deviceID.length() == NODE_ID_SIZE * 2)
                            {
                                std::transform(deviceID.begin(), deviceID.end(), deviceID.begin(), [](unsigned char c)
                                               { return std::toupper(c); }); // convert to uppercase
                                mqttHelper->GetIoRtsManager()->RemoveIoDevice(deviceID);
                            }
                        }
                        cJSON *invertDeviceItem = cJSON_GetObjectItem(root, MQTT_CLIENT_INV_DEVICE_ID.c_str());
                        if (cJSON_IsString(invertDeviceItem))
                        {
                            std::string deviceID(invertDeviceItem->valuestring);
                            if (deviceID.length() == NODE_ID_SIZE * 2)
                            {
                                std::transform(deviceID.begin(), deviceID.end(), deviceID.begin(), [](unsigned char c)
                                               { return std::toupper(c); }); // convert to uppercase
                                mqttHelper->GetIoRtsManager()->mIoHome->InvertOpenClosePositionForDevice(deviceID);
                            }
                        }
                        cJSON *linkRemoteToDeviceItem = cJSON_GetObjectItem(root, MQTT_CLIENT_LINK_REMOTE_ID.c_str());
                        if (cJSON_IsString(linkRemoteToDeviceItem))
                        {
                            std::string data(linkRemoteToDeviceItem->valuestring);
                            if (data.length() != 13 || data[6] != ';')
                            {
                                // ESP_LOGE(TAG, "Failed to parse JSON to link remote to IO device from MQTT!");
                            }
                            else
                            {
                                std::string deviceID = data.substr(0, NODE_ID_SIZE * 2);
                                std::string remoteID = data.substr(NODE_ID_SIZE * 2 + 1);
                                std::transform(deviceID.begin(), deviceID.end(), deviceID.begin(), [](unsigned char c)
                                               { return std::toupper(c); }); // convert to uppercase
                                std::transform(remoteID.begin(), remoteID.end(), remoteID.begin(), [](unsigned char c)
                                               { return std::toupper(c); }); // convert to uppercase
                                mqttHelper->GetIoRtsManager()->LinkRemoteToDevice(remoteID, deviceID);
                            }
                        }
                        cJSON *removeRemoteItem = cJSON_GetObjectItem(root, MQTT_CLIENT_REM_REMOTE_ID.c_str());
                        if (cJSON_IsString(removeRemoteItem))
                        {
                            std::string remoteID(removeRemoteItem->valuestring);
                            if (remoteID.length() == NODE_ID_SIZE * 2)
                            {
                                std::transform(remoteID.begin(), remoteID.end(), remoteID.begin(), [](unsigned char c)
                                               { return std::toupper(c); }); // convert to uppercase
                                mqttHelper->GetIoRtsManager()->RemoveIoRemote(remoteID);
                            }
                        }
                        cJSON *setDeviceNameItem = cJSON_GetObjectItem(root, MQTT_CLIENT_SET_DEVICE_NAME_ID.c_str());
                        if (cJSON_IsString(setDeviceNameItem))
                        {
                            std::string data(setDeviceNameItem->valuestring);
                            if (data.length() < 8 || data[6] != ';')
                            {
                                // ESP_LOGE(TAG, "Failed to parse JSON to change IO device name from MQTT!");
                            }
                            else
                            {
                                std::string deviceID = data.substr(0, NODE_ID_SIZE * 2);
                                std::string deviceName = data.substr(NODE_ID_SIZE * 2 + 1);
                                std::transform(deviceID.begin(), deviceID.end(), deviceID.begin(), [](unsigned char c)
                                               { return std::toupper(c); }); // convert to uppercase
                                mqttHelper->GetIoRtsManager()->mIoHome->SetDeviceName(deviceID, deviceName);
                            }
                        }
                        // Don't forget to delete JSON object to free memory!
                        cJSON_Delete(root);
                    }
                    else if (entity_id.compare(MQTT_CLIENT_DISCOVER_ID) == 0)
                    {
                        if (mqttHelper->isIoHomePassive())
                            break; // don't process IO devices commands if in passive mode
                        ESP_LOGI(TAG, "DISCOVER requested from MQTT!");
                        mqttHelper->GetIoRtsManager()->mIoHome->DiscoverAndPairDevice();
                    }
                    else if (entity_id.starts_with(MQTT_CLIENT_PREFIX_IO))
                    {
                        if (mqttHelper->isIoHomePassive())
                            break; // don't process IO devices commands if in passive mode

                        // Parse and execute command on targeted IO device
                        const std::string deviceID = entity_id.substr(MQTT_CLIENT_PREFIX_IO.length());
                        const std::string command(event->data, event->data_len);
                        if (deviceID.length() == NODE_ID_SIZE * 2)
                        {
                            // ESP_LOGI(TAG, "Received command %s for device %s", command.c_str(), deviceID.c_str());
                            if (topic_str.ends_with(MQTT_CLIENT_COMMAND_TOPIC))
                            {
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
                                else if (command.compare("IDENTIFY") == 0)
                                {
                                    mqttHelper->GetIoRtsManager()->mIoHome->IdentifyDevice(deviceID);
                                }
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
                            else if (topic_str.ends_with(MQTT_CLIENT_COMMAND_FAV_POS_TOPIC)) // favorite position button pressed
                            {
                                // ESP_LOGI(TAG, "Received 'set favorite position' for device %s", deviceID.c_str());
                                mqttHelper->GetIoRtsManager()->mIoHome->SetDeviceToFavoritePosition(deviceID);
                            }
                            else if (topic_str.ends_with(MQTT_CLIENT_COMMAND_TILT_TOPIC)) // it should be a tilt between 0 and 100
                            {
                                float tilt = strtof(command.c_str(), NULL);
                                if (tilt >= (float)0.0 && tilt <= (float)100.0)
                                {
                                    mqttHelper->GetIoRtsManager()->mIoHome->SetDeviceTilt(deviceID, (uint8_t)tilt);
                                }
                                else
                                    ESP_LOGE(TAG, "Received command %s for device %s -> invalid tilt!", command.c_str(), deviceID.c_str());
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
        mIsIoHomePassive = IoHomeConfig::isPassiveModeEnabled();
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
        // Controller and IO devices are separate HA devices.
        // IO devices link back to the controller via "via_device".
        SendControllerDiscovery();
        if (!mIsIoHomePassive)
            SendIoDevicesDiscovery();
    }
    void MqttHelpers::SendControllerDiscovery()
    {
        bool error = false;
        std::string clientId = MqttConfig::GetClientId();
        std::string availability_topic = mTopicPrefix + MQTT_CLIENT_BIRTH_WILL_TOPIC;
        const esp_app_desc_t *desc = esp_app_get_description();

        cJSON *discovery = cJSON_CreateObject();
        if (discovery == NULL)
            return;

        // "dev" section — the controller
        cJSON *dev = cJSON_AddObjectToObject(discovery, "dev");
        if (dev == NULL)
            error = true;
        else
        {
            error = error || (cJSON_AddStringToObject(dev, "ids", clientId.c_str()) == NULL);  // identifiers
            error = error || (cJSON_AddStringToObject(dev, "name", clientId.c_str()) == NULL); // name
            error = error || (cJSON_AddStringToObject(dev, "mf", "nicolas5000") == NULL);      // manufacturer
            error = error || (cJSON_AddStringToObject(dev, "sw", desc->version) == NULL);      // sw_version
        }

        // "o" (origin) section
        if (!error)
        {
            cJSON *o = cJSON_AddObjectToObject(discovery, "o");
            if (o == NULL)
                error = true;
            else
            {
                error = error || (cJSON_AddStringToObject(o, "name", clientId.c_str()) == NULL);                             // name
                error = error || (cJSON_AddStringToObject(o, "url", "https://github.com/nicolas5000/io-rts-esp32") == NULL); // support_url
                error = error || (cJSON_AddStringToObject(o, "sw", desc->version) == NULL);                                  // sw_version
            }
        }

        // "cmps" section — controller components only
        cJSON *cmps = NULL;
        if (!error)
        {
            cmps = cJSON_AddObjectToObject(discovery, "cmps");
            if (cmps == NULL)
                error = true;
        }
        if (!error)
        {
            // Add reboot button
            cJSON *cmp = cJSON_AddObjectToObject(cmps, "reboot");
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
        }
        if (!error)
        {
            // Add "IoLogging" switch https://www.home-assistant.io/integrations/switch.mqtt/
            cJSON *cmp = cJSON_AddObjectToObject(cmps, MQTT_CLIENT_IO_LOGGING_ID.c_str());
            if (cmp == NULL)
                error = true;
            else
            {
                error = error || (cJSON_AddStringToObject(cmp, "p", "switch") == NULL);                                  // platform
                error = error || (cJSON_AddStringToObject(cmp, "unique_id", MQTT_CLIENT_IO_LOGGING_ID.c_str()) == NULL); // unique_id
                error = error || (cJSON_AddStringToObject(cmp, "name", "Enable IO logging") == NULL);                    // name
                error = error || (cJSON_AddBoolToObject(cmp, "optimistic", true) == NULL);                               // optimistic
                std::string state_topic = mTopicPrefix + "/" + MQTT_CLIENT_CONFIG_IO_ID + MQTT_CLIENT_STATE_TOPIC;
                error = error || (cJSON_AddStringToObject(cmp, "state_topic", state_topic.c_str()) == NULL); // state_topic
                std::string value_template = "{{ value_json." + MQTT_CLIENT_IO_LOGGING_ID + " }}";
                error = error || (cJSON_AddStringToObject(cmp, "value_template", value_template.c_str()) == NULL); // value_template
                std::string command_topic = mTopicPrefix + "/" + MQTT_CLIENT_CONFIG_IO_ID + MQTT_CLIENT_COMMAND_TOPIC;
                error = error || (cJSON_AddStringToObject(cmp, "command_topic", command_topic.c_str()) == NULL); // command_topic
                std::string command_template = "{\"" + MQTT_CLIENT_IO_LOGGING_ID + "\": \"{{ value }}\"}";
                error = error || (cJSON_AddStringToObject(cmp, "command_template", command_template.c_str()) == NULL); // command_template
            }
        }
        if (!error)
        {
            // Add "IoPassive" switch https://www.home-assistant.io/integrations/switch.mqtt/
            cJSON *cmp = cJSON_AddObjectToObject(cmps, MQTT_CLIENT_IO_PASSIVE_ID.c_str());
            if (cmp == NULL)
                error = true;
            else
            {
                error = error || (cJSON_AddStringToObject(cmp, "p", "switch") == NULL);                                  // platform
                error = error || (cJSON_AddStringToObject(cmp, "unique_id", MQTT_CLIENT_IO_PASSIVE_ID.c_str()) == NULL); // unique_id
                error = error || (cJSON_AddStringToObject(cmp, "name", "Enable IO Passive mode") == NULL);               // name
                error = error || (cJSON_AddBoolToObject(cmp, "optimistic", true) == NULL);                               // optimistic
                std::string state_topic = mTopicPrefix + "/" + MQTT_CLIENT_CONFIG_IO_ID + MQTT_CLIENT_STATE_TOPIC;
                error = error || (cJSON_AddStringToObject(cmp, "state_topic", state_topic.c_str()) == NULL); // state_topic
                std::string value_template = "{{ value_json." + MQTT_CLIENT_IO_PASSIVE_ID + " }}";
                error = error || (cJSON_AddStringToObject(cmp, "value_template", value_template.c_str()) == NULL); // value_template
                std::string command_topic = mTopicPrefix + "/" + MQTT_CLIENT_CONFIG_IO_ID + MQTT_CLIENT_COMMAND_TOPIC;
                error = error || (cJSON_AddStringToObject(cmp, "command_topic", command_topic.c_str()) == NULL); // command_topic
                std::string command_template = "{\"" + MQTT_CLIENT_IO_PASSIVE_ID + "\": \"{{ value }}\"}";
                error = error || (cJSON_AddStringToObject(cmp, "command_template", command_template.c_str()) == NULL); // command_template
            }
        }
        if (!error)
        {
            // Add "IoTxPower" number https://www.home-assistant.io/integrations/number.mqtt/
            cJSON *cmp = cJSON_AddObjectToObject(cmps, MQTT_CLIENT_IO_TX_POWER_ID.c_str());
            if (cmp == NULL)
                error = true;
            else
            {
                error = error || (cJSON_AddStringToObject(cmp, "p", "number") == NULL);                                   // platform
                error = error || (cJSON_AddStringToObject(cmp, "unique_id", MQTT_CLIENT_IO_TX_POWER_ID.c_str()) == NULL); // unique_id
                error = error || (cJSON_AddStringToObject(cmp, "name", "IO Tx Power") == NULL);                           // name
                error = error || (cJSON_AddBoolToObject(cmp, "optimistic", true) == NULL);                                // optimistic
                error = error || (cJSON_AddNumberToObject(cmp, "min", 0) == NULL);                                        // min
                error = error || (cJSON_AddNumberToObject(cmp, "max", 20) == NULL);                                       // max
                std::string state_topic = mTopicPrefix + "/" + MQTT_CLIENT_CONFIG_IO_ID + MQTT_CLIENT_STATE_TOPIC;
                error = error || (cJSON_AddStringToObject(cmp, "state_topic", state_topic.c_str()) == NULL); // state_topic
                std::string value_template = "{{ value_json." + MQTT_CLIENT_IO_TX_POWER_ID + " }}";
                error = error || (cJSON_AddStringToObject(cmp, "value_template", value_template.c_str()) == NULL); // value_template
                std::string command_topic = mTopicPrefix + "/" + MQTT_CLIENT_CONFIG_IO_ID + MQTT_CLIENT_COMMAND_TOPIC;
                error = error || (cJSON_AddStringToObject(cmp, "command_topic", command_topic.c_str()) == NULL); // command_topic
                std::string command_template = "{\"" + MQTT_CLIENT_IO_TX_POWER_ID + "\": {{ value }} }";
                error = error || (cJSON_AddStringToObject(cmp, "command_template", command_template.c_str()) == NULL); // command_template
            }
        }
        if (!error && !mIsIoHomePassive)
        {
            // Add 'Discover' button
            cJSON *cmp = cJSON_AddObjectToObject(cmps, "discover");
            if (cmp == NULL)
                error = true;
            else
            {
                error = error || (cJSON_AddStringToObject(cmp, "p", "button") == NULL);                                // platform
                error = error || (cJSON_AddStringToObject(cmp, "unique_id", MQTT_CLIENT_DISCOVER_ID.c_str()) == NULL); // unique_id
                error = error || (cJSON_AddStringToObject(cmp, "name", "Discover IO device") == NULL);                 // name
                std::string discover_topic = mTopicPrefix + "/" + MQTT_CLIENT_DISCOVER_ID + MQTT_CLIENT_COMMAND_TOPIC;
                error = error || (cJSON_AddStringToObject(cmp, "command_topic", discover_topic.c_str()) == NULL); // command_topic
            }
            // Add "AddIoDevice" text https://www.home-assistant.io/integrations/text.mqtt/
            cmp = cJSON_AddObjectToObject(cmps, MQTT_CLIENT_ADD_DEVICE_ID.c_str());
            if (cmp == NULL)
                error = true;
            else
            {
                error = error || (cJSON_AddStringToObject(cmp, "p", "text") == NULL);                                    // platform
                error = error || (cJSON_AddStringToObject(cmp, "unique_id", MQTT_CLIENT_ADD_DEVICE_ID.c_str()) == NULL); // unique_id
                error = error || (cJSON_AddStringToObject(cmp, "name", "Add IO device") == NULL);                        // name
                std::string command_topic = mTopicPrefix + "/" + MQTT_CLIENT_MANAGE_IO_ID + MQTT_CLIENT_COMMAND_TOPIC;
                error = error || (cJSON_AddStringToObject(cmp, "command_topic", command_topic.c_str()) == NULL); // command_topic
                std::string command_template = "{\"" + MQTT_CLIENT_ADD_DEVICE_ID + "\": \"{{ value }}\"}";
                error = error || (cJSON_AddStringToObject(cmp, "command_template", command_template.c_str()) == NULL); // command_template
            }
            // Add "RemoveIoDevice" text https://www.home-assistant.io/integrations/text.mqtt/
            cmp = cJSON_AddObjectToObject(cmps, MQTT_CLIENT_REM_DEVICE_ID.c_str());
            if (cmp == NULL)
                error = true;
            else
            {
                error = error || (cJSON_AddStringToObject(cmp, "p", "text") == NULL);                                    // platform
                error = error || (cJSON_AddStringToObject(cmp, "unique_id", MQTT_CLIENT_REM_DEVICE_ID.c_str()) == NULL); // unique_id
                error = error || (cJSON_AddStringToObject(cmp, "name", "Remove IO device") == NULL);                     // name
                std::string command_topic = mTopicPrefix + "/" + MQTT_CLIENT_MANAGE_IO_ID + MQTT_CLIENT_COMMAND_TOPIC;
                error = error || (cJSON_AddStringToObject(cmp, "command_topic", command_topic.c_str()) == NULL); // command_topic
                std::string command_template = "{\"" + MQTT_CLIENT_REM_DEVICE_ID + "\": \"{{ value }}\"}";
                error = error || (cJSON_AddStringToObject(cmp, "command_template", command_template.c_str()) == NULL); // command_template
            }
            // Add "InvertIoDevice" text https://www.home-assistant.io/integrations/text.mqtt/
            cmp = cJSON_AddObjectToObject(cmps, MQTT_CLIENT_INV_DEVICE_ID.c_str());
            if (cmp == NULL)
                error = true;
            else
            {
                error = error || (cJSON_AddStringToObject(cmp, "p", "text") == NULL);                                    // platform
                error = error || (cJSON_AddStringToObject(cmp, "unique_id", MQTT_CLIENT_INV_DEVICE_ID.c_str()) == NULL); // unique_id
                error = error || (cJSON_AddStringToObject(cmp, "name", "Invert IO device") == NULL);                     // name
                std::string command_topic = mTopicPrefix + "/" + MQTT_CLIENT_MANAGE_IO_ID + MQTT_CLIENT_COMMAND_TOPIC;
                error = error || (cJSON_AddStringToObject(cmp, "command_topic", command_topic.c_str()) == NULL); // command_topic
                std::string command_template = "{\"" + MQTT_CLIENT_INV_DEVICE_ID + "\": \"{{ value }}\"}";
                error = error || (cJSON_AddStringToObject(cmp, "command_template", command_template.c_str()) == NULL); // command_template
            }
            // Add "LinkIoRemote" text https://www.home-assistant.io/integrations/text.mqtt/
            cmp = cJSON_AddObjectToObject(cmps, MQTT_CLIENT_LINK_REMOTE_ID.c_str());
            if (cmp == NULL)
                error = true;
            else
            {
                error = error || (cJSON_AddStringToObject(cmp, "p", "text") == NULL);                                     // platform
                error = error || (cJSON_AddStringToObject(cmp, "unique_id", MQTT_CLIENT_LINK_REMOTE_ID.c_str()) == NULL); // unique_id
                error = error || (cJSON_AddStringToObject(cmp, "name", "Link IO device to remote") == NULL);              // name
                std::string command_topic = mTopicPrefix + "/" + MQTT_CLIENT_MANAGE_IO_ID + MQTT_CLIENT_COMMAND_TOPIC;
                error = error || (cJSON_AddStringToObject(cmp, "command_topic", command_topic.c_str()) == NULL); // command_topic
                std::string command_template = "{\"" + MQTT_CLIENT_LINK_REMOTE_ID + "\": \"{{ value }}\"}";
                error = error || (cJSON_AddStringToObject(cmp, "command_template", command_template.c_str()) == NULL); // command_template
            }
            // Add "RemoveIoRemote" text https://www.home-assistant.io/integrations/text.mqtt/
            cmp = cJSON_AddObjectToObject(cmps, MQTT_CLIENT_REM_REMOTE_ID.c_str());
            if (cmp == NULL)
                error = true;
            else
            {
                error = error || (cJSON_AddStringToObject(cmp, "p", "text") == NULL);                                    // platform
                error = error || (cJSON_AddStringToObject(cmp, "unique_id", MQTT_CLIENT_REM_REMOTE_ID.c_str()) == NULL); // unique_id
                error = error || (cJSON_AddStringToObject(cmp, "name", "Remove IO remote") == NULL);                     // name
                std::string command_topic = mTopicPrefix + "/" + MQTT_CLIENT_MANAGE_IO_ID + MQTT_CLIENT_COMMAND_TOPIC;
                error = error || (cJSON_AddStringToObject(cmp, "command_topic", command_topic.c_str()) == NULL); // command_topic
                std::string command_template = "{\"" + MQTT_CLIENT_REM_REMOTE_ID + "\": \"{{ value }}\"}";
                error = error || (cJSON_AddStringToObject(cmp, "command_template", command_template.c_str()) == NULL); // command_template
            }
            // Add "SetIoDeviceName" text https://www.home-assistant.io/integrations/text.mqtt/
            cmp = cJSON_AddObjectToObject(cmps, MQTT_CLIENT_SET_DEVICE_NAME_ID.c_str());
            if (cmp == NULL)
                error = true;
            else
            {
                error = error || (cJSON_AddStringToObject(cmp, "p", "text") == NULL);                                         // platform
                error = error || (cJSON_AddStringToObject(cmp, "unique_id", MQTT_CLIENT_SET_DEVICE_NAME_ID.c_str()) == NULL); // unique_id
                error = error || (cJSON_AddStringToObject(cmp, "name", "Change IO device name") == NULL);                     // name
                std::string command_topic = mTopicPrefix + "/" + MQTT_CLIENT_MANAGE_IO_ID + MQTT_CLIENT_COMMAND_TOPIC;
                error = error || (cJSON_AddStringToObject(cmp, "command_topic", command_topic.c_str()) == NULL); // command_topic
                std::string command_template = "{\"" + MQTT_CLIENT_SET_DEVICE_NAME_ID + "\": \"{{ value }}\"}";
                error = error || (cJSON_AddStringToObject(cmp, "command_template", command_template.c_str()) == NULL); // command_template
            }
        }

        // Shared availability
        error = error || (cJSON_AddStringToObject(discovery, "availability_topic", availability_topic.c_str()) == NULL);

        // Publish controller discovery
        if (!error)
        {
            std::string topic = mDiscoveryPrefix + MQTT_CLIENT_DISCOVERY_TOPIC;
            const char *data = cJSON_Print(discovery);
            if (data == NULL)
            {
                ESP_LOGE(TAG, "Failed to create controller discovery string");
            }
            else
            {
                esp_mqtt_client_publish(mMqttClientHandle, topic.c_str(), data, 0, 0, 1);
                cJSON_free((void *)data);
                ESP_LOGI(TAG, "Sent controller discovery successfully");
            }
        }
        cJSON_Delete(discovery);
    }
    void MqttHelpers::SendIoDevicesDiscovery()
    {
        std::string clientId = MqttConfig::GetClientId();
        std::string availability_topic = mTopicPrefix + MQTT_CLIENT_BIRTH_WILL_TOPIC;
        const esp_app_desc_t *desc = esp_app_get_description();

        // Compute the discovery base path (e.g. "homeassistant/device") from mDiscoveryPrefix
        // mDiscoveryPrefix is like "homeassistant/device/io-rts", we need "homeassistant/device"
        std::string discoveryBase = mDiscoveryPrefix;
        size_t lastSlash = discoveryBase.rfind('/');
        if (lastSlash != std::string::npos)
            discoveryBase = discoveryBase.substr(0, lastSlash);

        std::lock_guard<std::mutex> guard(mIoRtsManager->mIoDevicesMutex);
        for (auto it = mIoRtsManager->mIoDevices.begin(); it != mIoRtsManager->mIoDevices.end(); ++it)
        {
            std::string device_id = MQTT_CLIENT_PREFIX_IO + it->first;

            // For deleted devices, send empty payload to remove the device discovery
            if (it->second.is_deleted)
            {
                std::string topic = discoveryBase + "/" + device_id + MQTT_CLIENT_DISCOVERY_TOPIC;
                esp_mqtt_client_publish(mMqttClientHandle, topic.c_str(), NULL, 0, 0, 1);
                continue;
            }

            bool error = false;
            cJSON *discovery = cJSON_CreateObject();
            if (discovery == NULL)
                continue;

            // "dev" section — this IO device, linked to controller via via_device
            cJSON *dev = cJSON_AddObjectToObject(discovery, "dev");
            if (dev == NULL)
                error = true;
            else
            {
                error = error || (cJSON_AddStringToObject(dev, "ids", device_id.c_str()) == NULL);                                         // identifiers
                error = error || (cJSON_AddStringToObject(dev, "name", it->second.info.name) == NULL);                                     // name
                error = error || (cJSON_AddStringToObject(dev, "mf", IoDeviceManufacturer(it->second.info.manufacturer).c_str()) == NULL); // manufacturer
                error = error || (cJSON_AddStringToObject(dev, "sn", it->first.c_str()) == NULL);                                          // serial_number (node address)
                error = error || (cJSON_AddStringToObject(dev, "via_device", clientId.c_str()) == NULL);                                   // via_device — links to controller
            }

            // "o" (origin) section
            if (!error)
            {
                cJSON *o = cJSON_AddObjectToObject(discovery, "o");
                if (o == NULL)
                    error = true;
                else
                {
                    error = error || (cJSON_AddStringToObject(o, "name", clientId.c_str()) == NULL);                             // name
                    error = error || (cJSON_AddStringToObject(o, "url", "https://github.com/nicolas5000/io-rts-esp32") == NULL); // support_url
                    error = error || (cJSON_AddStringToObject(o, "sw", desc->version) == NULL);                                  // sw_version
                }
            }

            // "cmps" section — device components
            cJSON *cmps = NULL;
            if (!error)
            {
                cmps = cJSON_AddObjectToObject(discovery, "cmps");
                if (cmps == NULL)
                    error = true;
            }

            if (!error)
            {
                std::string device_id_fav = MQTT_CLIENT_PREFIX_IO + it->first + MQTT_CLIENT_SUFFIX_FAV_IO;
                std::string device_cmd_topic = GetTopicPrefix() + "/" + device_id + MQTT_CLIENT_COMMAND_TOPIC;
                std::string device_state_topic = GetTopicPrefix() + "/" + device_id + MQTT_CLIENT_STATE_TOPIC;
                std::string device_position_topic = GetTopicPrefix() + "/" + device_id + MQTT_CLIENT_POSITION_TOPIC;
                std::string device_cmd_position_topic = GetTopicPrefix() + "/" + device_id + MQTT_CLIENT_COMMAND_POSITION_TOPIC;
                std::string device_tilt_topic = GetTopicPrefix() + "/" + device_id + MQTT_CLIENT_TILT_TOPIC;
                std::string device_cmd_tilt_topic = GetTopicPrefix() + "/" + device_id + MQTT_CLIENT_COMMAND_TILT_TOPIC;
                std::string device_cmd_fav_pos_topic = GetTopicPrefix() + "/" + device_id + MQTT_CLIENT_COMMAND_FAV_POS_TOPIC;
                std::string device_platform;

                cJSON *cmp = cJSON_AddObjectToObject(cmps, device_id.c_str());
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
                        device_platform = "cover";
                        if (it->second.info.is_openclose_inverted)
                        {
                            error = error || (cJSON_AddNumberToObject(cmp, "position_closed", 0) == NULL); // position_closed
                            error = error || (cJSON_AddNumberToObject(cmp, "position_open", 100) == NULL); // position_open
                        }
                        else
                        {
                            error = error || (cJSON_AddNumberToObject(cmp, "position_closed", 100) == NULL); // position_closed
                            error = error || (cJSON_AddNumberToObject(cmp, "position_open", 0) == NULL);     // position_open
                        }
                        error = error || (cJSON_AddStringToObject(cmp, "position_topic", device_position_topic.c_str()) == NULL);         // position_topic
                        error = error || (cJSON_AddStringToObject(cmp, "set_position_topic", device_cmd_position_topic.c_str()) == NULL); // set_position_topic
                        // add tilt topics if device supports tilt
                        if (iohome::deviceTypeSupportsTilt(it->second.info.device_type))
                        {
                            error = error || (cJSON_AddStringToObject(cmp, "tilt_status_topic", device_tilt_topic.c_str()) == NULL);      // tilt_status_topic
                            error = error || (cJSON_AddStringToObject(cmp, "tilt_command_topic", device_cmd_tilt_topic.c_str()) == NULL); // tilt_command_topic
                            error = error || (cJSON_AddNumberToObject(cmp, "tilt_closed_value", 0) == NULL);                              // tilt_closed_value
                            error = error || (cJSON_AddNumberToObject(cmp, "tilt_opened_value", 100) == NULL);                            // tilt_opened_value
                            error = error || (cJSON_AddNumberToObject(cmp, "tilt_min", 0) == NULL);                                       // tilt_min
                            error = error || (cJSON_AddNumberToObject(cmp, "tilt_max", 100) == NULL);                                     // tilt_max
                        }
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
                        // add "favorite position" button as a separate component
                        if (!error)
                        {
                            cJSON *fav = cJSON_AddObjectToObject(cmps, device_id_fav.c_str());
                            if (fav == NULL)
                                error = true;
                            else
                            {
                                error = error || (cJSON_AddStringToObject(fav, "p", "button") == NULL);                      // platform
                                error = error || (cJSON_AddStringToObject(fav, "unique_id", device_id_fav.c_str()) == NULL); // unique_id
                                std::string favName = it->second.info.name + std::string(" Favorite position");
                                error = error || (cJSON_AddStringToObject(fav, "name", favName.c_str()) == NULL);                           // name
                                error = error || (cJSON_AddStringToObject(fav, "command_topic", device_cmd_fav_pos_topic.c_str()) == NULL); // command_topic
                            }
                        }
                        break;
                    }
                    case DeviceType::LIGHT:
                        // https://www.home-assistant.io/integrations/light.mqtt/
                        device_platform = "light";
                        error = error || (cJSON_AddStringToObject(cmp, "optimistic", "true") == NULL); // optimistic
                        // TODO: manage brightness, waiting for feedback
                        break;
                    case DeviceType::ON_OFF_SWITCH:
                        // https://www.home-assistant.io/integrations/switch.mqtt/
                        device_platform = "switch";
                        error = error || (cJSON_AddStringToObject(cmp, "optimistic", "true") == NULL); // optimistic
                        break;
                    case DeviceType::LOCK:
                        // https://www.home-assistant.io/integrations/lock.mqtt/
                        device_platform = "lock";
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
                        ESP_LOGE(TAG, "Failed to add device to MQTT discovery: type not managed! (%d)", it->second.info.device_type);
                        cJSON_Delete(discovery);
                        continue; // skip this device entirely
                    }
                    // Common fields for all supported device types
                    error = error || (cJSON_AddStringToObject(cmp, "p", device_platform.c_str()) == NULL);              // platform
                    error = error || (cJSON_AddStringToObject(cmp, "unique_id", device_id.c_str()) == NULL);            // unique_id
                    error = error || (cJSON_AddStringToObject(cmp, "name", it->second.info.name) == NULL);              // name
                    error = error || (cJSON_AddStringToObject(cmp, "command_topic", device_cmd_topic.c_str()) == NULL); // command_topic
                    error = error || (cJSON_AddStringToObject(cmp, "state_topic", device_state_topic.c_str()) == NULL); // state_topic
                }

                // add "identify" button as a separate component for all device types
                if (!error)
                {
                    std::string device_id_ident = MQTT_CLIENT_PREFIX_IO + it->first + MQTT_CLIENT_SUFFIX_IDENTIFY;
                    std::string device_cmd_topic = GetTopicPrefix() + "/" + device_id + MQTT_CLIENT_COMMAND_TOPIC;
                    cJSON *ident = cJSON_AddObjectToObject(cmps, device_id_ident.c_str());
                    if (ident == NULL)
                        error = true;
                    else
                    {
                        error = error || (cJSON_AddStringToObject(ident, "p", "button") == NULL);                        // platform
                        error = error || (cJSON_AddStringToObject(ident, "unique_id", device_id_ident.c_str()) == NULL); // unique_id
                        std::string identName = it->second.info.name + std::string(" Identify");
                        error = error || (cJSON_AddStringToObject(ident, "name", identName.c_str()) == NULL);                   // name
                        error = error || (cJSON_AddStringToObject(ident, "command_topic", device_cmd_topic.c_str()) == NULL);    // command_topic — uses /set
                        error = error || (cJSON_AddStringToObject(ident, "payload_press", "IDENTIFY") == NULL);                 // payload_press
                        error = error || (cJSON_AddStringToObject(ident, "device_class", "identify") == NULL);                  // device_class
                        error = error || (cJSON_AddStringToObject(ident, "entity_category", "diagnostic") == NULL);             // entity_category
                    }
                }
            }

            // Shared availability — uses controller's status topic
            error = error || (cJSON_AddStringToObject(discovery, "availability_topic", availability_topic.c_str()) == NULL);

            // Publish this IO device's discovery
            if (!error)
            {
                std::string topic = discoveryBase + "/" + device_id + MQTT_CLIENT_DISCOVERY_TOPIC;
                const char *data = cJSON_Print(discovery);
                if (data == NULL)
                {
                    ESP_LOGE(TAG, "Failed to create IO device discovery string for %s", it->first.c_str());
                }
                else
                {
                    esp_mqtt_client_publish(mMqttClientHandle, topic.c_str(), data, 0, 0, 1);
                    cJSON_free((void *)data);
                    ESP_LOGD(TAG, "Sent IO device discovery for %s", it->first.c_str());
                }
            }
            cJSON_Delete(discovery);
        }
        ESP_LOGI(TAG, "Sent all IO device discoveries");
    }
    void MqttHelpers::SendIoDeviceStatus(const std::string &deviceId)
    {
        if (mIsIoHomePassive)
            return; // don't send status if in passive mode

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
                if (iohome::deviceTypeSupportsTilt(device->second.info.device_type))
                {
                    std::string tiltTopic = GetTopicPrefix() + "/" + MQTT_CLIENT_PREFIX_IO + device->first + MQTT_CLIENT_TILT_TOPIC;
                    esp_mqtt_client_publish(mMqttClientHandle, tiltTopic.c_str(), NULL, 0, 0, 1);
                }
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
                        if (device->second.info.is_openclose_inverted)
                        {
                            if (device->second.position < (float)0.1)
                                data = "closed";
                            else
                                data = "open";
                        }
                        else
                        {
                            if (device->second.position > (float)99.9)
                                data = "closed";
                            else
                                data = "open";
                        }
                    }
                    else
                    {
                        if (device->second.position > device->second.target)
                            data = device->second.info.is_openclose_inverted ? "closing" : "opening";
                        else
                            data = device->second.info.is_openclose_inverted ? "opening" : "closing";
                    }
                    esp_mqtt_client_publish(mMqttClientHandle, stateTopic.c_str(), data.c_str(), 0, 0, 1);
                    // send tilt if device supports it
                    if (iohome::deviceTypeSupportsTilt(device->second.info.device_type) && device->second.tilt != UNKNOWN_POSITION)
                    {
                        std::string tiltTopic = GetTopicPrefix() + "/" + MQTT_CLIENT_PREFIX_IO + device->first + MQTT_CLIENT_TILT_TOPIC;
                        data = std::to_string((int)device->second.tilt);
                        esp_mqtt_client_publish(mMqttClientHandle, tiltTopic.c_str(), data.c_str(), 0, 0, 1);
                    }
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
    void MqttHelpers::SendLog(const std::string &log)
    {
        std::string topic = mTopicPrefix + MQTT_CLIENT_LOG_TOPIC;
        esp_mqtt_client_publish(mMqttClientHandle, topic.c_str(), log.c_str(), 0, 0, 0);
    }
}