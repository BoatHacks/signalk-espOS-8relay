idf_build_get_property(target IDF_TARGET)

set(srcs src/pcf85063.c src/rtc_pcf85063.c)
set(priv_reqs "")
if(NOT target STREQUAL "linux")
    list(APPEND srcs src/rtc_hw.c)
    list(APPEND priv_reqs board esp_driver_i2c)
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS include
    PRIV_REQUIRES ${priv_reqs})
