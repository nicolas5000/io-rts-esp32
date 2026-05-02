#pragma once

#include <string>

#include "esp_err.h"

namespace Config
{
    class IoHomeConfig
    {
    public:
        /// @brief Delete all IO Home configuration in storage
        static void DeleteIoHomeConfig();

        /// @brief Get logging in IO-HomeControl layer status from storage
        /// @return true if logging enabled
        static bool isLoggingEnabled();

        /// @brief Set logging in IO-HomeControl layer to configuration storage
        /// @param loggingEnabled true to enable logging in IO-HomeControl layer
        /// @return ESP_OK if configuration put to storage without error
        static esp_err_t ActivateLogging(bool loggingEnabled);

        /// @brief Get passive mode IO-HomeControl layer status from storage
        /// @return true if passive mode enabled
        static bool isPassiveModeEnabled();

        /// @brief Set passive mode in IO-HomeControl layer to configuration storage
        /// @param passiveModeEnabled true to enable passive mode in IO-HomeControl layer
        /// @return ESP_OK if configuration put to storage without error
        static esp_err_t ActivatePassiveMode(bool passiveModeEnabled);

        /// @brief Get IO System key from configuration storage
        /// @return IO System key (string representation of 16 bytes)
        static const std::string GetIoSystemKey();

        /// @brief Set IO System key to configuration storage
        /// @param ioSystemKey IO System key (string representation of 16 bytes)
        /// @return ESP_OK if configuration put to storage without error
        static esp_err_t SetIoSystemKey(const std::string &ioSystemKey);

        /// @brief Get IO Node ID from configuration storage
        /// @return IO Node ID (string representation of 3 bytes)
        static const std::string GetIoNodeId();

        /// @brief Set IO Node ID to configuration storage
        /// @param ioNodeId IO Node ID (string representation of 3 bytes)
        /// @return ESP_OK if configuration put to storage without error
        static esp_err_t SetIoNodeId(const std::string &ioNodeId);

        /// @brief Get ignore auto-update flag from configuration storage
        /// @return true if auto-update (0x80) flag should be ignored and timer value used instead
        static bool isIgnoreAutoUpdateEnabled();

        /// @brief Set ignore auto-update flag to configuration storage
        /// @param ignoreAutoUpdate true to ignore auto-update flag and use timer value
        /// @return ESP_OK if configuration put to storage without error
        static esp_err_t SetIgnoreAutoUpdate(bool ignoreAutoUpdate);

        /// @brief Get TX Power from configuration storage
        /// @return TX Power (dBm)
        static uint8_t GetTxPower();

        /// @brief Set TX Power to configuration storage
        /// @param txPower TX Power (dBm), range 0-20
        /// @return ESP_OK if configuration put to storage without error
        static esp_err_t SetTxPower(const uint8_t txPower);
    };

}