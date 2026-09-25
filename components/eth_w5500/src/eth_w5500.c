#include "eth_w5500.h"

#include <stdatomic.h>
#include <stdio.h>

#include "board.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_eth.h"
#include "esp_eth_mac_w5500.h"
#include "esp_eth_phy_w5500.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "espos_net.h"

static const char *TAG = "eth_w5500";

#define ETH_SPI_HOST SPI2_HOST
// Conservative; the W5500 is rated far higher. Raise only after testing on
// the board, since signal quality depends on its layout.
#define ETH_SPI_CLOCK_HZ (20 * 1000 * 1000)

static struct {
    atomic_bool started;
    atomic_bool link;
    esp_eth_handle_t eth;
    esp_netif_t *netif;
} s;

static void report_down(void)
{
    espos_net_report(ESPOS_NET_IF_ETH, false, NULL, NULL, NULL, 0);
}

static void on_eth_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    // ETH_EVENT is shared by every Ethernet driver; only react to ours.
    const esp_eth_handle_t *eth = data;
    if (!eth || *eth != s.eth) {
        return;
    }
    if (id == ETHERNET_EVENT_CONNECTED) {
        atomic_store(&s.link, true);
        ESP_LOGI(TAG, "link up; waiting for an address");
    } else if (id == ETHERNET_EVENT_DISCONNECTED) {
        atomic_store(&s.link, false);
        ESP_LOGW(TAG, "link down");
        // Report on link loss too, so the route leaves an unplugged cable at
        // once instead of when the address is declared lost.
        report_down();
    }
}

static void on_ip_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    const ip_event_got_ip_t *e = data;
    if (!e || e->esp_netif != s.netif) {
        return;
    }
    if (id == IP_EVENT_ETH_GOT_IP) {
        char ip[ESPOS_NET_IP_MAX];
        char netmask[ESPOS_NET_IP_MAX];
        char gateway[ESPOS_NET_IP_MAX];
        snprintf(ip, sizeof(ip), IPSTR, IP2STR(&e->ip_info.ip));
        snprintf(netmask, sizeof(netmask), IPSTR, IP2STR(&e->ip_info.netmask));
        snprintf(gateway, sizeof(gateway), IPSTR, IP2STR(&e->ip_info.gw));
        ESP_LOGI(TAG, "address %s", ip);
        espos_net_report(ESPOS_NET_IF_ETH, true, ip, netmask, gateway, 0);
    } else if (id == IP_EVENT_ETH_LOST_IP) {
        ESP_LOGW(TAG, "lost the address");
        report_down();
    }
}

static esp_err_t install(void)
{
    // Needed for the W5500 interrupt pin; already installed is fine.
    esp_err_t err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    spi_bus_config_t bus = {
        .mosi_io_num = BOARD_ETH_MOSI,
        .miso_io_num = BOARD_ETH_MISO,
        .sclk_io_num = BOARD_ETH_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };
    err = spi_bus_initialize(ETH_SPI_HOST, &bus, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_initialize: %s", esp_err_to_name(err));
        return err;
    }

    spi_device_interface_config_t dev = {
        .mode = 0,
        .clock_speed_hz = ETH_SPI_CLOCK_HZ,
        .queue_size = 16,
        .spics_io_num = BOARD_ETH_CS,
    };
    eth_w5500_config_t w5500_cfg = ETH_W5500_DEFAULT_CONFIG(ETH_SPI_HOST, &dev);
    w5500_cfg.base.int_gpio_num = BOARD_ETH_INT;

    eth_mac_config_t mac_cfg = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phy_cfg = ETH_PHY_DEFAULT_CONFIG();
    phy_cfg.reset_gpio_num = BOARD_ETH_RST;

    esp_eth_mac_t *mac = esp_eth_mac_new_w5500(&w5500_cfg, &mac_cfg);
    esp_eth_phy_t *phy = mac ? esp_eth_phy_new_w5500(&phy_cfg) : NULL;
    if (!mac || !phy) {
        ESP_LOGE(TAG, "could not create the W5500 driver");
        return ESP_ERR_NO_MEM;
    }
    esp_eth_config_t cfg = ETH_DEFAULT_CONFIG(mac, phy);
    err = esp_eth_driver_install(&cfg, &s.eth);
    if (err != ESP_OK) {
        // Usually the chip didn't answer on SPI: wrong pins, or no W5500.
        ESP_LOGE(TAG, "esp_eth_driver_install: %s (check the SPI pins in board.h)", esp_err_to_name(err));
        s.eth = NULL;
        return err;
    }

    // Use the chip's own Ethernet MAC address, so every board has a stable,
    // unique one whatever the W5500 holds.
    uint8_t mac_addr[6];
    ESP_ERROR_CHECK(esp_read_mac(mac_addr, ESP_MAC_ETH));
    ESP_ERROR_CHECK(esp_eth_ioctl(s.eth, ETH_CMD_S_MAC_ADDR, mac_addr));

    esp_netif_config_t ncfg = ESP_NETIF_DEFAULT_ETH();
    s.netif = esp_netif_new(&ncfg);
    if (!s.netif || esp_netif_attach(s.netif, esp_eth_new_netif_glue(s.eth)) != ESP_OK) {
        ESP_LOGE(TAG, "could not attach the Ethernet netif");
        return ESP_FAIL;
    }

    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, on_eth_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, on_ip_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_LOST_IP, on_ip_event, NULL));

    // Before the driver starts, so the first DHCP request carries espOS's
    // hostname.
    return espos_net_register_if(ESPOS_NET_IF_ETH, s.netif);
}

esp_err_t eth_w5500_start(void)
{
    if (atomic_load(&s.started)) {
        return ESP_OK;
    }
    espos_net_status_t ns;
    if (espos_net_get_status(&ns) != ESP_OK) {
        ESP_LOGE(TAG, "call espos_start() first");
        return ESP_ERR_INVALID_STATE;
    }
    // A failed install is not retried: it means the hardware or pin map is
    // wrong, which a second attempt won't fix. The device carries on on WiFi.
    esp_err_t err = install();
    if (err != ESP_OK) {
        return err;
    }
    err = esp_eth_start(s.eth);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_eth_start: %s", esp_err_to_name(err));
        return err;
    }
    atomic_store(&s.started, true);
    ESP_LOGI(TAG, "started; waiting for a link");
    return ESP_OK;
}

bool eth_w5500_link_up(void)
{
    return atomic_load(&s.started) && atomic_load(&s.link);
}
