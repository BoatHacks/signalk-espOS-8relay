idf_component_register(
    SRCS test_main.c test_indicator.c
    INCLUDE_DIRS "."
    PRIV_REQUIRES indicator unity
    WHOLE_ARCHIVE)
