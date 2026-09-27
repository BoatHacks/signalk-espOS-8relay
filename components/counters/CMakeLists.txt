idf_build_get_property(target IDF_TARGET)

set(srcs src/counters.c)
set(priv_reqs "")
if(NOT target STREQUAL "linux")
    list(APPEND srcs src/counters_hw.c)
    list(APPEND priv_reqs esp_timer nvs_flash)
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS include
    REQUIRES board freertos
    PRIV_REQUIRES ${priv_reqs})
