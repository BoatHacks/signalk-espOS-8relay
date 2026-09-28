idf_component_register(
    SRCS src/sun.c
    INCLUDE_DIRS include)

# libm: sin/cos/tan/asin/acos/fmod/llround. Linked in automatically on the
# ESP32 target (newlib pulls it in), not on the linux host target.
idf_build_get_property(target IDF_TARGET)
if(target STREQUAL "linux")
    target_link_libraries(${COMPONENT_LIB} PRIVATE m)
endif()
