idf_component_register(
    SRCS test_main.c test_sk_bridge.c
    INCLUDE_DIRS "."
    PRIV_REQUIRES switch_bank unity
    WHOLE_ARCHIVE)
