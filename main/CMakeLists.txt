idf_component_register(SRCS "cmd_line_management.cpp" "main.cpp" "../io-homecontrol/IoHomeControl.cpp" "../io-homecontrol/protocol/iohome_commands.cpp"
                        "../io-homecontrol/protocol/iohome_crypto.cpp" "../io-homecontrol/protocol/iohome_frame.cpp"
                        "../io-homecontrol/radio/RadioSX1276.cpp"
                    INCLUDE_DIRS "." "../io-homecontrol" "../io-homecontrol/protocol" "../io-homecontrol/radio"
                    REQUIRES esp_timer esp_driver_spi esp_driver_gpio console esp_driver_uart mbedtls)