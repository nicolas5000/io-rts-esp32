#include "cmd_line_management.hpp"
#include "NetworkConfig.hpp"
#include "MqttConfig.hpp"

#include <stdio.h>
#include <string.h>
#include <algorithm>
#include "argtable3/argtable3.h"
#include "esp_console.h"
#include "esp_log.h"

using namespace Config;

static const char *TAG = "cmdline_mngt";
static iohome::IoHomeControl *sIoHome;

// ******************* IO DISCOVER ********************

static int do_iodiscover_cmd(int argc, char **argv)
{
    if (!sIoHome->DiscoverAndPairDevice())
    {
        ESP_LOGW(TAG, "Discover failed");
    }
    return 0;
}

static void register_iodiscover(void)
{
    const esp_console_cmd_t iodiscover_cmd = {
        .command = "io_discover",
        .help = "Try to find and pair an IO-HomeControl device in pairing mode and never registered",
        .hint = NULL,
        .func = &do_iodiscover_cmd,
        .argtable = NULL,
        .func_w_context = NULL,
        .context = NULL};
    ESP_ERROR_CHECK(esp_console_cmd_register(&iodiscover_cmd));
}

// ******************* IO ADD ********************

/// @brief Structure used by the 'io_add' command
static struct
{
    struct arg_str *device_id;
    struct arg_end *end;
} ioadd_args;

static int do_ioadd_cmd(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&ioadd_args);
    if (nerrors != 0)
    {
        arg_print_errors(stderr, ioadd_args.end, argv[0]);
        return 1;
    }
    std::string deviceID(ioadd_args.device_id->sval[0]);
    std::transform(deviceID.begin(), deviceID.end(), deviceID.begin(), [](unsigned char c)
                   { return std::toupper(c); }); // convert to uppercase
    sIoHome->AddDevice(deviceID);
    return 0;
}

void register_ioadd(void)
{
    ioadd_args.device_id = arg_str1(NULL, NULL, "<deviceid>", "ID of the device, 3 bytes (eg 112233)");
    ioadd_args.end = arg_end(1);

    const esp_console_cmd_t ioadd_cmd = {
        .command = "io_add",
        .help = "Add an already registered IO-HomeControl device",
        .hint = NULL,
        .func = &do_ioadd_cmd,
        .argtable = &ioadd_args,
        .func_w_context = NULL,
        .context = NULL};

    ESP_ERROR_CHECK(esp_console_cmd_register(&ioadd_cmd));
}

// ******************* IO OPEN ********************

/// @brief Structure used by the 'io_open' command
static struct
{
    struct arg_str *device_id;
    struct arg_end *end;
} ioopen_args;

static int do_ioopen_cmd(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&ioopen_args);
    if (nerrors != 0)
    {
        arg_print_errors(stderr, ioopen_args.end, argv[0]);
        return 1;
    }
    sIoHome->OpenDevice(ioopen_args.device_id->sval[0]);
    return 0;
}

void register_ioopen(void)
{
    ioopen_args.device_id = arg_str1(NULL, NULL, "<deviceid>", "ID of the device, 3 bytes (eg 112233)");
    ioopen_args.end = arg_end(1);

    const esp_console_cmd_t ioopen_cmd = {
        .command = "io_open",
        .help = "Open (or set to 'On') an IO-HomeControl device",
        .hint = NULL,
        .func = &do_ioopen_cmd,
        .argtable = &ioopen_args,
        .func_w_context = NULL,
        .context = NULL};

    ESP_ERROR_CHECK(esp_console_cmd_register(&ioopen_cmd));
}

// ******************* IO CLOSE ********************

/// @brief Structure used by the 'io_close' command
static struct
{
    struct arg_str *device_id;
    struct arg_end *end;
} ioclose_args;

static int do_ioclose_cmd(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&ioclose_args);
    if (nerrors != 0)
    {
        arg_print_errors(stderr, ioclose_args.end, argv[0]);
        return 1;
    }
    sIoHome->CloseDevice(ioclose_args.device_id->sval[0]);
    return 0;
}

void register_ioclose(void)
{
    ioclose_args.device_id = arg_str1(NULL, NULL, "<deviceid>", "ID of the device, 3 bytes (eg 112233)");
    ioclose_args.end = arg_end(1);

    const esp_console_cmd_t ioclose_cmd = {
        .command = "io_close",
        .help = "Close (or set to 'Off') an IO-HomeControl device",
        .hint = NULL,
        .func = &do_ioclose_cmd,
        .argtable = &ioclose_args,
        .func_w_context = NULL,
        .context = NULL};

    ESP_ERROR_CHECK(esp_console_cmd_register(&ioclose_cmd));
}

