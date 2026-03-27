add_executable(Sub0h264_Tests
    test_main.cpp
    test_version.cpp
)

target_link_libraries(Sub0h264_Tests PRIVATE Sub0h264::Sub0h264)
target_include_directories(Sub0h264_Tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/vendor"
)

add_test(NAME Sub0h264_Tests COMMAND Sub0h264_Tests)
