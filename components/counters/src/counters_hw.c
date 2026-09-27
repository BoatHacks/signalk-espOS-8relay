#include "counters_hw.h"

#include "esp_timer.h"
#include "nvs.h"

#define NVS_NS "counters"
#define NVS_KEY "v1"

static nvs_handle_t s_nvs;

static esp_err_t load(void *ctx, counters_data_t *out)
{
    size_t len = sizeof(*out);
    esp_err_t err = nvs_get_blob(s_nvs, NVS_KEY, out, &len);
    if (err == ESP_OK && len != sizeof(*out)) {
        return ESP_ERR_INVALID_SIZE;  // a future format; start at zero rather than misread it
    }
    return err;
}

static esp_err_t save(void *ctx, const counters_data_t *data)
{
    esp_err_t err = nvs_set_blob(s_nvs, NVS_KEY, data, sizeof(*data));
    return err == ESP_OK ? nvs_commit(s_nvs) : err;
}

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

esp_err_t counters_hw_clear(void)
{
    esp_err_t err = nvs_erase_key(s_nvs, NVS_KEY);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        return err;
    }
    return nvs_commit(s_nvs);
}

esp_err_t counters_hw_create(counters_hw_t *out)
{
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &s_nvs);
    if (err != ESP_OK) {
        return err;
    }
    *out = (counters_hw_t){
        .store = {.load = load, .save = save},
        .now_ms = now_ms,
    };
    return ESP_OK;
}
