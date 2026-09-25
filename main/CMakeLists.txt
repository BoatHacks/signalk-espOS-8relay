idf_component_register(
    SRCS "main.c"
    PRIV_REQUIRES espos_core espos_sk espos_ota board device_config eth_w5500 relay_ctrl input_sense switch_bank indicator web_ui espos_config espos_event espos_health espos_net esp_app_format esp_timer nvs_flash)
