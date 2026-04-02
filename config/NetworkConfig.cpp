#include "NetworkConfig.hpp"
#include "NvsHelpers.hpp"

#include "sdkconfig.h"

static const std::string NETWORK_CONFIG_NAMESPACE = "network";                        // namespace to group network configuration in NVS
static const std::string NETWORK_CONFIG_WIFI_SSID = "wifi_ssid";                      // Key to store Wifi SSID (string)
static const std::string NETWORK_CONFIG_WIFI_PWD = "wifi_pwd";                        // Key to store Wifi password (string)
static const std::string NETWORK_CONFIG_WIFI_PWID = "wifi_pw_id";                     // Key to store Wifi password identifier for SAE H2E (string)
static const std::string NETWORK_CONFIG_WIFI_AUTH_MODE_THRESHOLD = "wifi_auth_thres"; // Key to store Wifi authentication mode threshold (uint8_t)
static const std::string NETWORK_CONFIG_WIFI_SAE_MODE = "wifi_sae_mode";              // Key to store Wifi WPA3 SAE mode selection (uint8_t)
static const std::string NETWORK_CONFIG_DHCP = "ip_dhcp";                             // Key to store DHCP mode (uint8_t), 1 if DHCP enabled
static const std::string NETWORK_CONFIG_HOSTNAME = "ip_hostname";                     // Key to store hostname (string)
static const std::string NETWORK_CONFIG_IP_ADDR = "ip_static_ip";                     // Key to store static IPv4 address (string)
static const std::string NETWORK_CONFIG_IP_MASK = "ip_static_mask";                   // Key to store static IPv4 mask (string)
static const std::string NETWORK_CONFIG_IP_GTW = "ip_static_gw";                      // Key to store static gateway IPv4 address (string)
static const std::string NETWORK_CONFIG_DNS_MAIN = "ip_dns_main";                     // Key to store static main DNS IP address (string)
static const std::string NETWORK_CONFIG_DNS_BACKUP = "ip_dns_backup";                 // Key to store static backup DNS IP address (string)
static const std::string NETWORK_CONFIG_SNTP_SERVER = "ip_sntp_server";               // Key to store SNTP server IP name (string)

using namespace Helpers;

namespace Config
{
    const std::string NetworkConfig::GetHostname()
    {
        std::string hostname = CONFIG_IP_LAYER_HOSTNAME;
        NvsHelpers::GetString(NETWORK_CONFIG_NAMESPACE, NETWORK_CONFIG_HOSTNAME, hostname);
        return hostname;
    }

    esp_err_t NetworkConfig::SetHostname(const std::string &hostname)
    {
        return NvsHelpers::SetString(NETWORK_CONFIG_NAMESPACE, NETWORK_CONFIG_HOSTNAME, hostname);
    }

    bool NetworkConfig::isDHCP()
    {
#ifdef CONFIG_CONNECTIVITY_CHOICE_DHCP
        uint8_t is_dhcp = 1;
#else
        uint8_t is_dhcp = 0;
#endif
        NvsHelpers::GetValue(NETWORK_CONFIG_NAMESPACE, NETWORK_CONFIG_DHCP, is_dhcp);
        return is_dhcp;
    }

    esp_err_t NetworkConfig::SetDHCP(bool dhcpEnabled)
    {
        uint8_t is_dhcp = dhcpEnabled ? 0x01 : 0x00;
        return NvsHelpers::SetValue(NETWORK_CONFIG_NAMESPACE, NETWORK_CONFIG_DHCP, is_dhcp);
    }

    const std::string NetworkConfig::GetIpAddress()
    {
        std::string ip_addr = CONFIG_IP_LAYER_STATIC_IP_ADDR;
        NvsHelpers::GetString(NETWORK_CONFIG_NAMESPACE, NETWORK_CONFIG_IP_ADDR, ip_addr);
        return ip_addr;
    }

    esp_err_t NetworkConfig::SetIpAddress(const std::string &address)
    {
        return NvsHelpers::SetString(NETWORK_CONFIG_NAMESPACE, NETWORK_CONFIG_IP_ADDR, address);
    }

