idf_build_get_property(target IDF_TARGET)

set(srcs src/sk_bridge.c src/switch_bank_pgn.c)
set(priv_reqs "")
if(NOT target STREQUAL "linux")
    list(APPEND srcs src/sk_espos.c src/n2k_bridge.cpp)
    list(APPEND priv_reqs espos_sk espos_n2k ttlappalainen__nmea2000 nvs_flash esp_app_format)
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS include
    REQUIRES board device_config freertos
    PRIV_REQUIRES ${priv_reqs})
