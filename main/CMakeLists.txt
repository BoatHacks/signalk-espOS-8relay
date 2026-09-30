idf_component_register(
    SRCS "main.c"
    PRIV_REQUIRES espos_core espos_sk espos_ota espos_wifi espos_time board button counters device_config eth_w5500 relay_ctrl rtc_pcf85063 schedule input_sense switch_bank indicator web_ui espos_config espos_event espos_health espos_net esp_app_format app_update esp_timer nvs_flash efuse spi_flash esp_hw_support esp_psram heap espressif__cjson console)
