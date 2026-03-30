# test_apps/unit_tests/main/CMakeLists.txt

idf_component_include(sub0h264)
idf_component_include(log)
idf_component_include(esp_timer)
idf_component_include(heap)

add_library(${COMPONENT_TARGET} STATIC
    test_runner.cpp
)

target_include_directories(${COMPONENT_TARGET} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../tests/vendor"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../components/sub0h264/src"
)

target_link_libraries(${COMPONENT_TARGET} PRIVATE
    idf::sub0h264
    idf::log
    idf::esp_timer
    idf::heap
)

# Embed test fixture H.264 files as binary data
set(_fixtures_dir "${CMAKE_CURRENT_SOURCE_DIR}/../../../tests/fixtures")

if(EXISTS "${_fixtures_dir}/flat_black_640x480.h264")
    target_add_binary_data(${COMPONENT_TARGET}
        "${_fixtures_dir}/flat_black_640x480.h264" BINARY)
endif()

if(EXISTS "${_fixtures_dir}/baseline_640x480_short.h264")
    target_add_binary_data(${COMPONENT_TARGET}
        "${_fixtures_dir}/baseline_640x480_short.h264" BINARY)
endif()

if(EXISTS "${_fixtures_dir}/high_640x480.h264")
    target_add_binary_data(${COMPONENT_TARGET}
        "${_fixtures_dir}/high_640x480.h264" BINARY)
endif()
