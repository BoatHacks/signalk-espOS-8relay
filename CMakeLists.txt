cmake_minimum_required(VERSION 3.22)
# The NMEA2000 library declares cmake_minimum_required(VERSION 3.0), which
# CMake 4 rejects outright. CMake 4 accepts it with this override, needed
# twice: as a variable for this process, and in the environment for the
# separate `cmake -P` process ESP-IDF uses to read component files. No
# effect on CMake 3.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
set(ENV{CMAKE_POLICY_VERSION_MINIMUM} 3.5)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(signalk-espos-8relay)

# Web UI bundle -> the LittleFS 'storage' partition. Defined by espos_httpd;
# must come after project().
espos_project_ui_partition()

# espOS 0.10.3 installed from the component registry decides which of its
# optional parts to start by looking for bare component names (espos_wifi,
# espos_sk, espos_ota) in BUILD_COMPONENTS. Registry components are named
# signalk-espos__espos_*, so every check fails: espos_start() then starts no
# WiFi, no SignalK client and no OTA, and espos_ota never asks WiFi whether a
# station is configured. Link and define what espOS meant to. Harmless once
# espOS matches the namespaced names itself: the same links and =1 defines
# again.
idf_build_get_property(_comps BUILD_COMPONENTS)
idf_component_get_property(_core signalk-espos__espos_core COMPONENT_LIB)
foreach(_part wifi sk ota)
    if("signalk-espos__espos_${_part}" IN_LIST _comps)
        idf_component_get_property(_lib signalk-espos__espos_${_part} COMPONENT_LIB)
        string(TOUPPER "${_part}" _PART)
        target_link_libraries(${_core} PRIVATE ${_lib})
        target_compile_definitions(${_core} PRIVATE ESPOS_HAVE_${_PART}=1)
    endif()
endforeach()
if("signalk-espos__espos_ota" IN_LIST _comps AND "signalk-espos__espos_wifi" IN_LIST _comps)
    idf_component_get_property(_ota signalk-espos__espos_ota COMPONENT_LIB)
    idf_component_get_property(_wifi signalk-espos__espos_wifi COMPONENT_LIB)
    target_link_libraries(${_ota} PRIVATE ${_wifi})
    target_compile_definitions(${_ota} PRIVATE ESPOS_OTA_HAVE_WIFI=1)
endif()
# Same for espos_n2k: it builds its GET /api/v1/n2k diagnostics only when
# espos_httpd's header is on its include path (__has_include), which its
# bare-name optional dependency doesn't achieve, so the stub was built.
if("signalk-espos__espos_n2k" IN_LIST _comps AND "signalk-espos__espos_httpd" IN_LIST _comps)
    idf_component_get_property(_n2k signalk-espos__espos_n2k COMPONENT_LIB)
    idf_component_get_property(_httpd signalk-espos__espos_httpd COMPONENT_LIB)
    target_link_libraries(${_n2k} PRIVATE ${_httpd})
endif()

# Guard the assumption the workaround above relies on: wifi, sk, ota, n2k and
# httpd are pinned dependencies (idf_component.yml), so they're always in
# this build, and the =1 defines above must always land. If espOS ever
# changes how its registry copy names components again -- fixing the bug the
# workaround exists for, or breaking it a different way -- this notices
# either a missing =1 define (the workaround's own IN_LIST check stopped
# matching) or the whole workaround having become a silent no-op, instead of
# shipping a firmware with WiFi/SignalK/OTA/N2K-diagnostics quietly missing
# again. Full audit of this bug class: espOS#138.
get_target_property(_core_defs ${_core} COMPILE_DEFINITIONS)
foreach(_part WIFI SK OTA)
    if(NOT "ESPOS_HAVE_${_part}=1" IN_LIST _core_defs)
        message(FATAL_ERROR
            "espos_core is missing ESPOS_HAVE_${_part}=1: the registry-naming "
            "workaround in this file (see espOS#138) no longer matches espOS's "
            "component names, or espos_${_part} dropped out of the build.")
    endif()
endforeach()
if(NOT DEFINED _ota)
    message(FATAL_ERROR
        "espos_ota and/or espos_wifi are no longer both in BUILD_COMPONENTS: "
        "the registry-naming workaround in this file (see espOS#138) has "
        "nothing to check. Update this guard to match.")
endif()
get_target_property(_ota_defs ${_ota} COMPILE_DEFINITIONS)
if(NOT "ESPOS_OTA_HAVE_WIFI=1" IN_LIST _ota_defs)
    message(FATAL_ERROR
        "espos_ota is missing ESPOS_OTA_HAVE_WIFI=1: the registry-naming "
        "workaround in this file (see espOS#138) no longer matches espOS's "
        "component names, or espos_wifi dropped out of the build.")
endif()
