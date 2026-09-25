idf_build_get_property(target IDF_TARGET)

set(srcs src/indicator_logic.c)
set(priv_reqs "")
if(NOT target STREQUAL "linux")
    list(APPEND srcs src/indicator.c)
    list(APPEND priv_reqs espressif__led_strip esp_driver_ledc esp_timer espos_health espos_net espos_sk)
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS include
    REQUIRES board device_config
    PRIV_REQUIRES ${priv_reqs})
