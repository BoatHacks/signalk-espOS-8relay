idf_component_register(
    SRCS test_main.c test_input_sense.c
    INCLUDE_DIRS "."
    PRIV_REQUIRES input_sense unity
    WHOLE_ARCHIVE)
