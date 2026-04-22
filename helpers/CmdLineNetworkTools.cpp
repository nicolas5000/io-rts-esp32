#include "CmdLineManagement.hpp"
#include "NetworkConfig.hpp"

#include <string.h>
#include "argtable3/argtable3.h"
#include "esp_console.h"
#include "esp_log.h"

using namespace Config;

static const char *TAG = "cmdline_mngt";

// ******************* WIFI CONFIG ********************
#ifdef CONFIG_CONNECTIVITY_CHOICE_WIFI
/// @brief Structure used by the 'config_wifi' command
static struct
{
    struct arg_lit *read;
    struct arg_lit *del;
    struct arg_str *ssid;
    struct arg_str *pwd;
    struct arg_int *saemode;
    struct arg_str *saepwid;
    struct arg_str *auth;
    struct arg_end *end;
} configwifi_args;

static int do_configwifi_cmd(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&configwifi_args);
    if (nerrors != 0)
    {
        arg_print_errors(stderr, configwifi_args.end, argv[0]);
        return 1;
    }
    if (configwifi_args.read->count > 0)
    {
        // Read Wifi configuration
        ESP_LOGI(TAG, "Wifi SSID: %s", NetworkConfig::GetWifiSSID().c_str());
        ESP_LOGI(TAG, "Wifi password: %s", NetworkConfig::GetWifiPassword().c_str());
        ESP_LOGI(TAG, "Wifi SAE Mode: %d", NetworkConfig::GetWifiSAEMode());
        ESP_LOGI(TAG, "Wifi SAE Password identifier: %s", NetworkConfig::GetSAEPasswordId().c_str());
        ESP_LOGI(TAG, "Wifi Authentication threshold: %s", NetworkConfig::WifiAuthModeToString(NetworkConfig::GetWifiAuthModeThreshold()).c_str());
    }
    else if (configwifi_args.del->count > 0)
    {
        NetworkConfig::DeleteWifiConfig();
        ESP_LOGI(TAG, "Wifi configuration restored to default values. New configuration will be applied after reboot!");
    }
    else
    {
        esp_err_t err;
        // Set configuration
        if (configwifi_args.ssid->count > 0)
        {
            err = NetworkConfig::SetWifiSSID(configwifi_args.ssid->sval[0]);
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to set Wifi SSID to configuration storage! (%d)", err);
            }
            else
            {
                ESP_LOGI(TAG, "Wifi SSID set to configuration storage: %s", NetworkConfig::GetWifiSSID().c_str());
            }
        }
        if (configwifi_args.pwd->count > 0)
        {
            err = NetworkConfig::SetWifiPassword(configwifi_args.pwd->sval[0]);
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to set Wifi password to configuration storage! (%d)", err);
            }
            else
            {
                ESP_LOGI(TAG, "Wifi password set to configuration storage: %s", NetworkConfig::GetWifiPassword().c_str());
            }
        }
        if (configwifi_args.saemode->count > 0)
        {
            err = NetworkConfig::SetWifiSAEMode(static_cast<wifi_sae_pwe_method_t>(configwifi_args.saemode->ival[0]));
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to set Wifi SAE Mode to configuration storage! (%d)", err);
            }
            else
            {
                ESP_LOGI(TAG, "Wifi SAE Mode set to configuration storage: %d", NetworkConfig::GetWifiSAEMode());
            }
        }
        if (configwifi_args.saepwid->count > 0)
        {
            err = NetworkConfig::SetSAEPasswordId(configwifi_args.saepwid->sval[0]);
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to set Wifi SAE Password identifier to configuration storage! (%d)", err);
            }
            else
            {
                ESP_LOGI(TAG, "Wifi SAE Password identifier set to configuration storage: %s", NetworkConfig::GetSAEPasswordId().c_str());
            }
        }
        if (configwifi_args.auth->count > 0)
        {
            wifi_auth_mode_t threshold = NetworkConfig::StringToWifiAuthMode(configwifi_args.auth->sval[0]);
            err = NetworkConfig::SetWifiAuthModeThreshold(threshold);
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to set Wifi Authentication threshold to configuration storage! (%d)", err);
            }
            else
            {
                ESP_LOGI(TAG, "Wifi Authentication threshold set to configuration storage: %s", NetworkConfig::WifiAuthModeToString(NetworkConfig::GetWifiAuthModeThreshold()).c_str());
            }
        }
        ESP_LOGI(TAG, "New configuration will be applied after reboot!");
    }
    return 0;
}

