add_executable(Sub0h264_Tests
    test_main.cpp
    test_version.cpp
    test_bitstream.cpp
    test_nal.cpp
    test_sps_pps.cpp
    test_slice.cpp
    test_cavlc.cpp
    test_iframe.cpp
)

target_link_libraries(Sub0h264_Tests PRIVATE Sub0h264::Sub0h264)
target_include_directories(Sub0h264_Tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/vendor"
)

# Path to test fixtures (H.264 test vectors)
target_compile_definitions(Sub0h264_Tests PRIVATE
    SUB0H264_TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)

add_test(NAME Sub0h264_Tests COMMAND Sub0h264_Tests)
