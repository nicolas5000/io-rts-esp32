#pragma once

#include <string>

#include "esp_err.h"

#ifdef CONFIG_CONNECTIVITY_CHOICE_WIFI
#include "esp_wifi.h"
#endif

namespace Config
{
    class NetworkConfig
    {
    public:

        /// @brief Get Hostname from configuration storage (useful for DHCP)
        /// @return hostname
        static const std::string GetHostname();

        /// @brief Set hostname to configuration storage
        /// @param hostname hostname to store
        /// @return ESP_OK if configuration put to storage without error
        static esp_err_t SetHostname(const std::string &hostname);

        /// @brief Get DHCP / Static IP configuration from storage
        /// @return true if DHCP enabled
        static bool isDHCP();

        /// @brief Set DHCP to configuration storage
        /// @param dhcpEnabled true to enable DHCP
        /// @return ESP_OK if configuration put to storage without error
        static esp_err_t SetDHCP(bool dhcpEnabled);

        /// @brief Get static IPv4 address from configuration storage
        /// @return IP address
        static const std::string GetIpAddress();

        /// @brief Set static IPv4 address to configuration storage
        /// @param address IP address
        /// @return ESP_OK if configuration put to storage without error
        static esp_err_t SetIpAddress(const std::string &address);

        /// @brief Get network mask from configuration storage
        /// @return network mask
        static const std::string GetNetworkMask();

        /// @brief Set network mask to configuration storage
        /// @param mask network mask
        /// @return ESP_OK if configuration put to storage without error
        static esp_err_t SetNetworkMask(const std::string &mask);

        /// @brief Get gateway IPv4 address from configuration storage
        /// @return gateway IPv4 address
        static const std::string GetGatewayAddress();

        /// @brief Set gateway IPv4 address to configuration storage
        /// @param address gateway IPv4 address
        /// @return ESP_OK if configuration put to storage without error
        static esp_err_t SetGatewayAddress(const std::string &address);

        /// @brief Get main DNS IPv4 address from configuration storage
        /// @return main DNS IPv4 address
        static const std::string GetMainDNSAddress();

        /// @brief Set main DNS IPv4 address to configuration storage
        /// @param address main DNS IPv4 address
        /// @return ESP_OK if configuration put to storage without error
        static esp_err_t SetMainDNSAddress(const std::string &address);

        /// @brief Get backup DNS IPv4 address from configuration storage
        /// @return backup DNS IPv4 address
        static const std::string GetBackupDNSAddress();
        
        /// @brief Set backup DNS IPv4 address to configuration storage
        /// @param address backup DNS IPv4 address
        /// @return ESP_OK if configuration put to storage without error
        static esp_err_t SetBackupDNSAddress(const std::string &address);

        /// @brief Get SNTP IPv4 address from configuration storage
        /// @return SNTP IPv4 address
        static const std::string GetSNTPAddress();

        /// @brief Set SNTP IPv4 address to configuration storage
        /// @param address SNTP IPv4 address
        /// @return ESP_OK if configuration put to storage without error
        static esp_err_t SetSNTPAddress(const std::string &address);

#ifdef CONFIG_CONNECTIVITY_CHOICE_WIFI

        /// @brief Get Wifi SSID from configuration storage
        /// @return Wifi SSID
        static const std::string GetWifiSSID();

        /// @brief Set Wifi SSID to configuration storage
        /// @param ssid Wifi SSID
        /// @return ESP_OK if configuration put to storage without error
        static esp_err_t SetWifiSSID(const std::string &ssid);

        /// @brief Get Wifi password from configuration storage
        /// @return Wifi password
        static const std::string GetWifiPassword();

        /// @brief Set Wifi password to configuration storage
        /// @param password Wifi password
        /// @return ESP_OK if configuration put to storage without error
        static esp_err_t SetWifiPassword(const std::string &password);

        /// @brief Get Wifi SAE mode from configuration storage
        /// @return Wifi SAE mode
        static wifi_sae_pwe_method_t GetWifiSAEMode();

        /// @brief Set Wifi SAE mode to configuration storage
        /// @param mode Wifi SAE mode
        /// @return ESP_OK if configuration put to storage without error
        static esp_err_t SetWifiSAEMode(wifi_sae_pwe_method_t mode);

        /// @brief Get SAE Password identifier from configuration storage
        /// @return SAE Password identifier
        static const std::string GetSAEPasswordId();

        /// @brief Set SAE Password identifier to configuration storage
        /// @param id SAE Password identifier
        /// @return ESP_OK if configuration put to storage without error
        static esp_err_t SetSAEPasswordId(const std::string &id);

        /// @brief Get authentication mode threshold from configuration storage
        /// @return authentication mode threshold
        static wifi_auth_mode_t GetWifiAuthModeThreshold();

        /// @brief Set authentication mode threshold to configuration storage
        /// @param threshold authentication mode threshold
        /// @return ESP_OK if configuration put to storage without error
        static esp_err_t SetWifiAuthModeThreshold(wifi_auth_mode_t threshold);

#endif // CONFIG_CONNECTIVITY_CHOICE_WIFI
    };

}