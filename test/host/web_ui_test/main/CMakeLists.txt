idf_component_register(
    SRCS test_main.c test_web_ui.c
    INCLUDE_DIRS "."
    PRIV_REQUIRES web_ui espressif__cjson unity
    WHOLE_ARCHIVE)
