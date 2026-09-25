idf_build_get_property(target IDF_TARGET)

set(srcs src/web_ui_logic.c)
set(priv_reqs "")
set(embed "")
if(NOT target STREQUAL "linux")
    list(APPEND srcs src/web_ui.c)
    list(APPEND priv_reqs espos_httpd esp_http_server freertos)
    set(embed www/relays.html)
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS include
    REQUIRES board device_config
    PRIV_REQUIRES espressif__cjson ${priv_reqs}
    EMBED_TXTFILES ${embed})
