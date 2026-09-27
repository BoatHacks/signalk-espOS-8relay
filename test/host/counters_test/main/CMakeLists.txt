idf_component_register(
    SRCS test_main.c test_counters.c
    INCLUDE_DIRS "."
    PRIV_REQUIRES counters unity
    WHOLE_ARCHIVE)
