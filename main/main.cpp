#include <stdio.h>
#include <string.h>

#include "HardwareConfig.hpp"
#include "NetworkHelpers.hpp"
#include "IoRtsManager.hpp"
#include "CmdLineManagement.hpp"
#include "oled_display.h"

#include "esp_log.h"
#include "sdkconfig.h"
#include "esp_console.h"

using namespace Helpers;

// static const char *TAG = "io-rts-esp32";

extern "C" void app_main(void)
{
    // Initialize Hardware: NVS, LittleFS, GPIO ISR, SPI bus
    esp_err_t err = Config::InitHardware();
    ESP_ERROR_CHECK(err);

#if CONFIG_OLED_ENABLED
    ESP_ERROR_CHECK(oled_init());
    oled_print_line(0, "io-homecontrol");
    oled_print_line(1, "--------------------");
    oled_print_line(4, "--------------------");
    oled_show_status("Booting...");
#endif

    // Initialize network: Ethernet/Wifi + DHCP/Static IP + SNTP
    NetworkHelpers::InitNetwork();
    vTaskDelay(pdMS_TO_TICKS(5000));

    // Initialize Manager
    IoRts::IoRtsManager ioRtsManager = IoRts::IoRtsManager();

    // Initialize commands line tools
    init_cmdline(&ioRtsManager);

    while (true)
        vTaskDelay(pdMS_TO_TICKS(60000));
}