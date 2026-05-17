idf_component_register(SRCS "main.cpp" "IoRtsManager.cpp" "../io-homecontrol/IoHomeControl.cpp" "../io-homecontrol/protocol/iohome_commands.cpp"
                        "../io-homecontrol/protocol/iohome_crypto.cpp" "../io-homecontrol/protocol/iohome_frame.cpp" "../io-homecontrol/protocol/iohome_device.cpp"
                        "../io-homecontrol/radio/RadioSX1276.cpp" "../helpers/NvsHelpers.cpp" "../helpers/NetworkHelpers.cpp" "../helpers/MqttHelpers.cpp"
                        "../helpers/DeviceStorage.cpp" "../helpers/EncodingHelpers.cpp" "../config/MiscConfig.cpp"
                        "../config/NetworkConfig.cpp" "../config/HardwareConfig.cpp" "../config/MqttConfig.cpp" "../config/IoHomeConfig.cpp" "../config/SyslogConfig.cpp"
                        "../helpers/CmdLineManagement.cpp" "../helpers/CmdLineIoTools.cpp" "../helpers/CmdLineMqttTools.cpp" "../helpers/CmdLineNetworkTools.cpp"
                    INCLUDE_DIRS "." "../helpers" "../io-homecontrol" "../io-homecontrol/protocol" "../io-homecontrol/radio" "../config"
                    REQUIRES esp_timer esp_driver_spi esp_driver_gpio console esp_driver_uart mbedtls nvs_flash esp_wifi esp_eth esp_netif mqtt esp_app_format littlefs oled_display web_server espressif__mdns)