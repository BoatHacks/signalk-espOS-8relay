idf_component_register(
    SRCS test_main.c test_device_config.c
    INCLUDE_DIRS "."
    PRIV_REQUIRES device_config espos_config espos_health unity
    WHOLE_ARCHIVE)
