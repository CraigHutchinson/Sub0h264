# test_apps/esp_shootout/CMakeLists.txt — ESP32-P4 Sub0h264 vs libavc shootout
cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_SOURCE_DIR}/../../components/libavc_decoder"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp_shootout)
