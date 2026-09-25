idf_component_register(
    SRCS test_main.c test_switch_bank_pgn.c
    INCLUDE_DIRS "."
    PRIV_REQUIRES switch_bank unity
    WHOLE_ARCHIVE)
