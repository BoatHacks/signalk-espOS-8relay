idf_component_register(
    SRCS test_main.c test_pcf85063.c test_rtc_pcf85063.c
    INCLUDE_DIRS "."
    PRIV_REQUIRES rtc_pcf85063 unity
    WHOLE_ARCHIVE)
