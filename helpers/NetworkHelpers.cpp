#include "NetworkHelpers.hpp"
#include "NetworkConfig.hpp"
#include "HardwareConfig.hpp"

#include "esp_netif.h"
#include "esp_event.h"

#ifdef CONFIG_CONNECTIVITY_CHOICE_WIFI
#include "esp_wifi.h"
#endif

#ifdef CONFIG_CONNECTIVITY_CHOICE_ETH
#include "esp_eth.h"
#include "esp_mac.h"
#include "esp_eth_mac_w5500.h"
#include "esp_eth_phy_w5500.h"
#endif // CONFIG_CONNECTIVITY_CHOICE_ETH

#include "esp_log.h"
#include "esp_sntp.h"
#include <netdb.h>

static const char *TAG = "helpers";
static bool sIsConnected = false;
static esp_netif_t *s_netif = nullptr;

using namespace Config;

namespace Helpers
{
    /// @brief Set DNS server to network interface
    /// @param addr DNS server IPv4 address
    /// @param type DNS type (main or backup)
    /// @return ESP_OK if no error
    static esp_err_t set_dns_server(uint32_t addr, esp_netif_dns_type_t type)
    {
        if (addr && (addr != IPADDR_NONE))
        {
            esp_netif_dns_info_t dns;
            memset(&dns, 0, sizeof(dns));
            dns.ip.u_addr.ip4.addr = addr;
            dns.ip.type = IPADDR_TYPE_V4;
            ESP_ERROR_CHECK(esp_netif_set_dns_info(s_netif, type, &dns));
        }
        return ESP_OK;
    }

    /// @brief Set IPv4 configuration (DHCP or static IP) to network interface from configuration storage
    static void set_ip_from_configuration()
    {
        // Set hostname
        esp_netif_set_hostname(s_netif, NetworkConfig::GetHostname().c_str());
        // Set IP configuration
        if (NetworkConfig::isDHCP() != 0)
        {
            // Set DHCP configuration
            // Nothing to do!
        }
        else
        {
            // Stop DHCP client if started
            esp_netif_dhcp_status_t dhcp_status = ESP_NETIF_DHCP_STARTED;
            esp_netif_dhcpc_get_status(s_netif, &dhcp_status);
            if ((dhcp_status == ESP_NETIF_DHCP_STARTED) && (esp_netif_dhcpc_stop(s_netif) != ESP_OK))
            {
                ESP_LOGE(TAG, "Failed to stop dhcp client");
                return;
            }
            // Set static IP configuration
            esp_netif_ip_info_t ip;
            memset(&ip, 0, sizeof(esp_netif_ip_info_t));
            ip.ip.addr = ipaddr_addr(NetworkConfig::GetIpAddress().c_str());
            ip.netmask.addr = ipaddr_addr(NetworkConfig::GetNetworkMask().c_str());
            ip.gw.addr = ipaddr_addr(NetworkConfig::GetGatewayAddress().c_str());
            esp_err_t err = esp_netif_set_ip_info(s_netif, &ip);
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to set ip info (%d)", err);
                return;
            }
            ESP_ERROR_CHECK(set_dns_server(ipaddr_addr(NetworkConfig::GetMainDNSAddress().c_str()), ESP_NETIF_DNS_MAIN));
            ESP_ERROR_CHECK(set_dns_server(ipaddr_addr(NetworkConfig::GetBackupDNSAddress().c_str()), ESP_NETIF_DNS_BACKUP));
        }
    }

    /// @brief Set SNTP configuration to network interface from configuration storage
    static void set_sntp_from_configuration()
    {
        esp_sntp_stop(); // in case it's a reconnection!
        esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
        esp_sntp_setservername(0, NetworkConfig::GetSNTPAddress().c_str());
        esp_sntp_init();
    }

