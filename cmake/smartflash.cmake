# cmake/smartflash.cmake — Differential flash using esptool --diff-with
#
# Invoked as a CMake script (-P) by the 'smartflash' custom target.
# Reads flasher_args.json, compares each binary against a previous
# snapshot in BUILD_DIR/.flash_prev/, and passes --diff-with to esptool
# for sector-level differential flashing.
#
# After a successful flash, copies all flashed binaries to .flash_prev/
# for the next run. First run flashes everything without --diff-with.
#
# Usage (from CMake target):
#   cmake -P cmake/smartflash.cmake
#     -DBUILD_DIR=<binary_dir>
#     -DFLASH_PORT=<COMx>
#     -DPYTHON_EXE=<path_to_python>
#
# Adapted from NestNinja/cmake/smartflash.cmake for Sub0h264.
#
# Docs: https://docs.espressif.com/projects/esptool/en/latest/esp32/esptool/basic-commands.html
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.22)

if(NOT DEFINED BUILD_DIR)
    message(FATAL_ERROR "Usage: cmake -P smartflash.cmake -DBUILD_DIR=...")
endif()

# Read FLASH_PORT from parameter or ESPPORT environment variable.
if(NOT DEFINED FLASH_PORT OR FLASH_PORT STREQUAL "")
    set(FLASH_PORT $ENV{ESPPORT})
endif()
if(FLASH_PORT STREQUAL "")
    message(FATAL_ERROR "[smartflash] No port — set ESPPORT env var or pass -DFLASH_PORT=COMx")
endif()

# ── Read flasher_args.json ───────────────────────────────────────────────

set(FLASHER_ARGS_FILE "${BUILD_DIR}/flasher_args.json")
if(NOT EXISTS "${FLASHER_ARGS_FILE}")
    message(FATAL_ERROR "flasher_args.json not found at ${FLASHER_ARGS_FILE} — build first")
endif()

file(READ "${FLASHER_ARGS_FILE}" FLASHER_JSON)

string(JSON CHIP GET "${FLASHER_JSON}" "extra_esptool_args" "chip")
string(JSON BEFORE GET "${FLASHER_JSON}" "extra_esptool_args" "before")
string(JSON AFTER  GET "${FLASHER_JSON}" "extra_esptool_args" "after")

# Parse write_flash_args array.
string(JSON WF_COUNT LENGTH "${FLASHER_JSON}" "write_flash_args")
set(WRITE_FLASH_ARGS "")
math(EXPR WF_LAST "${WF_COUNT} - 1")
foreach(i RANGE 0 ${WF_LAST})
    string(JSON WF_ARG GET "${FLASHER_JSON}" "write_flash_args" ${i})
    list(APPEND WRITE_FLASH_ARGS "${WF_ARG}")
endforeach()

# Parse flash_files object: {offset: filename, ...}
string(JSON FF_COUNT LENGTH "${FLASHER_JSON}" "flash_files")
set(FLASH_OFFSETS "")
set(FLASH_FILES "")
math(EXPR FF_LAST "${FF_COUNT} - 1")
foreach(i RANGE 0 ${FF_LAST})
    string(JSON FF_KEY MEMBER "${FLASHER_JSON}" "flash_files" ${i})
    string(JSON FF_VAL GET "${FLASHER_JSON}" "flash_files" "${FF_KEY}")
    list(APPEND FLASH_OFFSETS "${FF_KEY}")
    list(APPEND FLASH_FILES "${FF_VAL}")
endforeach()

# ── Check prev/ snapshot ─────────────────────────────────────────────────

set(PREV_DIR "${BUILD_DIR}/.flash_prev")
set(HAS_PREV FALSE)

if(EXISTS "${PREV_DIR}/.port")
    file(READ "${PREV_DIR}/.port" PREV_PORT)
    string(STRIP "${PREV_PORT}" PREV_PORT)
    if("${PREV_PORT}" STREQUAL "${FLASH_PORT}")
        set(HAS_PREV TRUE)
    else()
        message(STATUS "[smartflash] Port changed (${PREV_PORT} -> ${FLASH_PORT}) — full flash")
        file(REMOVE_RECURSE "${PREV_DIR}")
    endif()
endif()

# ── Build esptool command ────────────────────────────────────────────────

if(NOT DEFINED PYTHON_EXE OR NOT EXISTS "${PYTHON_EXE}")
    find_program(PYTHON_EXE python3 python REQUIRED)
endif()

set(ESPTOOL_CMD ${PYTHON_EXE} -m esptool
    --chip ${CHIP}
    -p ${FLASH_PORT}
    -b 460800
    --before ${BEFORE}
    --after ${AFTER}
    write_flash
    ${WRITE_FLASH_ARGS}
)

# Append offset + file pairs.
list(LENGTH FLASH_OFFSETS N_PARTS)
math(EXPR N_LAST "${N_PARTS} - 1")
foreach(i RANGE 0 ${N_LAST})
    list(GET FLASH_OFFSETS ${i} OFFSET)
    list(GET FLASH_FILES ${i} REL_FILE)
    set(ABS_FILE "${BUILD_DIR}/${REL_FILE}")
    list(APPEND ESPTOOL_CMD "${OFFSET}" "${ABS_FILE}")
endforeach()

# Append --diff-with at the end (after all positional args).
if(HAS_PREV)
    list(APPEND ESPTOOL_CMD "--diff-with")
    foreach(i RANGE 0 ${N_LAST})
        list(GET FLASH_FILES ${i} REL_FILE)
        get_filename_component(FNAME "${REL_FILE}" NAME)
        set(PREV_FILE "${PREV_DIR}/${FNAME}")
        if(EXISTS "${PREV_FILE}")
            list(APPEND ESPTOOL_CMD "${PREV_FILE}")
        else()
            list(APPEND ESPTOOL_CMD "skip")
        endif()
    endforeach()
endif()

# ── Execute ──────────────────────────────────────────────────────────────

if(HAS_PREV)
    message(STATUS "[smartflash] ${N_PARTS} partitions -> ${FLASH_PORT} (diff-with prev)")
else()
    message(STATUS "[smartflash] ${N_PARTS} partitions -> ${FLASH_PORT} (full — no prev snapshot)")
endif()

string(REPLACE ";" " " _cmd_str "${ESPTOOL_CMD}")
message(STATUS "[smartflash] CMD: ${_cmd_str}")

execute_process(
    COMMAND ${ESPTOOL_CMD}
    WORKING_DIRECTORY "${BUILD_DIR}"
    RESULT_VARIABLE FLASH_RESULT
)

if(NOT FLASH_RESULT EQUAL 0)
    message(FATAL_ERROR "[smartflash] esptool failed (exit ${FLASH_RESULT})")
endif()

# ── Snapshot: copy flashed binaries to prev/ ─────────────────────────────

set(TMP_DIR "${PREV_DIR}.tmp")
file(REMOVE_RECURSE "${TMP_DIR}")
file(MAKE_DIRECTORY "${TMP_DIR}")

foreach(i RANGE 0 ${N_LAST})
    list(GET FLASH_FILES ${i} REL_FILE)
    get_filename_component(FNAME "${REL_FILE}" NAME)
    file(COPY_FILE "${BUILD_DIR}/${REL_FILE}" "${TMP_DIR}/${FNAME}")
endforeach()

file(WRITE "${TMP_DIR}/.port" "${FLASH_PORT}")

# Atomic rename.
file(REMOVE_RECURSE "${PREV_DIR}")
file(RENAME "${TMP_DIR}" "${PREV_DIR}")

message(STATUS "[smartflash] Done — snapshot saved to .flash_prev/")
