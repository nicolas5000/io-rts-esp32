#pragma once

#include "IoRtsManager.hpp"

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Initialize command line
    /// @param io_rts_manager Pointer to IoRtsManager object (nullptr = skip IO commands)
    void init_cmdline(IoRts::IoRtsManager *io_rts_manager);

    /// @brief Initialize command line tools
    void init_cmdline_tools();

    /// @brief Register misc. command line tools (like reboot)
    void register_misc_cmdline_tools();

    /// @brief Register network configuration command line tools (Wifi, DHCP, static IPv4, DNS and SNTP configuration)
    void register_network_config_cmdline_tools();

    /// @brief Register MQTT configuration command line tools
    void register_mqtt_config_cmdline_tools();

    /// @brief Register syslog configuration command line tools
    void register_syslog_config_cmdline_tools();

    /// @brief Register command line tools
    /// @param io_rts_manager Pointer to IoRtsManager object
    void register_io_cmdline_tools(IoRts::IoRtsManager *io_rts_manager);

    /// @brief Initialize hardware console (driver, ...)
    void init_console();

#ifdef __cplusplus
}
#endif