    const std::string NetworkConfig::GetNetworkMask()
    {
        std::string ip_mask = CONFIG_IP_LAYER_STATIC_NETMASK_ADDR;
        NvsHelpers::GetString(NETWORK_CONFIG_NAMESPACE, NETWORK_CONFIG_IP_MASK, ip_mask);
        return ip_mask;
    }

    esp_err_t NetworkConfig::SetNetworkMask(const std::string &mask)
    {
        return NvsHelpers::SetString(NETWORK_CONFIG_NAMESPACE, NETWORK_CONFIG_IP_MASK, mask);
    }

    const std::string NetworkConfig::GetGatewayAddress()
    {
        std::string ip_gw = CONFIG_IP_LAYER_STATIC_GW_ADDR;
        NvsHelpers::GetString(NETWORK_CONFIG_NAMESPACE, NETWORK_CONFIG_IP_GTW, ip_gw);
        return ip_gw;
    }

    esp_err_t NetworkConfig::SetGatewayAddress(const std::string &address)
    {
        return NvsHelpers::SetString(NETWORK_CONFIG_NAMESPACE, NETWORK_CONFIG_IP_GTW, address);
    }

    const std::string NetworkConfig::GetMainDNSAddress()
    {
        std::string dns_main = CONFIG_IP_LAYER_STATIC_DNS_SERVER_MAIN;
        NvsHelpers::GetString(NETWORK_CONFIG_NAMESPACE, NETWORK_CONFIG_DNS_MAIN, dns_main);
        return dns_main;
    }

    esp_err_t NetworkConfig::SetMainDNSAddress(const std::string &address)
    {
        return NvsHelpers::SetString(NETWORK_CONFIG_NAMESPACE, NETWORK_CONFIG_DNS_MAIN, address);
    }

    const std::string NetworkConfig::GetBackupDNSAddress()
    {
        std::string dns_backup = CONFIG_IP_LAYER_STATIC_DNS_SERVER_BACKUP;
        NvsHelpers::GetString(NETWORK_CONFIG_NAMESPACE, NETWORK_CONFIG_DNS_BACKUP, dns_backup);
        return dns_backup;
    }

    esp_err_t NetworkConfig::SetBackupDNSAddress(const std::string &address)
    {
        return NvsHelpers::SetString(NETWORK_CONFIG_NAMESPACE, NETWORK_CONFIG_DNS_BACKUP, address);
    }

    const std::string NetworkConfig::GetSNTPAddress()
    {
        std::string sntp_server = CONFIG_SNTP_TIME_SERVER;
        NvsHelpers::GetString(NETWORK_CONFIG_NAMESPACE, NETWORK_CONFIG_SNTP_SERVER, sntp_server);
        return sntp_server;
    }

    esp_err_t NetworkConfig::SetSNTPAddress(const std::string &address)
    {
        return NvsHelpers::SetString(NETWORK_CONFIG_NAMESPACE, NETWORK_CONFIG_SNTP_SERVER, address);
    }

#ifdef CONFIG_CONNECTIVITY_CHOICE_WIFI
    const std::string NetworkConfig::GetWifiSSID()
    {
        std::string wifi_ssid = CONFIG_ESP_WIFI_SSID;
        NvsHelpers::GetString(NETWORK_CONFIG_NAMESPACE, NETWORK_CONFIG_WIFI_SSID, wifi_ssid);
        return wifi_ssid;
    }

    esp_err_t NetworkConfig::SetWifiSSID(const std::string &ssid)
    {
        return NvsHelpers::SetString(NETWORK_CONFIG_NAMESPACE, NETWORK_CONFIG_WIFI_SSID, ssid);
    }

    const std::string NetworkConfig::GetWifiPassword()
    {
        std::string wifi_pwd = CONFIG_ESP_WIFI_PASSWORD;
        NvsHelpers::GetString(NETWORK_CONFIG_NAMESPACE, NETWORK_CONFIG_WIFI_PWD, wifi_pwd);
        return wifi_pwd;
    }

