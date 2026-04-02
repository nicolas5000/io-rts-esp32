#pragma once

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
    };

}