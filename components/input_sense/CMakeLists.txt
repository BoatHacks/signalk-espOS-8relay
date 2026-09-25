idf_build_get_property(target IDF_TARGET)

set(srcs src/input_sense.c)
set(priv_reqs "")
if(NOT target STREQUAL "linux")
    list(APPEND srcs src/input_hw.c)
    list(APPEND priv_reqs esp_driver_gpio esp_timer)
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS include
    REQUIRES board device_config
    PRIV_REQUIRES ${priv_reqs})
