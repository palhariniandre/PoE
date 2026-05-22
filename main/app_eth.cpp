#include "app_eth.h"

#include <inttypes.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_eth.h"
#include "esp_eth_enc28j60.h"
#include "esp_eth_netif_glue.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_netif_ip_addr.h"
#include "freertos/event_groups.h"
#include "sdkconfig.h"

static const char *TAG = "app_eth";

static constexpr spi_host_device_t ENC28J60_SPI_HOST = SPI2_HOST;
// Matter DNS-SD/multicast increases TX bursts; 4 MHz is more forgiving for ENC28J60 on protoboard.
static constexpr int ENC28J60_SPI_CLOCK_MHZ = 4;
static constexpr int ENC28J60_SPI_CLOCK_HZ = ENC28J60_SPI_CLOCK_MHZ * 1000 * 1000;

static constexpr gpio_num_t ENC28J60_PIN_MOSI = GPIO_NUM_23;
static constexpr gpio_num_t ENC28J60_PIN_MISO = GPIO_NUM_19;
static constexpr gpio_num_t ENC28J60_PIN_SCLK = GPIO_NUM_18;
static constexpr gpio_num_t ENC28J60_PIN_CS = GPIO_NUM_5;
static constexpr gpio_num_t ENC28J60_PIN_INT = GPIO_NUM_27;

static constexpr EventBits_t ETH_LINK_BIT = BIT0;
static constexpr EventBits_t ETH_IPV4_BIT = BIT1;
static constexpr EventBits_t ETH_IPV6_BIT = BIT2;

static EventGroupHandle_t s_eth_event_group;
static esp_eth_handle_t s_eth_handle;
static esp_netif_t *s_eth_netif;
static esp_eth_netif_glue_handle_t s_eth_netif_glue;

static const char *ip6_type_to_str(esp_ip6_addr_type_t type)
{
    switch (type) {
    case ESP_IP6_ADDR_IS_GLOBAL:
        return "global";
    case ESP_IP6_ADDR_IS_LINK_LOCAL:
        return "link-local";
    case ESP_IP6_ADDR_IS_SITE_LOCAL:
        return "site-local";
    case ESP_IP6_ADDR_IS_UNIQUE_LOCAL:
        return "unique-local";
    case ESP_IP6_ADDR_IS_IPV4_MAPPED_IPV6:
        return "ipv4-mapped";
    case ESP_IP6_ADDR_IS_UNKNOWN:
    default:
        return "unknown";
    }
}

static esp_err_t create_default_event_loop_once(void)
{
    esp_err_t err = esp_event_loop_create_default();
    if (err == ESP_ERR_INVALID_STATE) {
        return ESP_OK;
    }
    return err;
}

static esp_err_t install_gpio_isr_service_once(void)
{
    esp_err_t err = gpio_install_isr_service(0);
    if (err == ESP_ERR_INVALID_STATE) {
        return ESP_OK;
    }
    return err;
}

static void eth_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    esp_eth_handle_t eth_handle = *(esp_eth_handle_t *)event_data;

    switch (event_id) {
    case ETHERNET_EVENT_START:
        ESP_LOGI(TAG, "Ethernet Started");
        break;

    case ETHERNET_EVENT_CONNECTED: {
        uint8_t mac_addr[6] = {};
        esp_eth_ioctl(eth_handle, ETH_CMD_G_MAC_ADDR, mac_addr);
        ESP_LOGI(TAG, "Ethernet Link Up");
        ESP_LOGI(TAG, "Ethernet HW Addr %02x:%02x:%02x:%02x:%02x:%02x",
                 mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);

#if CONFIG_LWIP_IPV6
        if (s_eth_netif) {
            esp_err_t err = esp_netif_create_ip6_linklocal(s_eth_netif);
            if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
                ESP_LOGW(TAG, "falha ao criar IPv6 link-local: %s", esp_err_to_name(err));
            }
        }
#endif
        xEventGroupSetBits(s_eth_event_group, ETH_LINK_BIT);
        break;
    }

    case ETHERNET_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "Ethernet Link Down");
        xEventGroupClearBits(s_eth_event_group, ETH_LINK_BIT | ETH_IPV4_BIT | ETH_IPV6_BIT);
        break;

    case ETHERNET_EVENT_STOP:
        ESP_LOGI(TAG, "Ethernet Stopped");
        xEventGroupClearBits(s_eth_event_group, ETH_LINK_BIT | ETH_IPV4_BIT | ETH_IPV6_BIT);
        break;

    default:
        break;
    }
}

