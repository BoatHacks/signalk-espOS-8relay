idf_component_register(
    SRCS test_main.c test_tone.c
    INCLUDE_DIRS "."
    PRIV_REQUIRES espos_tone unity
    WHOLE_ARCHIVE)