#ifdef CONFIG_CONNECTIVITY_CHOICE_WIFI
    /// @brief Handler to manage Wifi events
    /// @param arg currently not used in our project, see ESP-IDF documentation
    /// @param event_base Event base, WIFI_EVENT or IP_EVENT
    /// @param event_id type of event
    /// @param event_data useful data depending on event ID and base
    static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                                   int32_t event_id, void *event_data)
    {
        if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
        {
            sIsConnected = false;
            esp_wifi_connect();
        }
        else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED)
        {
            sIsConnected = false;
            set_ip_from_configuration();
        }
        else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
        {
            sIsConnected = false;
            ESP_LOGE(TAG, "Connection to the AP fail!");
            vTaskDelay(pdMS_TO_TICKS(60000));
            ESP_LOGI(TAG, "Retry to connect to the AP");
            esp_wifi_connect();
        }
        else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
        {
            ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
            ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
            // configure SNTP
            set_sntp_from_configuration();
            sIsConnected = true;
        }
    }

    /// @brief Set Wifi configuration to network interface from configuration storage
    static void set_wifi_from_configuration()
    {
        // Get Wifi configuration from storage
        std::string wifi_ssid = NetworkConfig::GetWifiSSID();
        std::string wifi_pwd = NetworkConfig::GetWifiPassword();
        std::string wifi_pwid = NetworkConfig::GetSAEPasswordId();
        // Initialize Wifi
        wifi_config_t wifi_config;
        memset(&wifi_config, 0, sizeof(wifi_config));
        memcpy(&wifi_config.sta.ssid, wifi_ssid.c_str(), wifi_ssid.length() < sizeof(wifi_config.sta.ssid) ? wifi_ssid.length() : sizeof(wifi_config.sta.ssid) - 1);
        memcpy(&wifi_config.sta.password, wifi_pwd.c_str(), wifi_pwd.length() < sizeof(wifi_config.sta.password) ? wifi_pwd.length() : sizeof(wifi_config.sta.password) - 1);
        wifi_config.sta.threshold.authmode = NetworkConfig::GetWifiAuthModeThreshold();
        wifi_config.sta.sae_pwe_h2e = NetworkConfig::GetWifiSAEMode();
        memcpy(&wifi_config.sta.sae_h2e_identifier, wifi_pwid.c_str(), wifi_pwid.length() < sizeof(wifi_config.sta.sae_h2e_identifier) ? wifi_pwid.length() : sizeof(wifi_config.sta.sae_h2e_identifier) - 1);
#ifdef CONFIG_ESP_WIFI_WPA3_COMPATIBLE_SUPPORT
        wifi_config.sta.disable_wpa3_compatible_mode = 0;
#endif
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
        ESP_ERROR_CHECK(esp_wifi_start());
    }

    /// @brief Initialize Wifi interface in station mode
    static void wifi_init_sta()
    {
        s_netif = esp_netif_create_default_wifi_sta();
        assert(s_netif);
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));
        ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                            ESP_EVENT_ANY_ID,
                                                            &wifi_event_handler,
                                                            s_netif,
                                                            NULL));
        ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                            IP_EVENT_STA_GOT_IP,
                                                            &wifi_event_handler,
                                                            s_netif,
                                                            NULL));
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        set_wifi_from_configuration();
    }