    esp_err_t NetworkConfig::SetWifiPassword(const std::string &password)
    {
        return NvsHelpers::SetString(NETWORK_CONFIG_NAMESPACE, NETWORK_CONFIG_WIFI_PWD, password);
    }

    wifi_sae_pwe_method_t NetworkConfig::GetWifiSAEMode()
    {
#if CONFIG_ESP_STATION_WPA3_SAE_PWE_HUNT_AND_PECK
        uint8_t wifi_sae = WPA3_SAE_PWE_HUNT_AND_PECK;
#elif CONFIG_ESP_STATION_WPA3_SAE_PWE_HASH_TO_ELEMENT
        uint8_t wifi_sae = WPA3_SAE_PWE_HASH_TO_ELEMENT;
#elif CONFIG_ESP_STATION_WPA3_SAE_PWE_BOTH
        uint8_t wifi_sae = WPA3_SAE_PWE_BOTH;
#endif
        NvsHelpers::GetValue(NETWORK_CONFIG_NAMESPACE, NETWORK_CONFIG_WIFI_SAE_MODE, wifi_sae);
        return static_cast<wifi_sae_pwe_method_t>(wifi_sae);
    }

    esp_err_t NetworkConfig::SetWifiSAEMode(wifi_sae_pwe_method_t mode)
    {
        return NvsHelpers::SetValue(NETWORK_CONFIG_NAMESPACE, NETWORK_CONFIG_WIFI_SAE_MODE, static_cast<uint8_t>(mode));
    }

    const std::string NetworkConfig::GetSAEPasswordId()
    {
        std::string wifi_pwid = CONFIG_ESP_WIFI_PW_ID;
        NvsHelpers::GetString(NETWORK_CONFIG_NAMESPACE, NETWORK_CONFIG_WIFI_PWID, wifi_pwid);
        return wifi_pwid;
    }

    esp_err_t NetworkConfig::SetSAEPasswordId(const std::string &id)
    {
        return NvsHelpers::SetString(NETWORK_CONFIG_NAMESPACE, NETWORK_CONFIG_WIFI_PWID, id);
    }

    wifi_auth_mode_t NetworkConfig::GetWifiAuthModeThreshold()
    {
#if CONFIG_ESP_WIFI_AUTH_OPEN
        uint8_t wifi_thres = WIFI_AUTH_OPEN;
#elif CONFIG_ESP_WIFI_AUTH_WEP
        uint8_t wifi_thres = WIFI_AUTH_WEP;
#elif CONFIG_ESP_WIFI_AUTH_WPA_PSK
        uint8_t wifi_thres = WIFI_AUTH_WPA_PSK;
#elif CONFIG_ESP_WIFI_AUTH_WPA2_PSK
        uint8_t wifi_thres = WIFI_AUTH_WPA2_PSK;
#elif CONFIG_ESP_WIFI_AUTH_WPA_WPA2_PSK
        uint8_t wifi_thres = WIFI_AUTH_WPA_WPA2_PSK;
#elif CONFIG_ESP_WIFI_AUTH_WPA3_PSK
        uint8_t wifi_thres = WIFI_AUTH_WPA3_PSK;
#elif CONFIG_ESP_WIFI_AUTH_WPA2_WPA3_PSK
        uint8_t wifi_thres = WIFI_AUTH_WPA2_WPA3_PSK;
#elif CONFIG_ESP_WIFI_AUTH_WAPI_PSK
        uint8_t wifi_thres = WIFI_AUTH_WAPI_PSK;
#endif
        NvsHelpers::GetValue(NETWORK_CONFIG_NAMESPACE, NETWORK_CONFIG_WIFI_AUTH_MODE_THRESHOLD, wifi_thres);
        return static_cast<wifi_auth_mode_t>(wifi_thres);
    }

    esp_err_t NetworkConfig::SetWifiAuthModeThreshold(wifi_auth_mode_t threshold)
    {
        return NvsHelpers::SetValue(NETWORK_CONFIG_NAMESPACE, NETWORK_CONFIG_WIFI_AUTH_MODE_THRESHOLD, static_cast<uint8_t>(threshold));
    }
#endif // CONFIG_CONNECTIVITY_CHOICE_WIFI
}