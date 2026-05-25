idf_component_register(
    SRCS "web_server.cpp" "syslog.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_server esp_netif joltwallet__littlefs espressif__cjson espressif__mqtt nvs_flash esp_driver_spi esp_driver_gpio mbedtls app_update
)

# Access project headers without creating a circular component dependency
target_include_directories(__idf_web_server PRIVATE
    ${CMAKE_SOURCE_DIR}/main
    ${CMAKE_SOURCE_DIR}/io-homecontrol
    ${CMAKE_SOURCE_DIR}/io-homecontrol/protocol
    ${CMAKE_SOURCE_DIR}/io-homecontrol/radio
    ${CMAKE_SOURCE_DIR}/helpers
    ${CMAKE_SOURCE_DIR}/config
)
