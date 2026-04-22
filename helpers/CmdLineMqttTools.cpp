#include "CmdLineManagement.hpp"
#include "MqttConfig.hpp"

#include <string.h>
#include "argtable3/argtable3.h"
#include "esp_console.h"
#include "esp_log.h"

using namespace Config;

static const char *TAG = "cmdline_mngt";

// ******************* MQTT CONFIG ********************

/// @brief Structure used by the 'config_mqtt' command
static struct
{
    struct arg_lit *read;
    struct arg_lit *del;
    struct arg_int *mqtt_state;
    struct arg_str *broker_addr;
    struct arg_int *broker_port;
    struct arg_str *client_id;
    struct arg_str *client_username;
    struct arg_str *client_password;
    struct arg_int *tls_enabled;
    struct arg_str *broker_cert;
    struct arg_str *topic_prefix;
    struct arg_str *discovery_prefix;
    struct arg_end *end;
} config_mqtt_args;

static int do_config_mqtt_cmd(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&config_mqtt_args);
    if (nerrors != 0)
    {
        arg_print_errors(stderr, config_mqtt_args.end, argv[0]);
        return 1;
    }
    if (config_mqtt_args.read->count > 0)
    {
        // Read MQTT configuration
        ESP_LOGI(TAG, "MQTT status: %s", MqttConfig::isEnabled() ? "enabled" : "disabled");
        ESP_LOGI(TAG, "Broker address: %s", MqttConfig::GetBrokerAddress().c_str());
        ESP_LOGI(TAG, "Broker port: %d", MqttConfig::GetBrokerPort());
        ESP_LOGI(TAG, "Client ID: %s", MqttConfig::GetClientId().c_str());
        ESP_LOGI(TAG, "Client username: %s", MqttConfig::GetClientUsername().c_str());
        ESP_LOGI(TAG, "Client password: %s", MqttConfig::GetClientPassword().c_str());
        ESP_LOGI(TAG, "TLS status: %s", MqttConfig::isTLSEnabled() ? "enabled" : "disabled");
        ESP_LOGI(TAG, "Broker cert: %s", MqttConfig::GetBrokerCertificate().c_str());
        ESP_LOGI(TAG, "Topic prefix: %s", MqttConfig::GetTopicPrefix().c_str());
        ESP_LOGI(TAG, "Discovery prefix: %s", MqttConfig::GetDiscoveryPrefix().c_str());
    }
    else if (config_mqtt_args.del->count > 0)
    {
        MqttConfig::DeleteMqttConfig();
        ESP_LOGI(TAG, "MQTT configuration restored to default values. New configuration will be applied after reboot!");
    }
    else
    {
        esp_err_t err;
        // Set configuration
        if (config_mqtt_args.mqtt_state->count > 0)
        {
            err = MqttConfig::Activate(config_mqtt_args.mqtt_state->ival[0] != 0);
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to set MQTT state to configuration storage! (%d)", err);
            }
            else
            {
                ESP_LOGI(TAG, "MQTT state set to configuration storage: %s", MqttConfig::isEnabled() ? "enabled" : "disabled");
            }
        }
        if (config_mqtt_args.broker_addr->count > 0)
        {
            err = MqttConfig::SetBrokerAddress(config_mqtt_args.broker_addr->sval[0]);
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to set broker address to configuration storage! (%d)", err);
            }
            else
            {
                ESP_LOGI(TAG, "Broker address set to configuration storage: %s", MqttConfig::GetBrokerAddress().c_str());
            }
        }
        if (config_mqtt_args.broker_port->count > 0)
        {
            if (config_mqtt_args.broker_port->ival[0] < 1 || config_mqtt_args.broker_port->ival[0] > 65535)
            {
                ESP_LOGE(TAG, "Invalid value for broker port, must be between 1 and 65565! (provided %d)", config_mqtt_args.broker_port->ival[0]);
            }
            else
            {
                err = MqttConfig::SetBrokerPort(config_mqtt_args.broker_port->ival[0]);
                if (err != ESP_OK)
                {
                    ESP_LOGE(TAG, "Failed to set broker port to configuration storage! (%d)", err);
                }
                else
                {
                    ESP_LOGI(TAG, "Broker port set to configuration storage: %d", MqttConfig::GetBrokerPort());
                }
            }
        }
        if (config_mqtt_args.client_id->count > 0)
        {
            err = MqttConfig::SetClientId(config_mqtt_args.client_id->sval[0]);
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to set client ID to configuration storage! (%d)", err);
            }
            else
            {
                ESP_LOGI(TAG, "Client ID set to configuration storage: %s", MqttConfig::GetClientId().c_str());
            }
        }
        if (config_mqtt_args.client_username->count > 0)
        {
            err = MqttConfig::SetClientUsername(config_mqtt_args.client_username->sval[0]);
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to set client username to configuration storage! (%d)", err);
            }
            else
            {
                ESP_LOGI(TAG, "Client username set to configuration storage: %s", MqttConfig::GetClientUsername().c_str());
            }
        }
        if (config_mqtt_args.client_password->count > 0)
        {
            err = MqttConfig::SetClientPassword(config_mqtt_args.client_password->sval[0]);
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to set client password to configuration storage! (%d)", err);
            }
            else
            {
                ESP_LOGI(TAG, "Client password set to configuration storage: %s", MqttConfig::GetClientPassword().c_str());
            }
        }
        if (config_mqtt_args.tls_enabled->count > 0)
        {
            err = MqttConfig::EnableTLS(config_mqtt_args.tls_enabled->ival[0] != 0);
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to set TLS state to configuration storage! (%d)", err);
            }
            else
            {
                ESP_LOGI(TAG, "TLS state set to configuration storage: %s", MqttConfig::isTLSEnabled() ? "enabled" : "disabled");
            }
        }
        if (config_mqtt_args.broker_cert->count > 0)
        {
            err = MqttConfig::SetBrokerCertificate(config_mqtt_args.broker_cert->sval[0]);
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to set broker certificate to configuration storage! (%d)", err);
            }
            else
            {
                ESP_LOGI(TAG, "Broker certificate set to configuration storage: %s", MqttConfig::GetBrokerCertificate().c_str());
            }
        }
        if (config_mqtt_args.topic_prefix->count > 0)
        {
            err = MqttConfig::SetTopicPrefix(config_mqtt_args.topic_prefix->sval[0]);
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to set topic prefix to configuration storage! (%d)", err);
            }
            else
            {
                ESP_LOGI(TAG, "Topic prefix set to configuration storage: %s", MqttConfig::GetTopicPrefix().c_str());
            }
        }
        if (config_mqtt_args.discovery_prefix->count > 0)
        {
            err = MqttConfig::SetDiscoveryPrefix(config_mqtt_args.discovery_prefix->sval[0]);
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to set discovery prefix to configuration storage! (%d)", err);
            }
            else
            {
                ESP_LOGI(TAG, "Discovery prefix set to configuration storage: %s", MqttConfig::GetDiscoveryPrefix().c_str());
            }
        }
        ESP_LOGI(TAG, "New configuration will be applied after reboot!");
    }
    return 0;
}

