idf_build_get_property(target IDF_TARGET)

set(srcs src/rtttl.c src/tone.c src/tone_player.c)
set(priv_reqs espressif__cjson)
set(reqs "")
if(NOT target STREQUAL "linux")
    list(APPEND srcs src/tone_buzzer.c)
    list(APPEND reqs esp_driver_ledc)
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS include
    REQUIRES ${reqs}
    PRIV_REQUIRES ${priv_reqs})
