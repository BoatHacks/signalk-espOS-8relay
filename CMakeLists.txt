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
