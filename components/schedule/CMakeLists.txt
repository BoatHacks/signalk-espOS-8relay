idf_component_register(
    SRCS src/sun.c src/position_n2k_pgn.c src/position_n2k.c src/position_sk.c src/position.c src/schedule_eval.c
    INCLUDE_DIRS include
    REQUIRES device_config signalk-espos__espos_sk signalk-espos__espos_time
    PRIV_REQUIRES espressif__cjson signalk-espos__espos_health)

# libm: sin/cos/tan/asin/acos/fmod/llround. Linked in automatically on the
# ESP32 target (newlib pulls it in), not on the linux host target.
idf_build_get_property(target IDF_TARGET)
if(target STREQUAL "linux")
    target_link_libraries(${COMPONENT_LIB} PRIVATE m)
endif()
