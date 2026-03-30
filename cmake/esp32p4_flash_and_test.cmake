# cmake/esp32p4_flash_and_test.cmake
#
# CMake -P script: orchestrates ESP32-P4 flash + serial capture + result parsing.
# Invoked by CTest via add_test(COMMAND cmake -P this_script.cmake -D ...).
#
# Input variables (passed via -D):
#   BUILD_DIR      Path to test_apps/unit_tests/build/esp32p4
#   COM_PORT       Serial port (default: COM5)
#   BOOT_TIMEOUT   Seconds to wait for boot marker (default: 15)
#   TEST_TIMEOUT   Seconds to wait for test completion (default: 120)
#   PYTHON         Python interpreter (default: auto-detected)
#   LOG_DIR        Log output directory (default: ${BUILD_DIR}/test_logs)
#
# Exit: 0 on success, FATAL_ERROR on failure.
#
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.19)  # Required for string(JSON ...)

# ── 0. Defaults ──────────────────────────────────────────────────────────

if(NOT DEFINED BUILD_DIR)
    message(FATAL_ERROR "BUILD_DIR not set. Usage:\n"
        "  cmake -P esp32p4_flash_and_test.cmake -D BUILD_DIR=<path> [-D COM_PORT=COM5]")
endif()

if(NOT DEFINED COM_PORT)
    if(DEFINED ENV{ESPPORT})
        set(COM_PORT "$ENV{ESPPORT}")
    else()
        set(COM_PORT "COM5")
    endif()
endif()

if(NOT DEFINED BOOT_TIMEOUT)
    set(BOOT_TIMEOUT 15)
endif()

if(NOT DEFINED TEST_TIMEOUT)
    set(TEST_TIMEOUT 120)
endif()

if(NOT DEFINED PYTHON)
    # Prefer IDF venv Python (has esptool + pyserial installed)
    if(DEFINED ENV{IDF_PYTHON_ENV_PATH})
        find_program(PYTHON NAMES python python3
            HINTS "$ENV{IDF_PYTHON_ENV_PATH}/Scripts" "$ENV{IDF_PYTHON_ENV_PATH}/bin"
            REQUIRED)
    else()
        # Fallback: search common IDF venv locations
        find_program(PYTHON NAMES python python3
            HINTS "C:/Espressif/tools/python/master/venv/Scripts"
                  "$ENV{IDF_TOOLS_PATH}/python/master/venv/Scripts"
            REQUIRED)
    endif()
endif()

if(NOT DEFINED LOG_DIR)
    set(LOG_DIR "${BUILD_DIR}/test_logs")
endif()

# ── 1. Parse flasher_args.json ───────────────────────────────────────────

set(_flasher_json "${BUILD_DIR}/flasher_args.json")
if(NOT EXISTS "${_flasher_json}")
    message(FATAL_ERROR
        "Build not found: ${_flasher_json}\n"
        "Run: cmake --build --preset esp32p4  first.")
endif()

file(READ "${_flasher_json}" _json)

# Extract chip type
string(JSON _chip GET "${_json}" "extra_esptool_args" "chip")

# Extract flash settings
string(JSON _flash_mode GET "${_json}" "flash_settings" "flash_mode")
string(JSON _flash_size GET "${_json}" "flash_settings" "flash_size")
string(JSON _flash_freq GET "${_json}" "flash_settings" "flash_freq")

# Extract flash_files: build offset->filename pairs
string(JSON _flash_files GET "${_json}" "flash_files")
string(JSON _n_files LENGTH "${_flash_files}")

set(_flash_pairs "")
math(EXPR _last "${_n_files} - 1")
foreach(_i RANGE 0 ${_last})
    string(JSON _offset MEMBER "${_flash_files}" ${_i})
    string(JSON _file GET "${_flash_files}" "${_offset}")
    # Paths in flasher_args.json are relative to BUILD_DIR
    list(APPEND _flash_pairs "${_offset}" "${BUILD_DIR}/${_file}")
endforeach()

message(STATUS "[flash-and-test] Chip: ${_chip}")
message(STATUS "[flash-and-test] Flash: ${_flash_mode} ${_flash_size} ${_flash_freq}")
message(STATUS "[flash-and-test] Port: ${COM_PORT}")
message(STATUS "[flash-and-test] Files: ${_n_files} partitions")