// ******************* IO STOP ********************

/// @brief Structure used by the 'io_stop' command
static struct
{
    struct arg_str *device_id;
    struct arg_end *end;
} iostop_args;

static int do_iostop_cmd(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&iostop_args);
    if (nerrors != 0)
    {
        arg_print_errors(stderr, iostop_args.end, argv[0]);
        return 1;
    }
    sIoHome->StopDevice(iostop_args.device_id->sval[0]);
    return 0;
}

void register_iostop(void)
{
    iostop_args.device_id = arg_str1(NULL, NULL, "<deviceid>", "ID of the device, 3 bytes (eg 112233)");
    iostop_args.end = arg_end(1);

    const esp_console_cmd_t iostop_cmd = {
        .command = "io_stop",
        .help = "Stop a currently moving IO-HomeControl device",
        .hint = NULL,
        .func = &do_iostop_cmd,
        .argtable = &iostop_args,
        .func_w_context = NULL,
        .context = NULL};

    ESP_ERROR_CHECK(esp_console_cmd_register(&iostop_cmd));
}

// ******************* IO FAVORITE POSITION ********************

/// @brief Structure used by the 'io_setfavpos' command
static struct
{
    struct arg_str *device_id;
    struct arg_end *end;
} iosetfavpos_args;

static int do_iosetfavpos_cmd(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&iosetfavpos_args);
    if (nerrors != 0)
    {
        arg_print_errors(stderr, iosetfavpos_args.end, argv[0]);
        return 1;
    }
    sIoHome->SetDeviceToFavoritePosition(iosetfavpos_args.device_id->sval[0]);
    return 0;
}

void register_iosetfavpos(void)
{
    iosetfavpos_args.device_id = arg_str1(NULL, NULL, "<deviceid>", "ID of the device, 3 bytes (eg 112233)");
    iosetfavpos_args.end = arg_end(1);

    const esp_console_cmd_t iosetfavpos_cmd = {
        .command = "io_setfavpos",
        .help = "Set an IO-HomeControl device to favorite position (like 'My' button)",
        .hint = NULL,
        .func = &do_iosetfavpos_cmd,
        .argtable = &iosetfavpos_args,
        .func_w_context = NULL,
        .context = NULL};

    ESP_ERROR_CHECK(esp_console_cmd_register(&iosetfavpos_cmd));
}

// ******************* IO SET POSITION ********************

/// @brief Structure used by the 'io_setpos' command
static struct
{
    struct arg_str *device_id;
    struct arg_int *position;
    struct arg_end *end;
} iosetpos_args;

static int do_iosetpos_cmd(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&iosetpos_args);
    if (nerrors != 0)
    {
        arg_print_errors(stderr, iosetpos_args.end, argv[0]);
        return 1;
    }
    if (iosetpos_args.position->ival[0] >= 0 && iosetpos_args.position->ival[0] <= 100)
    {
        sIoHome->SetDevicePosition(iosetpos_args.device_id->sval[0], iosetpos_args.position->ival[0]);
    }
    else
        ESP_LOGE(TAG, "Invalid value for <position>");
    return 0;
}

void register_iosetpos(void)
{
    iosetpos_args.device_id = arg_str1(NULL, NULL, "<deviceid>", "ID of the device, 3 bytes (eg 112233)");
    iosetpos_args.position = arg_int1(NULL, NULL, "<position>", "Specify the position to reach (0 = OPEN to 100 = CLOSED)");
    iosetpos_args.end = arg_end(2);

    const esp_console_cmd_t iosetpos_cmd = {
        .command = "io_setpos",
        .help = "Set an IO-HomeControl device to a specified position",
        .hint = NULL,
        .func = &do_iosetpos_cmd,
        .argtable = &iosetpos_args,
        .func_w_context = NULL,
        .context = NULL};

    ESP_ERROR_CHECK(esp_console_cmd_register(&iosetpos_cmd));
}

// ******************* IO FORCE STATUS UPDATE ********************

/// @brief Structure used by the 'io_update' command
static struct
{
    struct arg_str *device_id;
    struct arg_end *end;
} ioupdate_args;

static int do_ioupdate_cmd(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&ioupdate_args);
    if (nerrors != 0)
    {
        arg_print_errors(stderr, ioupdate_args.end, argv[0]);
        return 1;
    }
    sIoHome->ForceDeviceStatusUpdate(ioupdate_args.device_id->sval[0]);
    return 0;
}

