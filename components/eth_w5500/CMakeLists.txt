idf_component_register(
    SRCS src/eth_w5500.c
    INCLUDE_DIRS include
    PRIV_REQUIRES board espos_net esp_eth esp_netif esp_event esp_driver_spi esp_driver_gpio)
