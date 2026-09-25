idf_component_register(
    SRCS test_main.c test_relay_ctrl.c
    INCLUDE_DIRS "."
    PRIV_REQUIRES relay_ctrl espos_health unity
    WHOLE_ARCHIVE)
