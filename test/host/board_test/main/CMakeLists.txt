idf_component_register(
    SRCS test_main.c test_board.c
    INCLUDE_DIRS "."
    PRIV_REQUIRES board unity
    WHOLE_ARCHIVE)
