cmake_minimum_required(VERSION 3.22)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
set(COMPONENTS main)
list(APPEND EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../../../components/board"
    "${CMAKE_CURRENT_LIST_DIR}/../../../components/device_config"
    "${CMAKE_CURRENT_LIST_DIR}/../../../components/input_sense")
project(input_sense_test)
