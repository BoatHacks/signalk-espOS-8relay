idf_component_register(
    SRCS test_main.c test_sun.c
    INCLUDE_DIRS "."
    PRIV_REQUIRES schedule unity
    WHOLE_ARCHIVE)
