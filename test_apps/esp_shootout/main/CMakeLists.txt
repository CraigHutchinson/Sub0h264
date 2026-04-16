# Sub0h264 sources (compiled directly, not via component)
set(SUB0H264_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../../../components/sub0h264/src")
set(SUB0H264_PLATFORM "${CMAKE_CURRENT_SOURCE_DIR}/../../../components/sub0h264/platform/generic")

idf_component_register(
    SRCS
        "main.cpp"
        "${SUB0H264_SRC}/sub0h264_unity.cpp"
        "${SUB0H264_PLATFORM}/sub0h264_platform_generic.cpp"
    INCLUDE_DIRS
        "."
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../components/sub0h264/include"
        "${SUB0H264_SRC}"
    REQUIRES libavc_decoder heap esp_timer
)

# Embed benchmark fixtures
set(_fixtures_dir "${CMAKE_CURRENT_SOURCE_DIR}/../../../tests/fixtures")

set(_bench_fixtures
    bench_scroll_baseline_640x480.h264
    bench_scroll_high_640x480.h264
    bench_ball_baseline_640x480.h264
    bench_ball_high_640x480.h264
    bench_still_baseline_640x480.h264
    bench_still_high_640x480.h264
    flat_black_baseline_640x480.h264
    baseline_640x480_short.h264
    scrolling_texture_baseline.h264
    scrolling_texture_high.h264
    tapo_c110_stream2_high.h264
)

foreach(_f ${_bench_fixtures})
    if(EXISTS "${_fixtures_dir}/${_f}")
        target_add_binary_data(${COMPONENT_TARGET} "${_fixtures_dir}/${_f}" BINARY)
    endif()
endforeach()