# ── 2. Flash ─────────────────────────────────────────────────────────────

message(STATUS "[flash-and-test] Flashing ESP32-P4 on ${COM_PORT}...")

execute_process(
    COMMAND "${PYTHON}" -m esptool
        --chip "${_chip}"
        --port "${COM_PORT}"
        --baud 921600
        --before default_reset
        --after hard_reset
        write_flash
        --flash_mode "${_flash_mode}"
        --flash_size "${_flash_size}"
        --flash_freq "${_flash_freq}"
        ${_flash_pairs}
    RESULT_VARIABLE _flash_rc
    OUTPUT_VARIABLE _flash_out
    ERROR_VARIABLE _flash_err
    TIMEOUT 60
)

if(NOT _flash_rc EQUAL 0)
    message(FATAL_ERROR
        "[flash-and-test] Flash FAILED (rc=${_flash_rc}):\n"
        "${_flash_out}\n${_flash_err}")
endif()

message(STATUS "[flash-and-test] Flash complete. Waiting 500ms for port release...")

# Brief delay to ensure serial port is released by esptool
execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 0.5)

# ── 3. Serial capture ───────────────────────────────────────────────────

file(MAKE_DIRECTORY "${LOG_DIR}")
string(TIMESTAMP _ts "%Y%m%d_%H%M%S")
set(_log_file "${LOG_DIR}/test_${_ts}.log")

# Locate the serial capture script (sibling of this file)
get_filename_component(_script_dir "${CMAKE_CURRENT_LIST_FILE}" DIRECTORY)
set(_capture_script "${_script_dir}/esp32p4_serial_capture.py")

if(NOT EXISTS "${_capture_script}")
    message(FATAL_ERROR
        "[flash-and-test] Serial capture script not found: ${_capture_script}")
endif()

message(STATUS "[flash-and-test] Capturing serial output...")

execute_process(
    COMMAND "${PYTHON}" "${_capture_script}"
        --port "${COM_PORT}"
        --baud 115200
        --boot-timeout "${BOOT_TIMEOUT}"
        --test-timeout "${TEST_TIMEOUT}"
        --log-file "${_log_file}"
    RESULT_VARIABLE _capture_rc
    TIMEOUT 180  # Hard backstop: boot + test + margin
)

# ── 4. Parse results ────────────────────────────────────────────────────

if(EXISTS "${_log_file}")
    file(READ "${_log_file}" _raw_log)
    # Strip ANSI escape codes for clean matching
    string(REGEX REPLACE "\x1b\\[[0-9;]*[a-zA-Z]" "" _clean_log "${_raw_log}")
else()
    set(_clean_log "")
endif()

# Always print captured output (CTest shows this on failure)
message(STATUS "══════════════════ Device Output ══════════════════")
message("${_clean_log}")
message(STATUS "══════════════════ End Device Output ══════════════")

# Interpret exit code from serial capture script
if(_capture_rc EQUAL 0)
    message(STATUS "[flash-and-test] ESP32-P4 tests PASSED")
elseif(_capture_rc EQUAL 1)
    message(FATAL_ERROR
        "[flash-and-test] ESP32-P4 tests FAILED\n"
        "Log: ${_log_file}")
elseif(_capture_rc EQUAL 2)
    message(FATAL_ERROR
        "[flash-and-test] ESP32-P4 test TIMEOUT\n"
        "Boot timeout: ${BOOT_TIMEOUT}s, Test timeout: ${TEST_TIMEOUT}s\n"
        "Log: ${_log_file}")
elseif(_capture_rc EQUAL 3)
    message(FATAL_ERROR
        "[flash-and-test] ESP32-P4 CRASHED during tests\n"
        "Log: ${_log_file}")
elseif(_capture_rc EQUAL 4)
    message(FATAL_ERROR
        "[flash-and-test] Serial port error on ${COM_PORT}\n"
        "Check: device connected? port in use by another tool?")
else()
    message(FATAL_ERROR
        "[flash-and-test] Unknown error (rc=${_capture_rc})\n"
        "Log: ${_log_file}")
endif()
