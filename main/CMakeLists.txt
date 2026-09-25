idf_component_register(
    SRCS "main.c"
    PRIV_REQUIRES espos_core espos_sk board device_config eth_w5500 relay_ctrl input_sense switch_bank espos_config espos_event esp_timer)
