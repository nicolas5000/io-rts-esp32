#include "MqttConfig.hpp"
#include "NvsHelpers.hpp"

#include "sdkconfig.h"
#include "nvs.h"

static const std::string MQTT_CONFIG_NAMESPACE = "mqtt";                  // namespace to group MQTT configuration in NVS
static const std::string MQTT_CONFIG_ENABLE = "mqtt_enabled";             // Key to store MQTT status (uint8_t), 0 if MQTT disabled
static const std::string MQTT_CONFIG_BROKER_ADDRESS = "broker_addr";      // Key to store MQTT broker address (string)
static const std::string MQTT_CONFIG_BROKER_PORT = "broker_port";         // Key to store MQTT broker port (uint16_t)
static const std::string MQTT_CONFIG_CLIENT_ID = "client_id";             // Key to store MQTT status (string)
static const std::string MQTT_CONFIG_CLIENT_USERMANE = "client_username"; // Key to store MQTT status (string)
static const std::string MQTT_CONFIG_CLIENT_PASSWORD = "client_password"; // Key to store MQTT status (string)
static const std::string MQTT_CONFIG_TLS_ENABLE = "tls_enabled";          // Key to store MQTT status (uint8_t), 0 if TLS disabled
static const std::string MQTT_CONFIG_BROKER_CERT = "broker_cert";         // Key to store MQTT status (string)
static const std::string MQTT_CONFIG_TOPIC_PREFIX = "topic_prefix";       // Key to store MQTT status (string)
static const std::string MQTT_CONFIG_DISCOVERY_PREFIX = "disc_prefix";    // Key to store MQTT status (string)

using namespace Helpers;