void register_ioupdate(void)
{
    ioupdate_args.device_id = arg_str1(NULL, NULL, "<deviceid>", "ID of the device, 3 bytes (eg 112233)");
    ioupdate_args.end = arg_end(1);

    const esp_console_cmd_t ioupdate_cmd = {
        .command = "io_update",
        .help = "Force status update for a given IO-HomeControl device",
        .hint = NULL,
        .func = &do_ioupdate_cmd,
        .argtable = &ioupdate_args,
        .func_w_context = NULL,
        .context = NULL};

    ESP_ERROR_CHECK(esp_console_cmd_register(&ioupdate_cmd));
}

// ******************* IO SET NAME ********************

/// @brief Structure used by the 'io_setname' command
static struct
{
    struct arg_str *device_id;
    struct arg_str *device_name;
    struct arg_end *end;
} iosetname_args;

static int do_iosetname_cmd(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&iosetname_args);
    if (nerrors != 0)
    {
        arg_print_errors(stderr, iosetname_args.end, argv[0]);
        return 1;
    }
    sIoHome->SetDeviceName(iosetname_args.device_id->sval[0], iosetname_args.device_name->sval[0]);
    return 0;
}

void register_iosetname(void)
{
    iosetname_args.device_id = arg_str1(NULL, NULL, "<deviceid>", "ID of the device, 3 bytes (eg 112233)");
    iosetname_args.device_name = arg_str1(NULL, NULL, "<name>", "Name of the device, 1 to 15 characters");
    iosetname_args.end = arg_end(2);

    const esp_console_cmd_t iosetname_cmd = {
        .command = "io_setname",
        .help = "Set a new name inside IO-HomeControl device configuration",
        .hint = NULL,
        .func = &do_iosetname_cmd,
        .argtable = &iosetname_args,
        .func_w_context = NULL,
        .context = NULL};

    ESP_ERROR_CHECK(esp_console_cmd_register(&iosetname_cmd));
}

// ******************* IO LINK REMOTE ********************

/// @brief Structure used by the 'io_linkremote' command
static struct
{
    struct arg_str *device_id;
    struct arg_str *remote_id;
    struct arg_end *end;
} iolinkremote_args;

static int do_iolinkremote_cmd(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&iolinkremote_args);
    if (nerrors != 0)
    {
        arg_print_errors(stderr, iolinkremote_args.end, argv[0]);
        return 1;
    }
    sIoHome->LinkRemoteToDevice(iolinkremote_args.remote_id->sval[0], iolinkremote_args.device_id->sval[0]);
    return 0;
}

void register_iolinkremote(void)
{
    iolinkremote_args.device_id = arg_str1(NULL, NULL, "<deviceid>", "ID of the device, 3 bytes (eg 112233)");
    iolinkremote_args.remote_id = arg_str1(NULL, NULL, "<remoteid>", "ID of the remote, 3 bytes (eg AABBCC)");
    iolinkremote_args.end = arg_end(2);

    const esp_console_cmd_t iolinkremote_cmd = {
        .command = "io_linkremote",
        .help = "Link a remote to a IO-HomeControl device",
        .hint = NULL,
        .func = &do_iolinkremote_cmd,
        .argtable = &iolinkremote_args,
        .func_w_context = NULL,
        .context = NULL};

    ESP_ERROR_CHECK(esp_console_cmd_register(&iolinkremote_cmd));
}

// ******************* IO Register commands ********************

void register_io_cmdline_tools(iohome::IoHomeControl *io_home)
{
    sIoHome = io_home;
    register_iodiscover();
    register_ioadd();
    register_ioopen();
    register_ioclose();
    register_iostop();
    register_iosetfavpos();
    register_iosetpos();
    register_ioupdate();
    register_iosetname();
    register_iolinkremote();
}

// ******************* REBOOT ********************

static int do_reboot_cmd(int argc, char **argv)
{
    esp_restart();
    return 0;
}

void register_reboot(void)
{
    const esp_console_cmd_t reboot_cmd = {
        .command = "reboot",
        .help = "Reboot ESP32",
        .hint = NULL,
        .func = &do_reboot_cmd,
        .argtable = NULL,
        .func_w_context = NULL,
        .context = NULL};

    ESP_ERROR_CHECK(esp_console_cmd_register(&reboot_cmd));
}

// ******************* Misc Register commands ********************

void register_misc_cmdline_tools()
{
    register_reboot();
}

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
        ESP_LOGI(TAG, "Wifi configuration restored to default values");
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
        ESP_LOGI(TAG, "Network configuration restored to default values");
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
        ESP_LOGI(TAG, "MQTT configuration restored to default values");
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