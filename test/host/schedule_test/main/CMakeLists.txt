idf_component_register(
    SRCS test_main.c test_sun.c test_position_n2k_pgn.c test_position_n2k.c test_position_sk.c test_position.c
         test_schedule_eval.c
    INCLUDE_DIRS "."
    PRIV_REQUIRES schedule device_config unity
    WHOLE_ARCHIVE)
