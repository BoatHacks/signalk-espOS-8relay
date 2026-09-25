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
