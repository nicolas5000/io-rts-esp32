#include <stdio.h>
#include <string.h>

#include "HardwareConfig.hpp"
#include "NetworkHelpers.hpp"
#include "RadioSX1276.hpp"
#include "IoHomeControl.hpp"
#include "cmd_line_management.hpp"

#include "esp_log.h"
#include "sdkconfig.h"
#include "esp_console.h"

using namespace Helpers;

static const char *TAG = "io-rts-esp32";

#ifdef CONFIG_ENABLE_IOHOMECONTROL
static void loggerCallback(esp_log_level_t log_level, const char *tag, std::string log)
{
    switch (log_level)
    {
    case ESP_LOG_ERROR:
        ESP_LOGE(tag, "%s", log.c_str());
        break;
    case ESP_LOG_INFO:
        ESP_LOGI(tag, "%s", log.c_str());
        break;
    default:
        break;
    }
}

static void deviceStatusCallback(const std::string deviceID, const iohome::IoDevice &device)
{
    ESP_LOGI(TAG, "Callback received device status for %s: %s (0x%02X/0x%02X) / Position %.1f / Moving: %s",
             deviceID.c_str(), device.info.name, device.info.device_type, device.info.device_subtype,
             device.position,
             device.is_stopped ? "No" : "Yes");
}
#endif // ENABLE_IOHOMECONTROL

extern "C" void app_main(void)
{
    // Initialize Hardware: NVS, GPIO ISR, SPI bus
    esp_err_t err = Config::InitHardware();
    ESP_ERROR_CHECK(err);

    // Initialize network: Ethernet/Wifi + DHCP/Static IP + SNTP
    NetworkHelpers::InitNetwork();
    vTaskDelay(pdMS_TO_TICKS(5000));

    // Initialize IO-HOMECONTROL
#ifdef CONFIG_ENABLE_IOHOMECONTROL
#ifdef CONFIG_IOHOMECONTROL_LOGGING_ENABLED
    bool logging = true;
#else
    bool logging = false;
#endif
#ifdef CONFIG_IOHOMECONTROL_PASSIVE_MODE
    bool passive = true;
#else
    bool passive = false;
#endif
    RadioLinks::RadioSX1276 radio(Config::GetSX1276SpiHost(),
                                  CONFIG_IOHOMECONTROL_SX1276_SPI_CS, CONFIG_IOHOMECONTROL_SX1276_RST,
                                  CONFIG_IOHOMECONTROL_SX1276_DIO0, CONFIG_IOHOMECONTROL_SX1276_DIO4);
    iohome::IoHomeControl ioHome(&radio, loggerCallback, deviceStatusCallback);
    ioHome.SetVerbose(logging);
    ioHome.Begin(CONFIG_IOHOMECONTROL_DEFAULT_NODEID, CONFIG_IOHOMECONTROL_DEFAULT_KEY, passive);
    ioHome.ConfigureRadio(CONFIG_IOHOMECONTROL_DEFAULT_TX_POWER);
    ioHome.StartReceive();
#endif // ENABLE_IOHOMECONTROL

    // Initialize Command line tools
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt = "io-rts-esp32>";

    // install console REPL environment
#if CONFIG_ESP_CONSOLE_UART
    esp_console_dev_uart_config_t uart_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&uart_config, &repl_config, &repl));
#elif CONFIG_ESP_CONSOLE_USB_CDC
    esp_console_dev_usb_cdc_config_t cdc_config = ESP_CONSOLE_DEV_CDC_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_usb_cdc(&cdc_config, &repl_config, &repl));
#elif CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    esp_console_dev_usb_serial_jtag_config_t usbjtag_config = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_usb_serial_jtag(&usbjtag_config, &repl_config, &repl));
#endif
#ifdef CONFIG_ENABLE_IOHOMECONTROL
    register_io_cmdline_tools(&ioHome);
#endif

    printf("\n ==============================================================\n");
    printf(" |            Steps to Use io-rts-esp32                       |\n");
    printf(" |                                                            |\n");
    printf(" |  1. Try 'help', check all supported commands               |\n");
    printf(" |  2. Try 'io_' + TAB for IO commands auto-completion        |\n");
    printf(" |                                                            |\n");
    printf(" ==============================================================\n\n");

    // start console REPL
    ESP_ERROR_CHECK(esp_console_start_repl(repl));
    while (true)
        vTaskDelay(pdMS_TO_TICKS(60000));
}