#endif // CONFIG_CONNECTIVITY_CHOICE_WIFI
#ifdef CONFIG_CONNECTIVITY_CHOICE_ETH
    /// @brief Handler to manage Ethernet events
    /// @param arg currently not used in our project, see ESP-IDF documentation
    /// @param event_base Event base, ETH_EVENT or IP_EVENT
    /// @param event_id type of event
    /// @param event_data useful data depending on event ID and base
    static void eth_event_handler(void *arg, esp_event_base_t event_base,
                                  int32_t event_id, void *event_data)
    {
        uint8_t mac_addr[6] = {0};
        /* we can get the ethernet driver handle from event data */
        esp_eth_handle_t eth_handle = *(esp_eth_handle_t *)event_data;

        if (event_base == ETH_EVENT)
        {
            sIsConnected = false;
            switch (event_id)
            {
            case ETHERNET_EVENT_CONNECTED:
                esp_eth_ioctl(eth_handle, ETH_CMD_G_MAC_ADDR, mac_addr);
                ESP_LOGI(TAG, "Ethernet Link Up");
                ESP_LOGI(TAG, "Ethernet HW Addr %02x:%02x:%02x:%02x:%02x:%02x",
                         mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
                set_ip_from_configuration();
                break;
            case ETHERNET_EVENT_DISCONNECTED:
                ESP_LOGI(TAG, "Ethernet Link Down");
                break;
            case ETHERNET_EVENT_START:
                ESP_LOGI(TAG, "Ethernet Started");
                break;
            case ETHERNET_EVENT_STOP:
                ESP_LOGI(TAG, "Ethernet Stopped");
                break;
            default:
                break;
            }
        }
        else if (event_base == IP_EVENT && event_id == IP_EVENT_ETH_GOT_IP)
        {
            ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
            ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
            // configure SNTP
            set_sntp_from_configuration();
            sIsConnected = true;
        }
    }

    /// @brief Initialize W5500 Ethernet interface
    static esp_err_t ethernet_init(esp_eth_handle_t *eth_handle_out)
    {
        if (eth_handle_out == NULL)
        {
            ESP_LOGE(TAG, "invalid argument: eth_handle_out cannot be NULL");
            return ESP_ERR_INVALID_ARG;
        }
        
        // The SPI Ethernet module(s) might not have a burned factory MAC address, hence use manually configured address(es).
        // In this component, a locally administered MAC address derived from ESP32x base MAC address is used or
        // the MAC address is configured via Kconfig.
        // Note: The locally administered OUI range should be used only when testing on a LAN under your control!
        uint8_t local_mac_0[ETH_ADDR_LEN];
        uint8_t base_eth_mac_addr[ETH_ADDR_LEN];
        esp_read_mac(base_eth_mac_addr, ESP_MAC_ETH);
        esp_derive_local_mac(local_mac_0, base_eth_mac_addr);
        // Init common MAC and PHY configs to default
        eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
        mac_config.rx_task_stack_size = 4096; // To avoid stack overflow, we could choose a larger value
        eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
        // Update PHY config based on board specific configuration
        phy_config.phy_addr = -1;
        phy_config.reset_gpio_num = CONFIG_ETHERNET_SPI_PHY_RST_GPIO;
        // Configure SPI interface for specific SPI module
        spi_device_interface_config_t spi_devcfg;
        memset(&spi_devcfg, 0, sizeof(spi_devcfg));
        spi_devcfg.mode = 0;
        spi_devcfg.clock_speed_hz = CONFIG_ETHERNET_SPI_CLOCK_MHZ * 1000 * 1000;
        spi_devcfg.spics_io_num = CONFIG_ETHERNET_SPI_CS_GPIO;
        spi_devcfg.queue_size = 20;
        // Init vendor specific MAC config to default, and create new SPI Ethernet MAC instance
        // and new PHY instance based on board configuration
        eth_w5500_config_t w5500_config = ETH_W5500_DEFAULT_CONFIG(Config::GetEthernetSpiHost(), &spi_devcfg);
        w5500_config.int_gpio_num = CONFIG_ETHERNET_SPI_INT0_GPIO;
        // Configure Ethernet MAC object
        esp_eth_mac_t *mac = esp_eth_mac_new_w5500(&w5500_config, &mac_config);
        if (mac == NULL)
        {
            ESP_LOGE(TAG, "create MAC instance failed");
            return ESP_FAIL;
        }
        // Create new generic PHY instance
        esp_eth_phy_t *phy = esp_eth_phy_new_w5500(&phy_config);
        if (phy == NULL)
        {
            ESP_LOGE(TAG, "create PHY instance failed");
            mac->del(mac);
            return ESP_FAIL;
        }
        // Init Ethernet driver to default and install it
        esp_eth_handle_t eth_handle = NULL;
        esp_eth_config_t config = ETH_DEFAULT_CONFIG(mac, phy);
        if (esp_eth_driver_install(&config, &eth_handle) != ESP_OK)
        {
            ESP_LOGE(TAG, "Ethernet driver install failed");
            mac->del(mac);
            phy->del(phy);
            return ESP_FAIL;
        }
        // The SPI Ethernet module might not have a burned factory MAC address, we can set it manually.
        if (esp_eth_ioctl(eth_handle, ETH_CMD_S_MAC_ADDR, local_mac_0) != ESP_OK)
        {
            ESP_LOGE(TAG, "SPI Ethernet MAC address config failed");
            mac->del(mac);
            phy->del(phy);
            return ESP_FAIL;
        }

        *eth_handle_out = eth_handle;

        return ESP_OK;
    }

    /// @brief Initialize Ethernet driver, interface and event handlers
    static void eth_init()
    {
        // Initialize Ethernet driver
        esp_eth_handle_t eth_handle;
        ESP_ERROR_CHECK(ethernet_init(&eth_handle));
        // Create instance of esp-netif for Ethernet
        // Use ESP_NETIF_DEFAULT_ETH when just one Ethernet interface is used and you don't need to modify
        // default esp-netif configuration parameters.
        esp_netif_config_t cfg = ESP_NETIF_DEFAULT_ETH();
        s_netif = esp_netif_new(&cfg);
        esp_eth_netif_glue_handle_t eth_netif_glue = esp_eth_new_netif_glue(eth_handle);
        // Attach Ethernet driver to TCP/IP stack
        ESP_ERROR_CHECK(esp_netif_attach(s_netif, eth_netif_glue));

        // Register user defined event handlers
        ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &eth_event_handler, NULL));
        ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &eth_event_handler, NULL));

        // Start Ethernet driver state machine
        ESP_ERROR_CHECK(esp_eth_start(eth_handle));
    }
#endif // CONFIG_CONNECTIVITY_CHOICE_ETH
    void NetworkHelpers::InitNetwork()
    {
        ESP_ERROR_CHECK(esp_netif_init());
        ESP_ERROR_CHECK(esp_event_loop_create_default());
#ifdef CONFIG_CONNECTIVITY_CHOICE_WIFI
        wifi_init_sta();
#elifdef CONFIG_CONNECTIVITY_CHOICE_ETH
        eth_init();
#endif
        return;
    }

    bool NetworkHelpers::isConnected()
    {
        return sIsConnected;
    }
}