static void got_ip_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    ip_event_got_ip_t *event = static_cast<ip_event_got_ip_t *>(event_data);
    if (event->esp_netif != s_eth_netif) {
        return;
    }

    const esp_netif_ip_info_t *ip_info = &event->ip_info;
    ESP_LOGI(TAG, "Ethernet Got IPv4");
    ESP_LOGI(TAG, "ETHIP: " IPSTR, IP2STR(&ip_info->ip));
    ESP_LOGI(TAG, "ETHMASK: " IPSTR, IP2STR(&ip_info->netmask));
    ESP_LOGI(TAG, "ETHGW: " IPSTR, IP2STR(&ip_info->gw));
    xEventGroupSetBits(s_eth_event_group, ETH_IPV4_BIT);
}

#if CONFIG_LWIP_IPV6
static void got_ip6_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    ip_event_got_ip6_t *event = static_cast<ip_event_got_ip6_t *>(event_data);
    if (event->esp_netif != s_eth_netif) {
        return;
    }

    esp_ip6_addr_type_t type = esp_netif_ip6_get_addr_type(&event->ip6_info.ip);
    ESP_LOGI(TAG, "Ethernet Got IPv6: " IPV6STR ", type: %s",
             IPV62STR(event->ip6_info.ip), ip6_type_to_str(type));

    if (type == ESP_IP6_ADDR_IS_LINK_LOCAL || type == ESP_IP6_ADDR_IS_GLOBAL ||
        type == ESP_IP6_ADDR_IS_UNIQUE_LOCAL) {
        xEventGroupSetBits(s_eth_event_group, ETH_IPV6_BIT);
    }
}
#endif