void register_config_mqtt(void)
{
    config_mqtt_args.read = arg_lit0("r", "read", "Read current configuration from storage (no other argument required)");
    config_mqtt_args.del = arg_lit0("d", "delete", "Delete current configuration in storage (no other argument required)");
    config_mqtt_args.mqtt_state = arg_int0(NULL, "state", "<state>", "1 to enable MQTT client, 0 to disable");
    config_mqtt_args.broker_addr = arg_str0(NULL, "addr", "<address>", "Broker address to connect to");
    config_mqtt_args.broker_port = arg_int0(NULL, "port", "<port>", "Broker port to connect to");
    config_mqtt_args.client_id = arg_str0(NULL, "id", "<client_id>", "Client unique ID when connecting to MQTT broker");
    config_mqtt_args.client_username = arg_str0(NULL, "user", "<username>", "Client username when connecting to MQTT broker");
    config_mqtt_args.client_password = arg_str0(NULL, "pass", "<password>", "Client password when connecting to MQTT broker");
    config_mqtt_args.tls_enabled = arg_int0(NULL, "tls", "<tls_state>", "1 to enable TLS connection to MQTT broker, 0 to disable");
    config_mqtt_args.broker_cert = arg_str0(NULL, "cert", "<certificate>", "MQTT broker certificate (content of .pem file without --- BEGIN CERTIFICATE --- and ---END CERTIFICATE ---)");
    config_mqtt_args.topic_prefix = arg_str0(NULL, "topic", "<topic_prefix>", "Prefix added before all MQTT topics except discovery");
    config_mqtt_args.discovery_prefix = arg_str0(NULL, "discovery", "<discovery_prefix>", "Prefix added before discovery topic. Discovery topic will be <discovery_prefix>/config");
    config_mqtt_args.end = arg_end(12);

    const esp_console_cmd_t config_mqtt_cmd = {
        .command = "config_mqtt",
        .help = "Configure MQTT (changes are applied after reboot)",
        .hint = NULL,
        .func = &do_config_mqtt_cmd,
        .argtable = &config_mqtt_args,
        .func_w_context = NULL,
        .context = NULL};

    ESP_ERROR_CHECK(esp_console_cmd_register(&config_mqtt_cmd));
}

// ******************* MQTT configuration Register commands ********************

void register_mqtt_config_cmdline_tools()
{
    register_config_mqtt();
}
