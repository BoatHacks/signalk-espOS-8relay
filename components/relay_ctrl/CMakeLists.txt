idf_build_get_property(target IDF_TARGET)

set(srcs src/relay_ctrl.c src/tca9554.c)
set(priv_reqs espos_health)
if(NOT target STREQUAL "linux")
    list(APPEND srcs src/relay_hw.c)
    list(APPEND priv_reqs esp_driver_i2c esp_timer nvs_flash)
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS include
    REQUIRES board device_config freertos
    PRIV_REQUIRES ${priv_reqs})