void register_configwifi(void)
{
    configwifi_args.read = arg_lit0("r", "read", "Read current configuration from storage (no other argument required)");
    configwifi_args.del = arg_lit0("d", "delete", "Delete current configuration in storage (no other argument required)");
    configwifi_args.ssid = arg_str0(NULL, "ssid", "<SSID>", "Wifi SSID");
    configwifi_args.pwd = arg_str0(NULL, "pwd", "<password>", "Wifi password");
    configwifi_args.saemode = arg_int0(NULL, "saemode", "<SAE mode>", "Integer value: 1 = HUNT AND PECK, 2 = H2E, 3 = BOTH");
    configwifi_args.saepwid = arg_str0(NULL, "saepwid", "<SAE pass>", "SAE password identifier");
    configwifi_args.auth = arg_str0(NULL, "auth", "<threshold>", "Authentication threshold: OPEN, WEP, WPA-PSK, WPA/WPA2-PSK, WPA2-PSK, WAPI-PSK, WPA2/WPA3-PSK, WPA3-PSK");
    configwifi_args.end = arg_end(7);

    const esp_console_cmd_t configwifi_cmd = {
        .command = "config_wifi",
        .help = "Configure Wifi (changes are applied after reboot)",
        .hint = NULL,
        .func = &do_configwifi_cmd,
        .argtable = &configwifi_args,
        .func_w_context = NULL,
        .context = NULL};

    ESP_ERROR_CHECK(esp_console_cmd_register(&configwifi_cmd));
}
#endif // CONFIG_CONNECTIVITY_CHOICE_WIFI

// ******************* NETWORK CONFIG ********************

/// @brief Structure used by the 'config_network' command
static struct
{
    struct arg_lit *read;
    struct arg_lit *del;
    struct arg_str *hostname;
    struct arg_int *dhcp_enabled;
    struct arg_str *net_ip;
    struct arg_str *netmask;
    struct arg_str *gateway;
    struct arg_str *dns_main_srv;
    struct arg_str *dns_back_serv;
    struct arg_str *ntp_srv;
    struct arg_end *end;
} config_network_args;

