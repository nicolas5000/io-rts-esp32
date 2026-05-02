#include "IoHomeConfig.hpp"
#include "NvsHelpers.hpp"

#include "sdkconfig.h"

static const std::string IOHOME_CONFIG_NAMESPACE = "iohc";                 // namespace to group IO HomeControl configuration in NVS
static const std::string IOHOME_CONFIG_LOGGING_ENABLE = "log_enabled";     // Key to store logging status (uint8_t), 0 if logging disabled
static const std::string IOHOME_CONFIG_PASSIVE_ENABLE = "passive_enabled"; // Key to store passive mode status (uint8_t), 0 if passive mode disabled
static const std::string IOHOME_CONFIG_SYSTEM_KEY = "system_key";          // Key to store IO System key (string representation of 16 bytes)
static const std::string IOHOME_CONFIG_NODE_ID = "node_id";                // Key to store our own node ID (string representation of 3 bytes)
static const std::string IOHOME_CONFIG_TX_POWER = "tx_power";              // Key to store Tx power (uint8_t)
static const std::string IOHOME_CONFIG_IGNORE_AUTO_UPDATE = "ign_autoupd"; // Key to store ignore auto-update flag (uint8_t)

using namespace Helpers;

namespace Config
{
    void IoHomeConfig::DeleteIoHomeConfig()
    {
        NvsHelpers::DeleteValue(IOHOME_CONFIG_NAMESPACE, IOHOME_CONFIG_LOGGING_ENABLE);
        NvsHelpers::DeleteValue(IOHOME_CONFIG_NAMESPACE, IOHOME_CONFIG_PASSIVE_ENABLE);
        NvsHelpers::DeleteValue(IOHOME_CONFIG_NAMESPACE, IOHOME_CONFIG_SYSTEM_KEY);
        NvsHelpers::DeleteValue(IOHOME_CONFIG_NAMESPACE, IOHOME_CONFIG_NODE_ID);
        NvsHelpers::DeleteValue(IOHOME_CONFIG_NAMESPACE, IOHOME_CONFIG_TX_POWER);
        NvsHelpers::DeleteValue(IOHOME_CONFIG_NAMESPACE, IOHOME_CONFIG_IGNORE_AUTO_UPDATE);
    }
    bool IoHomeConfig::isLoggingEnabled()
    {
#ifdef CONFIG_IOHOMECONTROL_LOGGING_ENABLED
        uint8_t is_enabled = true;
#else
        uint8_t is_enabled = false;
#endif
        NvsHelpers::GetValue(IOHOME_CONFIG_NAMESPACE, IOHOME_CONFIG_LOGGING_ENABLE, is_enabled);
        return is_enabled;
    }
    esp_err_t IoHomeConfig::ActivateLogging(bool loggingEnabled)
    {
        uint8_t is_enabled = loggingEnabled ? 0x01 : 0x00;
        return NvsHelpers::SetValue(IOHOME_CONFIG_NAMESPACE, IOHOME_CONFIG_LOGGING_ENABLE, is_enabled);
    }
    bool IoHomeConfig::isPassiveModeEnabled()
    {
#ifdef CONFIG_IOHOMECONTROL_PASSIVE_MODE
        uint8_t is_enabled = true;
#else
        uint8_t is_enabled = false;
#endif
        NvsHelpers::GetValue(IOHOME_CONFIG_NAMESPACE, IOHOME_CONFIG_PASSIVE_ENABLE, is_enabled);
        return is_enabled;
    }
    esp_err_t IoHomeConfig::ActivatePassiveMode(bool passiveModeEnabled)
    {
        uint8_t is_enabled = passiveModeEnabled ? 0x01 : 0x00;
        return NvsHelpers::SetValue(IOHOME_CONFIG_NAMESPACE, IOHOME_CONFIG_PASSIVE_ENABLE, is_enabled);
    }
    const std::string IoHomeConfig::GetIoSystemKey()
    {
        std::string key = CONFIG_IOHOMECONTROL_DEFAULT_KEY;
        NvsHelpers::GetString(IOHOME_CONFIG_NAMESPACE, IOHOME_CONFIG_SYSTEM_KEY, key);
        return key;
    }
    esp_err_t IoHomeConfig::SetIoSystemKey(const std::string &ioSystemKey)
    {
        if (ioSystemKey.length() != 32) return ESP_ERR_INVALID_ARG;
        return NvsHelpers::SetString(IOHOME_CONFIG_NAMESPACE, IOHOME_CONFIG_SYSTEM_KEY, ioSystemKey);
    }
    const std::string IoHomeConfig::GetIoNodeId()
    {
        std::string nodeId = CONFIG_IOHOMECONTROL_DEFAULT_NODEID;
        NvsHelpers::GetString(IOHOME_CONFIG_NAMESPACE, IOHOME_CONFIG_NODE_ID, nodeId);
        return nodeId;
    }
    esp_err_t IoHomeConfig::SetIoNodeId(const std::string &ioNodeId)
    {
        if (ioNodeId.length() != 6) return ESP_ERR_INVALID_ARG;
        return NvsHelpers::SetString(IOHOME_CONFIG_NAMESPACE, IOHOME_CONFIG_NODE_ID, ioNodeId);
    }
    bool IoHomeConfig::isIgnoreAutoUpdateEnabled()
    {
#ifdef CONFIG_IOHOMECONTROL_IGNORE_AUTO_UPDATE
        uint8_t is_enabled = true;
#else
        uint8_t is_enabled = false;
#endif
        NvsHelpers::GetValue(IOHOME_CONFIG_NAMESPACE, IOHOME_CONFIG_IGNORE_AUTO_UPDATE, is_enabled);
        return is_enabled;
    }
    esp_err_t IoHomeConfig::SetIgnoreAutoUpdate(bool ignoreAutoUpdate)
    {
        uint8_t is_enabled = ignoreAutoUpdate ? 0x01 : 0x00;
        return NvsHelpers::SetValue(IOHOME_CONFIG_NAMESPACE, IOHOME_CONFIG_IGNORE_AUTO_UPDATE, is_enabled);
    }
    uint8_t IoHomeConfig::GetTxPower()
    {
        uint8_t power = CONFIG_IOHOMECONTROL_DEFAULT_TX_POWER;
        NvsHelpers::GetValue(IOHOME_CONFIG_NAMESPACE, IOHOME_CONFIG_TX_POWER, power);
        return power;
    }
    esp_err_t IoHomeConfig::SetTxPower(const uint8_t txPower)
    {
        if (txPower > 20)
        {
            return ESP_ERR_INVALID_ARG;
        }
        return NvsHelpers::SetValue(IOHOME_CONFIG_NAMESPACE, IOHOME_CONFIG_TX_POWER, static_cast<uint8_t>(txPower));
    }
}