esp_err_t app_eth_init(void)
{
    ESP_RETURN_ON_FALSE(s_eth_event_group == nullptr, ESP_ERR_INVALID_STATE, TAG, "Ethernet already initialized");

    s_eth_event_group = xEventGroupCreate();
    ESP_RETURN_ON_FALSE(s_eth_event_group, ESP_ERR_NO_MEM, TAG, "failed to create Ethernet event group");

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "esp_netif_init failed");
    ESP_RETURN_ON_ERROR(create_default_event_loop_once(), TAG, "esp_event_loop_create_default failed");
    ESP_RETURN_ON_ERROR(install_gpio_isr_service_once(), TAG, "gpio_install_isr_service failed");

    spi_bus_config_t buscfg = {};
    buscfg.mosi_io_num = ENC28J60_PIN_MOSI;
    buscfg.miso_io_num = ENC28J60_PIN_MISO;
    buscfg.sclk_io_num = ENC28J60_PIN_SCLK;
    buscfg.quadwp_io_num = -1;
    buscfg.quadhd_io_num = -1;

    ESP_LOGI(TAG, "Inicializando SPI%d: SCLK=%d MISO=%d MOSI=%d CS=%d INT=%d, clock=%d MHz",
             ENC28J60_SPI_HOST + 1, ENC28J60_PIN_SCLK, ENC28J60_PIN_MISO, ENC28J60_PIN_MOSI,
             ENC28J60_PIN_CS, ENC28J60_PIN_INT, ENC28J60_SPI_CLOCK_MHZ);
    ESP_RETURN_ON_ERROR(spi_bus_initialize(ENC28J60_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO),
                        TAG, "spi_bus_initialize failed");

    spi_device_interface_config_t devcfg = {};
    devcfg.mode = 0;
    devcfg.clock_speed_hz = ENC28J60_SPI_CLOCK_HZ;
    devcfg.spics_io_num = ENC28J60_PIN_CS;
    devcfg.queue_size = 20;
    devcfg.cs_ena_posttrans = enc28j60_cal_spi_cs_hold_time(ENC28J60_SPI_CLOCK_MHZ);

    eth_enc28j60_config_t enc28j60_config = ETH_ENC28J60_DEFAULT_CONFIG(ENC28J60_SPI_HOST, &devcfg);
    enc28j60_config.int_gpio_num = ENC28J60_PIN_INT;

    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    phy_config.autonego_timeout_ms = 0;
    phy_config.phy_addr = 0;
    phy_config.reset_gpio_num = -1;

    esp_eth_mac_t *mac = esp_eth_mac_new_enc28j60(&enc28j60_config, &mac_config);
    ESP_RETURN_ON_FALSE(mac, ESP_FAIL, TAG, "esp_eth_mac_new_enc28j60 failed");

    esp_eth_phy_t *phy = esp_eth_phy_new_enc28j60(&phy_config);
    if (!phy) {
        mac->del(mac);
        ESP_LOGE(TAG, "esp_eth_phy_new_enc28j60 failed");
        return ESP_FAIL;
    }

    esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(mac, phy);
    esp_err_t err = esp_eth_driver_install(&eth_config, &s_eth_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_eth_driver_install failed: %s", esp_err_to_name(err));
        mac->del(mac);
        phy->del(phy);
        return err;
    }

    uint8_t mac_addr[6] = {};
    ESP_RETURN_ON_ERROR(esp_read_mac(mac_addr, ESP_MAC_ETH), TAG, "esp_read_mac failed");
    ESP_RETURN_ON_ERROR(esp_eth_ioctl(s_eth_handle, ETH_CMD_S_MAC_ADDR, mac_addr),
                        TAG, "failed to set Ethernet MAC address");

    esp_netif_config_t netif_config = ESP_NETIF_DEFAULT_ETH();
    s_eth_netif = esp_netif_new(&netif_config);
    ESP_RETURN_ON_FALSE(s_eth_netif, ESP_ERR_NO_MEM, TAG, "esp_netif_new failed");

    s_eth_netif_glue = esp_eth_new_netif_glue(s_eth_handle);
    ESP_RETURN_ON_FALSE(s_eth_netif_glue, ESP_FAIL, TAG, "esp_eth_new_netif_glue failed");
    ESP_RETURN_ON_ERROR(esp_netif_attach(s_eth_netif, s_eth_netif_glue), TAG, "esp_netif_attach failed");

    ESP_RETURN_ON_ERROR(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, eth_event_handler, nullptr),
                        TAG, "failed to register ETH_EVENT handler");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, got_ip_event_handler, nullptr),
                        TAG, "failed to register IPv4 handler");
#if CONFIG_LWIP_IPV6
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_GOT_IP6, got_ip6_event_handler, nullptr),
                        TAG, "failed to register IPv6 handler");
#endif

    ESP_RETURN_ON_ERROR(esp_eth_start(s_eth_handle), TAG, "esp_eth_start failed");
    return ESP_OK;
}

esp_err_t app_wait_for_eth_connected(TickType_t timeout_ticks)
{
    ESP_RETURN_ON_FALSE(s_eth_event_group, ESP_ERR_INVALID_STATE, TAG, "Ethernet not initialized");

    EventBits_t required_bits = ETH_LINK_BIT | ETH_IPV4_BIT;
#if CONFIG_LWIP_IPV6
    required_bits |= ETH_IPV6_BIT;
#endif

    ESP_LOGI(TAG, "Aguardando Ethernet: link + IPv4%s",
#if CONFIG_LWIP_IPV6
             " + IPv6 link-local"
#else
             ""
#endif
    );

    EventBits_t bits = xEventGroupWaitBits(
        s_eth_event_group,
        required_bits,
        pdFALSE,
        pdTRUE,
        timeout_ticks);

    if ((bits & required_bits) != required_bits) {
        ESP_LOGE(TAG, "Timeout aguardando Ethernet. bits=0x%" PRIx32 ", required=0x%" PRIx32,
                 static_cast<uint32_t>(bits), static_cast<uint32_t>(required_bits));
        return ESP_ERR_TIMEOUT;
    }

    ESP_LOGI(TAG, "Ethernet pronta para a proxima etapa: Matter over Ethernet");
    return ESP_OK;
}
