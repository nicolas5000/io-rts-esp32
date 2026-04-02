#include "HardwareConfig.hpp"
#include "NvsHelpers.hpp"

#include "sdkconfig.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "HardwareConfig";

constexpr int8_t ESP_INTR_FLAG_DEFAULT = 0;

using namespace Helpers;

namespace Config
{
#if defined(CONFIG_IOHOMECONTROL_SX1276_SPI_HOST2) || defined(CONFIG_ETHERNET_SPI_HOST2)
    /// @brief Initialize SPI_HOST2 from configuration
    /// @return ESP_OK if no error
    static esp_err_t InitSPIHost2()
    {
        // Init SPI bus
        spi_bus_config_t bus = {};
        bus.mosi_io_num = CONFIG_SPI_HOST2_MOSI;
        bus.miso_io_num = CONFIG_SPI_HOST2_MISO;
        bus.sclk_io_num = CONFIG_SPI_HOST2_SCK;
        bus.quadwp_io_num = -1;
        bus.quadhd_io_num = -1;
        return spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO);
    }
#endif

#if defined(CONFIG_IOHOMECONTROL_SX1276_SPI_HOST3) || defined(CONFIG_ETHERNET_SPI_HOST3)
    /// @brief Initialize SPI_HOST3 from configuration
    /// @return ESP_OK if no error
    static esp_err_t InitSPIHost3()
    {
        // Init SPI bus
        spi_bus_config_t bus = {};
        bus.mosi_io_num = CONFIG_SPI_HOST3_MOSI;
        bus.miso_io_num = CONFIG_SPI_HOST3_MISO;
        bus.sclk_io_num = CONFIG_SPI_HOST3_SCK;
        bus.quadwp_io_num = -1;
        bus.quadhd_io_num = -1;
        return spi_bus_initialize(SPI3_HOST, &bus, SPI_DMA_CH_AUTO);
    }
#endif

    esp_err_t InitHardware()
    {
        // Initialize NVS
        esp_err_t err = NvsHelpers::InitNvs();
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to initialize NVS");
            return err;
        }

        // Install GPIO ISR service
        err = gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to install GPIO ISR service");
            return err;
        }

        // Init SPI bus
#if defined(CONFIG_IOHOMECONTROL_SX1276_SPI_HOST2) || defined(CONFIG_ETHERNET_SPI_HOST2)
        // Init SPI_HOST2
        err = InitSPIHost2();
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to initialize SPI_HOST2");
            return err;
        }
#endif
#if defined(CONFIG_IOHOMECONTROL_SX1276_SPI_HOST3) || defined(CONFIG_ETHERNET_SPI_HOST3)
        // Init SPI_HOST3
        err = InitSPIHost3();
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to initialize SPI_HOST3");
            return err;
        }
#endif
        return err;
    }

#ifdef CONFIG_ENABLE_IOHOMECONTROL
    spi_host_device_t GetSX1276SpiHost()
    {
#ifdef CONFIG_IOHOMECONTROL_SX1276_SPI_HOST3
    return SPI3_HOST;
#else
    return SPI2_HOST;
#endif
    }
#endif

#ifdef CONFIG_CONNECTIVITY_CHOICE_ETH
    spi_host_device_t GetEthernetSpiHost()
    {
#ifdef CONFIG_ETHERNET_SPI_HOST2
    return SPI2_HOST;
#elifdef CONFIG_ETHERNET_SPI_HOST3
    return SPI3_HOST;
#endif
    }
#endif
}