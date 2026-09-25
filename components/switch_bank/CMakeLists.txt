idf_build_get_property(target IDF_TARGET)

set(srcs src/sk_bridge.c)
set(priv_reqs "")
if(NOT target STREQUAL "linux")
    list(APPEND srcs src/sk_espos.c)
    list(APPEND priv_reqs espos_sk)
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS include
    REQUIRES board device_config freertos
    PRIV_REQUIRES ${priv_reqs})