namespace Config
{
    void MqttConfig::DeleteMqttConfig()
    {
        NvsHelpers::DeleteValue(MQTT_CONFIG_NAMESPACE, MQTT_CONFIG_ENABLE);
        NvsHelpers::DeleteValue(MQTT_CONFIG_NAMESPACE, MQTT_CONFIG_BROKER_ADDRESS);
        NvsHelpers::DeleteValue(MQTT_CONFIG_NAMESPACE, MQTT_CONFIG_BROKER_PORT);
        NvsHelpers::DeleteValue(MQTT_CONFIG_NAMESPACE, MQTT_CONFIG_CLIENT_ID);
        NvsHelpers::DeleteValue(MQTT_CONFIG_NAMESPACE, MQTT_CONFIG_CLIENT_USERMANE);
        NvsHelpers::DeleteValue(MQTT_CONFIG_NAMESPACE, MQTT_CONFIG_CLIENT_PASSWORD);
        NvsHelpers::DeleteValue(MQTT_CONFIG_NAMESPACE, MQTT_CONFIG_TLS_ENABLE);
        NvsHelpers::DeleteValue(MQTT_CONFIG_NAMESPACE, MQTT_CONFIG_BROKER_CERT);
        NvsHelpers::DeleteValue(MQTT_CONFIG_NAMESPACE, MQTT_CONFIG_TOPIC_PREFIX);
        NvsHelpers::DeleteValue(MQTT_CONFIG_NAMESPACE, MQTT_CONFIG_DISCOVERY_PREFIX);
    }
    bool MqttConfig::isEnabled()
    {
#ifdef CONFIG_MQTT_CLIENT_ENABLE
        uint8_t is_enabled = 1;
#else
        uint8_t is_enabled = 0;
#endif
        esp_err_t err = NvsHelpers::GetValue(MQTT_CONFIG_NAMESPACE, MQTT_CONFIG_ENABLE, is_enabled);
        if (err == ESP_ERR_NVS_NOT_FOUND && GetBrokerAddress().empty()) {
            // First run: no broker configured — write disabled so the UI shows MQTT as off
            NvsHelpers::SetValue(MQTT_CONFIG_NAMESPACE, MQTT_CONFIG_ENABLE, (uint8_t)0);
            return false;
        }
        return is_enabled;
    }
    esp_err_t MqttConfig::Activate(bool mqttEnabled)
    {
        uint8_t is_enabled = mqttEnabled ? 0x01 : 0x00;
        return NvsHelpers::SetValue(MQTT_CONFIG_NAMESPACE, MQTT_CONFIG_ENABLE, is_enabled);
    }
    const std::string MqttConfig::GetBrokerAddress()
    {
        std::string addr = CONFIG_MQTT_BROKER_ADDRESS;
        NvsHelpers::GetString(MQTT_CONFIG_NAMESPACE, MQTT_CONFIG_BROKER_ADDRESS, addr);
        return addr;
    }
    esp_err_t MqttConfig::SetBrokerAddress(const std::string &address)
    {
        return NvsHelpers::SetString(MQTT_CONFIG_NAMESPACE, MQTT_CONFIG_BROKER_ADDRESS, address);
    }
    uint16_t MqttConfig::GetBrokerPort()
    {
        uint16_t port = CONFIG_MQTT_BROKER_PORT;
        NvsHelpers::GetValue(MQTT_CONFIG_NAMESPACE, MQTT_CONFIG_BROKER_PORT, port);
        return port;
    }
    esp_err_t MqttConfig::SetBrokerPort(const uint16_t port)
    {
        if (port == 0)
        {
            return ESP_ERR_INVALID_ARG;
        }
        return NvsHelpers::SetValue(MQTT_CONFIG_NAMESPACE, MQTT_CONFIG_BROKER_PORT, static_cast<uint16_t>(port));
    }
    const std::string MqttConfig::GetClientId()
    {
        std::string id = CONFIG_MQTT_CLIENT_ID;
        NvsHelpers::GetString(MQTT_CONFIG_NAMESPACE, MQTT_CONFIG_CLIENT_ID, id);
        return id;
    }
    esp_err_t MqttConfig::SetClientId(const std::string &id)
    {
        return NvsHelpers::SetString(MQTT_CONFIG_NAMESPACE, MQTT_CONFIG_CLIENT_ID, id);
    }
    const std::string MqttConfig::GetClientUsername()
    {
        std::string name = CONFIG_MQTT_CLIENT_USERNAME;
        NvsHelpers::GetString(MQTT_CONFIG_NAMESPACE, MQTT_CONFIG_CLIENT_USERMANE, name);
        return name;
    }
    esp_err_t MqttConfig::SetClientUsername(const std::string &username)
    {
        return NvsHelpers::SetString(MQTT_CONFIG_NAMESPACE, MQTT_CONFIG_CLIENT_USERMANE, username);
    }
    const std::string MqttConfig::GetClientPassword()
    {
        std::string password = CONFIG_MQTT_CLIENT_PASSWORD;
        NvsHelpers::GetString(MQTT_CONFIG_NAMESPACE, MQTT_CONFIG_CLIENT_PASSWORD, password);
        return password;
    }
    esp_err_t MqttConfig::SetClientPassword(const std::string &password)
    {
        return NvsHelpers::SetString(MQTT_CONFIG_NAMESPACE, MQTT_CONFIG_CLIENT_PASSWORD, password);
    }
    bool MqttConfig::isTLSEnabled()
    {
#ifdef CONFIG_MQTT_CLIENT_TLS_ENABLED
        uint8_t is_enabled = 1;
#else
        uint8_t is_enabled = 0;
#endif
        NvsHelpers::GetValue(MQTT_CONFIG_NAMESPACE, MQTT_CONFIG_TLS_ENABLE, is_enabled);
        return is_enabled;
    }
    esp_err_t MqttConfig::EnableTLS(bool tlsEnabled)
    {
        uint8_t is_enabled = tlsEnabled ? 0x01 : 0x00;
        return NvsHelpers::SetValue(MQTT_CONFIG_NAMESPACE, MQTT_CONFIG_TLS_ENABLE, is_enabled);
    }
    const std::string MqttConfig::GetBrokerCertificate()
    {
        std::string name = CONFIG_MQTT_BROKER_CERTIFICATE;
        NvsHelpers::GetString(MQTT_CONFIG_NAMESPACE, MQTT_CONFIG_BROKER_CERT, name);
        return name;
    }
    esp_err_t MqttConfig::SetBrokerCertificate(const std::string &cert)
    {
        return NvsHelpers::SetString(MQTT_CONFIG_NAMESPACE, MQTT_CONFIG_BROKER_CERT, cert);
    }
    const std::string MqttConfig::GetTopicPrefix()
    {
        std::string name = CONFIG_MQTT_TOPIC_PREFIX;
        NvsHelpers::GetString(MQTT_CONFIG_NAMESPACE, MQTT_CONFIG_TOPIC_PREFIX, name);
        return name;
    }
    esp_err_t MqttConfig::SetTopicPrefix(const std::string &prefix)
    {
        return NvsHelpers::SetString(MQTT_CONFIG_NAMESPACE, MQTT_CONFIG_TOPIC_PREFIX, prefix);
    }
    const std::string MqttConfig::GetDiscoveryPrefix()
    {
        std::string name = CONFIG_MQTT_DISCOVERY_PREFIX;
        NvsHelpers::GetString(MQTT_CONFIG_NAMESPACE, MQTT_CONFIG_DISCOVERY_PREFIX, name);
        return name;
    }
    esp_err_t MqttConfig::SetDiscoveryPrefix(const std::string &prefix)
    {
        return NvsHelpers::SetString(MQTT_CONFIG_NAMESPACE, MQTT_CONFIG_DISCOVERY_PREFIX, prefix);
    }
}