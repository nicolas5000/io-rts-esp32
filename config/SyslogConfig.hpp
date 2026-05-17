#pragma once

#include <string>

#include "esp_err.h"

namespace Config
{
    class SyslogConfig
    {
    public:
        /// @brief Delete all syslog configuration in storage
        static void DeleteSyslogConfig();

        /// @brief Get syslog activation status from storage
        /// @return true if syslog enabled
        static bool isEnabled();

        /// @brief Set syslog activation to configuration storage
        /// @param enabled true to enable syslog
        /// @return ESP_OK if configuration put to storage without error
        static esp_err_t SetEnabled(bool enabled);

        /// @brief Get syslog server address from configuration storage
        /// @return server hostname or IP address
        static const std::string GetServer();

        /// @brief Set syslog server address to configuration storage
        /// @param server hostname or IP address
        /// @return ESP_OK if configuration put to storage without error
        static esp_err_t SetServer(const std::string &server);

        /// @brief Get syslog UDP port from configuration storage
        /// @return UDP port number
        static uint16_t GetPort();

        /// @brief Set syslog UDP port to configuration storage
        /// @param port UDP port number (1-65535)
        /// @return ESP_OK if configuration put to storage without error
        static esp_err_t SetPort(uint16_t port);

        /// @brief Get RFC 3164 facility number from configuration storage
        /// @return facility number (0-23)
        static uint8_t GetFacility();

        /// @brief Set RFC 3164 facility number to configuration storage
        /// @param facility facility number (0-23)
        /// @return ESP_OK if configuration put to storage without error
        static esp_err_t SetFacility(uint8_t facility);

        /// @brief Get minimum log level to forward from configuration storage
        /// @return minimum severity (7=debug, 6=info, 4=warning, 3=error)
        static uint8_t GetMinLevel();

        /// @brief Set minimum log level to forward to configuration storage
        /// @param level minimum severity (7=debug, 6=info, 4=warning, 3=error)
        /// @return ESP_OK if configuration put to storage without error
        static esp_err_t SetMinLevel(uint8_t level);
    };
}
