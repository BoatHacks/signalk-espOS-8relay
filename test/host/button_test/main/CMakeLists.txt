idf_component_register(
    SRCS test_main.c test_button_logic.c
    INCLUDE_DIRS "."
    PRIV_REQUIRES button unity
    WHOLE_ARCHIVE)
