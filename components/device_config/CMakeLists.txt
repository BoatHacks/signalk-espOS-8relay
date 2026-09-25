idf_component_register(
    SRCS src/device_config.c
    INCLUDE_DIRS include
    REQUIRES board
    PRIV_REQUIRES espos_config espos_health)

espos_config_add_descriptor(config/swbank.json)
