#include "esp_err.h"
#include "esp_log.h"
#include "espos.h"
#include "board.h"
#include "eth_w5500.h"

static const char *TAG = "app";

void app_main(void)
{
    espos_start_opts_t opts = ESPOS_START_OPTS_DEFAULT;
    opts.app_name = "signalk-espOS-8relay";
    opts.board = BOARD_NAME;
    ESP_ERROR_CHECK(espos_start(&opts));

    // Ethernet failing must not stop the device: it still works over WiFi.
    esp_err_t err = eth_w5500_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Ethernet unavailable (%s); continuing on WiFi", esp_err_to_name(err));
    }
}
