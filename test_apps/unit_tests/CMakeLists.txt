# test_apps/unit_tests/CMakeLists.txt
#
# ESP-IDF test app: runs the Sub0h264 doctest unit tests on ESP32-P4.
# Test fixtures are embedded as binary data in flash.
#
# Build + test (CTest integration):
#   pwsh -NoProfile -File ../../scripts/nomsys.ps1 cmake --preset esp32p4
#   pwsh -NoProfile -File ../../scripts/nomsys.ps1 cmake --build --preset esp32p4
#   ctest --preset esp32p4          # flash + serial capture + parse results
#
# Manual flash + monitor:
#   ESPPORT=COM5 pwsh -NoProfile -File ../../scripts/nomsys.ps1 cmake --build --preset esp32p4 --target flash

cmake_minimum_required(VERSION 3.22)

set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_SOURCE_DIR}/../../components/sub0h264"
)

# Remove stale sdkconfig on configure so sdkconfig.defaults is always applied.
# IDF generates sdkconfig in the source dir and caches values that override
# sdkconfig.defaults on subsequent configures. This ensures a clean state.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/sdkconfig")
    file(REMOVE "${CMAKE_CURRENT_SOURCE_DIR}/sdkconfig")
    message(STATUS "Removed stale sdkconfig — regenerating from sdkconfig.defaults")
endif()

include($ENV{IDF_PATH}/tools/cmakev2/idf.cmake)
project(unit_tests CXX C ASM)
idf_project_init()
idf_project_default()

# ── CTest integration: flash + serial capture + result parsing ───────────
# Invokes cmake/esp32p4_flash_and_test.cmake which handles:
#   1. Parse flasher_args.json for flash offsets (CMake-native)
#   2. Flash via esptool (execute_process)
#   3. Capture serial output via Python helper
#   4. Parse doctest results (CMake-native)

include(CTest)
if(BUILD_TESTING)
    set(_cmake_dir "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake")

    # COM_PORT: override with -DCOM_PORT=COM3 or ESPPORT env var
    if(NOT DEFINED COM_PORT)
        if(DEFINED ENV{ESPPORT})
            set(COM_PORT "$ENV{ESPPORT}" CACHE STRING "Serial port for ESP32-P4")
        else()
            set(COM_PORT "COM5" CACHE STRING "Serial port for ESP32-P4")
        endif()
    endif()

    # Resolve IDF Python venv (has esptool + pyserial)
    if(DEFINED ENV{IDF_PYTHON_ENV_PATH})
        set(_idf_python "$ENV{IDF_PYTHON_ENV_PATH}/Scripts/python")
    elseif(DEFINED ENV{IDF_TOOLS_PATH})
        set(_idf_python "$ENV{IDF_TOOLS_PATH}/python/master/venv/Scripts/python")
    else()
        set(_idf_python "python")
    endif()

    add_test(
        NAME esp32p4_unit_tests
        COMMAND "${CMAKE_COMMAND}"
            -D "BUILD_DIR=${CMAKE_BINARY_DIR}"
            -D "COM_PORT=${COM_PORT}"
            -D "BOOT_TIMEOUT=15"
            -D "TEST_TIMEOUT=120"
            -D "LOG_DIR=${CMAKE_BINARY_DIR}/test_logs"
            -D "PYTHON=${_idf_python}"
            -P "${_cmake_dir}/esp32p4_flash_and_test.cmake"
    )

    set_tests_properties(esp32p4_unit_tests PROPERTIES
        TIMEOUT 180                         # CTest backstop (flash + boot + test + margin)
        RESOURCE_LOCK "COM_${COM_PORT}"     # Prevent parallel access to same port
        LABELS "hardware;esp32p4"           # Filter: ctest -L hardware
    )
endif()
