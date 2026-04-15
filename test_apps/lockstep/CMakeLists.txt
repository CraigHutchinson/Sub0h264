# test_apps/lockstep/CMakeLists.txt — Lock-step entropy comparison tool
#
# Decodes a fixture with entropy trace enabled (CABAC bins + CAVLC symbols)
# and outputs bins in a format that can be compared against the JM reference
# decoder's trace output.
#
# Build:
#   cmake --preset default && cmake --build --preset default --target sub0h264_lockstep
#
# Usage:
#   sub0h264_lockstep <input.h264> [--slice N]   (default slice=1, the first P-frame)
#
# Output (to stderr): OUR_SLICE_START + per-bin lines, one per CABAC bin.
# Compare against JM trace with: python scripts/lockstep_compare.py

cmake_minimum_required(VERSION 3.21)

add_executable(sub0h264_lockstep main.cpp)
target_link_libraries(sub0h264_lockstep PRIVATE Sub0h264::Sub0h264)
target_include_directories(sub0h264_lockstep PRIVATE
    "${CMAKE_SOURCE_DIR}/components/sub0h264/src"
)

# Enable entropy trace (SUB0H264_ENTROPY_TRACE activates CABAC bin trace and
# CAVLC symbol trace hooks; compiled out entirely in production builds).
target_compile_definitions(sub0h264_lockstep PRIVATE
    SUB0H264_ENTROPY_TRACE=1
)
