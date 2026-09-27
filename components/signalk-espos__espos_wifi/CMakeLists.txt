# SPDX-FileCopyrightText: 2026 Dirk Wahrheit
# SPDX-License-Identifier: Apache-2.0
idf_build_get_property(target IDF_TARGET)
set(srcs src/wifi_sm.c src/wifi_reason.c src/wifi_last_ap.c src/espos_wifi.c src/api_wifi.c)
# espos_net is a public requirement: the station is one transport reporting
# into it, and a consumer that reached espos_mdns.h through this component's
# include path (it lived here until 0.7) still finds it there.
set(reqs espos_config espos_httpd espos_net)
set(priv_reqs espressif__cjson espos_event)
if(target STREQUAL "linux")
    # Host: scripted driver stand-in, no esp_wifi/lwIP on the simulator.
    list(APPEND srcs src/port_sim.c src/hosted_watchdog.c)
else()
    list(APPEND srcs src/port_idf.c src/portal_dns.c src/hosted_watchdog.c)
    list(APPEND priv_reqs esp_wifi esp_netif esp_event esp_timer lwip)
    # esp_hosted is only present on co-processor targets (P4 + C6 and
    # friends); the watchdog compiles to stubs without it. Declaring the
    # dependency unconditionally would break every native-radio build, so
    # key off the managed component actually being checked out.
    idf_build_get_property(project_dir PROJECT_DIR)
    if(EXISTS "${project_dir}/managed_components/espressif__esp_hosted/CMakeLists.txt")
        list(APPEND priv_reqs espressif__esp_hosted)
    endif()
endif()
idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS include
    REQUIRES ${reqs}
    PRIV_REQUIRES ${priv_reqs})

espos_config_add_descriptor(config/wifi.json)
