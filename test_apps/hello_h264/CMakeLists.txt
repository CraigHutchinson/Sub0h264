# test_apps/hello_h264/CMakeLists.txt
#
# Minimal ESP-IDF hello-world for the Sub0h264 decoder library.
# Prints version info and chip details — no decoding yet.
#
# Build:
#   cmake --preset esp32p4
#   cmake --build --preset esp32p4
# Flash + monitor:
#   $env:ESPPORT="COM7"; cmake --build --preset esp32p4 --target flash

cmake_minimum_required(VERSION 3.22)

set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_SOURCE_DIR}/../../components/sub0h264"
)

include($ENV{IDF_PATH}/tools/cmakev2/idf.cmake)
project(hello_h264 CXX C ASM)
idf_project_init()
idf_project_default()
