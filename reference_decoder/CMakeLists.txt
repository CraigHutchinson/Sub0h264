# reference_decoder/CMakeLists.txt — Spec-only H.264 reference decoder
#
# Builds as a standalone library + test within the Sub0h264 project.

cmake_minimum_required(VERSION 3.21)

# Unity build library (header-only, but we compile the unity .cpp for validation)
add_library(spec_ref_decoder STATIC
    src/spec_ref_unity.cpp
)

target_include_directories(spec_ref_decoder PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../components/sub0h264/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../components/sub0h264/include
)

target_compile_features(spec_ref_decoder PUBLIC cxx_std_17)

# Tests
add_executable(SpecRef_Tests
    tests/test_spec_ref.cpp
)

target_link_libraries(SpecRef_Tests PRIVATE spec_ref_decoder)

target_include_directories(SpecRef_Tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../tests/vendor
    ${CMAKE_CURRENT_SOURCE_DIR}/../tests
    ${CMAKE_CURRENT_SOURCE_DIR}/../components/sub0h264/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../components/sub0h264/include
)

# Use main repo fixtures directory (CABAC fixtures are not tracked in git,
# only available in the main working tree)
# For worktrees, try the main repo first, then fall back to local
if(EXISTS "D:/Craig/GitHub/Sub0h264/tests/fixtures/cabac_flat_main.h264")
    set(SPECREF_FIXTURES_DIR "D:/Craig/GitHub/Sub0h264/tests/fixtures"
        CACHE PATH "Path to H.264 test fixtures" FORCE)
else()
    set(SPECREF_FIXTURES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../tests/fixtures"
        CACHE PATH "Path to H.264 test fixtures" FORCE)
endif()

target_compile_definitions(SpecRef_Tests PRIVATE
    SUB0H264_TEST_FIXTURES_DIR="${SPECREF_FIXTURES_DIR}"
)

add_test(NAME SpecRef_Tests COMMAND SpecRef_Tests)

# Diagnostic executable (not part of ctest by default)
add_executable(SpecRef_Diag
    tests/test_diag.cpp
)

target_link_libraries(SpecRef_Diag PRIVATE spec_ref_decoder)

target_include_directories(SpecRef_Diag PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../tests/vendor
    ${CMAKE_CURRENT_SOURCE_DIR}/../tests
    ${CMAKE_CURRENT_SOURCE_DIR}/../components/sub0h264/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../components/sub0h264/include
)

target_compile_definitions(SpecRef_Diag PRIVATE
    SUB0H264_TEST_FIXTURES_DIR="${SPECREF_FIXTURES_DIR}"
)

add_test(NAME SpecRef_Diag COMMAND SpecRef_Diag)