static int do_config_network_cmd(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&config_network_args);
    if (nerrors != 0)
    {
        arg_print_errors(stderr, config_network_args.end, argv[0]);
        return 1;
    }
    if (config_network_args.read->count > 0)
    {
        // Read Network configuration
        ESP_LOGI(TAG, "Device hostname: %s", NetworkConfig::GetHostname().c_str());
        ESP_LOGI(TAG, "DHCP: %s", NetworkConfig::isDHCP() ? "enabled" : "disabled");
        ESP_LOGI(TAG, "Device IP address (if not DHCP): %s", NetworkConfig::GetIpAddress().c_str());
        ESP_LOGI(TAG, "Network mask (if not DHCP): %s", NetworkConfig::GetNetworkMask().c_str());
        ESP_LOGI(TAG, "Gateway IP address (if not DHCP): %s", NetworkConfig::GetGatewayAddress().c_str());
        ESP_LOGI(TAG, "Main DNS server address (if not DHCP): %s", NetworkConfig::GetMainDNSAddress().c_str());
        ESP_LOGI(TAG, "Backup DNS server address (if not DHCP): %s", NetworkConfig::GetBackupDNSAddress().c_str());
        ESP_LOGI(TAG, "NTP server address: %s", NetworkConfig::GetSNTPAddress().c_str());
    }
    else if (config_network_args.del->count > 0)
    {
        NetworkConfig::DeleteNetworkConfig();
        ESP_LOGI(TAG, "Network configuration restored to default values. New configuration will be applied after reboot!");
    }
    else
    {
        esp_err_t err;
        // Set configuration
        if (config_network_args.hostname->count > 0)
        {
            err = NetworkConfig::SetHostname(config_network_args.hostname->sval[0]);
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to set hostname to configuration storage! (%d)", err);
            }
            else
            {
                ESP_LOGI(TAG, "Device hostname set to configuration storage: %s", NetworkConfig::GetHostname().c_str());
            }
        }
        if (config_network_args.dhcp_enabled->count > 0)
        {
            err = NetworkConfig::SetDHCP(config_network_args.dhcp_enabled->ival[0] != 0);
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to set DHCP value to configuration storage! (%d)", err);
            }
            else
            {
                ESP_LOGI(TAG, "DHCP value set to configuration storage: %s", NetworkConfig::isDHCP() ? "enabled" : "disabled");
            }
        }
        if (config_network_args.net_ip->count > 0)
        {
            err = NetworkConfig::SetIpAddress(config_network_args.net_ip->sval[0]);
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to set device IP address to configuration storage! (%d)", err);
            }
            else
            {
                ESP_LOGI(TAG, "Device IP address set to configuration storage: %s", NetworkConfig::GetIpAddress().c_str());
            }
        }
        if (config_network_args.netmask->count > 0)
        {
            err = NetworkConfig::SetNetworkMask(config_network_args.netmask->sval[0]);
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to set network mask to configuration storage! (%d)", err);
            }
            else
            {
                ESP_LOGI(TAG, "Network mask set to configuration storage: %s", NetworkConfig::GetNetworkMask().c_str());
            }
        }
        if (config_network_args.gateway->count > 0)
        {
            err = NetworkConfig::SetGatewayAddress(config_network_args.gateway->sval[0]);
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to set gateway IP address to configuration storage! (%d)", err);
            }
            else
            {
                ESP_LOGI(TAG, "Gateway IP address set to configuration storage: %s", NetworkConfig::GetGatewayAddress().c_str());
            }
        }
        if (config_network_args.dns_main_srv->count > 0)
        {
            err = NetworkConfig::SetMainDNSAddress(config_network_args.dns_main_srv->sval[0]);
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to set main DNS server address to configuration storage! (%d)", err);
            }
            else
            {
                ESP_LOGI(TAG, "Main DNS server address set to configuration storage: %s", NetworkConfig::GetMainDNSAddress().c_str());
            }
        }
        if (config_network_args.dns_back_serv->count > 0)
        {
            err = NetworkConfig::SetBackupDNSAddress(config_network_args.dns_back_serv->sval[0]);
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to set backup DNS server address to configuration storage! (%d)", err);
            }
            else
            {
                ESP_LOGI(TAG, "Backup DNS server address set to configuration storage: %s", NetworkConfig::GetBackupDNSAddress().c_str());
            }
        }
        if (config_network_args.ntp_srv->count > 0)
        {
            err = NetworkConfig::SetSNTPAddress(config_network_args.ntp_srv->sval[0]);
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to set NTP server address to configuration storage! (%d)", err);
            }
            else
            {
                ESP_LOGI(TAG, "NTP server address set to configuration storage: %s", NetworkConfig::GetSNTPAddress().c_str());
            }
        }
        ESP_LOGI(TAG, "New configuration will be applied after reboot!");
    }
    return 0;
}

void register_config_network(void)
{
    config_network_args.read = arg_lit0("r", "read", "Read current configuration from storage (no other argument required)");
    config_network_args.del = arg_lit0("d", "delete", "Delete current configuration in storage (no other argument required)");
    config_network_args.hostname = arg_str0(NULL, "hostname", "<hostname>", "ESP32 hostname");
    config_network_args.dhcp_enabled = arg_int0(NULL, "dhcp", "<dhcp>", "1 to enabled DHCP, 0 to disable (static IP)");
    config_network_args.net_ip = arg_str0(NULL, "ip", "<address>", "ESP32 IPv4 address for static configuration");
    config_network_args.netmask = arg_str0(NULL, "mask", "<netmask>", "Network mask for static configuration");
    config_network_args.gateway = arg_str0(NULL, "gateway", "<gateway>", "Gateway IPv4 address for static configuration");
    config_network_args.dns_main_srv = arg_str0(NULL, "dns1", "<DNS1 address>", "Main DNS server address for static configuration");
    config_network_args.dns_back_serv = arg_str0(NULL, "dns2", "<DNS2 address>", "Backup DNS server address for static configuration");
    config_network_args.ntp_srv = arg_str0(NULL, "ntp", "<NTP address>", "NTP server address (eg pool.ntp.org)");
    config_network_args.end = arg_end(10);

    const esp_console_cmd_t config_network_cmd = {
        .command = "config_network",
        .help = "Configure Network (changes are applied after reboot)",
        .hint = NULL,
        .func = &do_config_network_cmd,
        .argtable = &config_network_args,
        .func_w_context = NULL,
        .context = NULL};

    ESP_ERROR_CHECK(esp_console_cmd_register(&config_network_cmd));
}

// ******************* Network configuration Register commands ********************

void register_network_config_cmdline_tools()
{
#ifdef CONFIG_CONNECTIVITY_CHOICE_WIFI
    register_configwifi();
#endif // CONFIG_CONNECTIVITY_CHOICE_WIFI
    register_config_network();
}
