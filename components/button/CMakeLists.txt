idf_build_get_property(target IDF_TARGET)

set(srcs src/button_logic.c)
set(priv_reqs "")
if(NOT target STREQUAL "linux")
    list(APPEND srcs src/button_hw.c)
    list(APPEND priv_reqs esp_driver_gpio)
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS include
    REQUIRES board
    PRIV_REQUIRES ${priv_reqs})
