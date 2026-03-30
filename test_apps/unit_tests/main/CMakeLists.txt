# test_apps/unit_tests/main/CMakeLists.txt
#
# ESP32-P4 test component: compiles ALL shared desktop test sources
# as individual TUs alongside the ESP32 test runner.
#
# Adding a new test file only requires editing tests/test_sources.cmake.

# Import shared test source list (single source of truth)
include("${CMAKE_CURRENT_SOURCE_DIR}/../../../tests/test_sources.cmake")

idf_component_include(sub0h264)
idf_component_include(log)
idf_component_include(esp_timer)
idf_component_include(heap)
idf_component_include(freertos)

# Build test sources as separate TUs alongside the runner
set(_all_sources test_runner.cpp)
foreach(_src ${SUB0H264_TEST_SOURCES})
    list(APPEND _all_sources "${SUB0H264_TEST_DIR}/${_src}")
endforeach()

add_library(${COMPONENT_TARGET} STATIC ${_all_sources})

target_include_directories(${COMPONENT_TARGET} PRIVATE
    "${SUB0H264_TEST_DIR}/vendor"
    "${SUB0H264_TEST_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../components/sub0h264/src"
)

target_link_libraries(${COMPONENT_TARGET} PRIVATE
    idf::sub0h264
    idf::log
    idf::esp_timer
    idf::heap
    idf::freertos
)

# Ensure linker doesn't strip self-registering doctest TEST_CASE objects.
# Without this, the linker drops test TUs since nothing in test_runner.cpp
# explicitly references symbols from them — doctest uses static constructors.
target_link_options(${COMPONENT_TARGET} INTERFACE
    -Wl,--whole-archive $<TARGET_FILE:${COMPONENT_TARGET}> -Wl,--no-whole-archive
)

# ── Auto-embed test fixtures from tests/fixtures/ ────────────────────────
set(_fixtures_dir "${SUB0H264_TEST_DIR}/fixtures")
file(GLOB _fixture_files "${_fixtures_dir}/*.h264")
foreach(_f ${_fixture_files})
    target_add_binary_data(${COMPONENT_TARGET} "${_f}" BINARY)
endforeach()
list(LENGTH _fixture_files _n_fixtures)
message(STATUS "Embedded ${_n_fixtures} test fixtures from ${_fixtures_dir}")
