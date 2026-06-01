#pragma once
#include <cstdint>

namespace Helpers
{
    class NetworkHelpers
    {
    public:
        /// @brief Initialize network (Wifi / Ethernet, then DHCP / static IP address, then SNTP server synchronization)
        static void InitNetwork();

        /// @brief Get current connection status
        /// @return true if currently connected to network, false otherwise
        static bool isConnected();

        static int GetWifiRetryCount();
        static uint8_t GetLastDisconnectReason();
        static int GetApTimeoutRemainingS();
    };

    // Free functions for fallback AP runtime config — callable from web_server.cpp
    bool     NetworkHelpers_GetFallbackEnabled();
    int      NetworkHelpers_GetRetriesBoot();
    int      NetworkHelpers_GetRetriesRunning();
    uint32_t NetworkHelpers_GetApTimeoutS();
    bool     NetworkHelpers_IsFallbackApRunning();
    void     NetworkHelpers_SetFallbackConfig(bool enabled, int rb, int rr, uint32_t tmo);

}