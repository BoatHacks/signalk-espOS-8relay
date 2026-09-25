cmake_minimum_required(VERSION 3.22)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(signalk-espos-8relay)

# Web UI bundle -> the LittleFS 'storage' partition. Defined by espos_httpd;
# must come after project().
espos_project_ui_partition()
