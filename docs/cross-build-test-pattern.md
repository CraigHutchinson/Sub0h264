# Cross-Build + Hardware-in-Loop Test Pattern for ESP-IDF Projects

Reference documentation for running CTest on ESP32 targets with the same `cmake --preset X && cmake --build --preset X && ctest --preset X` workflow used for desktop builds.

---

## 1. Problem Statement

Standard CMake cross-compilation (`CMAKE_TOOLCHAIN_FILE`) does not work with ESP-IDF. The IDF build system (`idf.cmake`) replaces `project()` semantics entirely — it generates its own toolchain, sets `CMAKE_CROSSCOMPILING`, injects the component model, manages linker scripts, partition tables, and bootloader builds. A single `project()` cannot target both the host and an ESP32 device.

CTest expects executables it can run on the build host. Cross-compiled binaries run only on target hardware, requiring flash + serial capture + output parsing — none of which CTest handles natively.

## 2. Architecture

### Two separate CMake projects

```
project-root/
├── CMakeLists.txt              # Host project: project(MyLib), enable_testing(), add_test()
├── CMakePresets.json           # Host presets: default, debug, ci-msvc, ci-unix
├── tests/                      # Host tests (doctest/gtest, run directly)
└── test_apps/
    └── unit_tests/
        ├── CMakeLists.txt      # IDF project: idf.cmake, idf_project_init(), add_test()
        ├── CMakePresets.json   # IDF presets: esp32p4 (configure, build, test)
        ├── sdkconfig.defaults  # Target-specific Kconfig
        └── main/
            └── test_runner.cpp # Doctest with embedded fixtures
```

The host and IDF projects are **completely independent** — different `project()` calls, different binary directories, different toolchains. They share only the component source code (via `EXTRA_COMPONENT_DIRS`).

### Data flow

```
ctest --preset esp32p4                  (from test_apps/unit_tests/)
  └─ add_test() invokes:
     cmake -P ../../cmake/esp32p4_flash_and_test.cmake
       -D BUILD_DIR=build/esp32p4
       -D COM_PORT=COM5
       │
       ├─ 1. Parse flasher_args.json        [CMake-native: string(JSON)]
       ├─ 2. Flash via esptool               [CMake: execute_process()]
       ├─ 3. Capture serial output           [Python: ~130 lines]
       ├─ 4. Parse doctest results from log  [CMake-native: file(READ) + string(REGEX)]
       └─ 5. Exit 0 or FATAL_ERROR           [CTest sees pass/fail]
```

## 3. Why Not `CMAKE_CROSSCOMPILING_EMULATOR`?

This CMake variable was designed for QEMU-like emulators that transparently execute cross-compiled binaries. It does not work well with ESP-IDF because:

- IDF's `idf.cmake` already sets `CMAKE_CROSSCOMPILING` internally
- The "emulator" semantics conflict with the flash-then-capture workflow
- CTest passes the executable path as an argument, but ESP-IDF flashing requires the full build directory (bootloader, partition table, app binary, flash offsets)
- Explicit `add_test(COMMAND cmake -P ...)` is clearer, more debuggable, and doesn't fight the build system

## 4. The Flash Step — CMake-Native JSON Parsing

ESP-IDF generates `flasher_args.json` during build containing all flash parameters:

```json
{
    "extra_esptool_args": { "chip": "esp32p4", "before": "default_reset", "after": "hard_reset" },
    "flash_settings": { "flash_mode": "dio", "flash_size": "16MB", "flash_freq": "80m" },
    "flash_files": {
        "0x2000": "bootloader/bootloader.bin",
        "0x8000": "partition_table/partition-table.bin",
        "0x10000": "unit_tests.bin"
    }
}
```

The CMake -P script parses this natively (CMake 3.19+ `string(JSON ...)`):

```cmake
file(READ "${BUILD_DIR}/flasher_args.json" _json)
string(JSON _chip GET "${_json}" "extra_esptool_args" "chip")
string(JSON _flash_mode GET "${_json}" "flash_settings" "flash_mode")
# ... iterate flash_files for offset->binary pairs
```

Then flashes via:

```cmake
execute_process(
    COMMAND ${PYTHON} -m esptool --chip ${_chip} --port ${COM_PORT} --baud 921600
        write_flash --flash_mode ${_flash_mode} ... ${_flash_pairs}
    RESULT_VARIABLE _rc
    TIMEOUT 60
)
```

**Key advantage:** No hardcoded flash offsets. If the partition layout changes, `flasher_args.json` reflects it automatically.

## 5. Serial Capture — Thin Python Helper

CMake has no serial port API. A thin Python script (~130 lines) handles the serial phase only.

### Critical implementation details

**DTR/RTS before open:** Serial port opening can trigger an unintended device reset via the DTR/RTS auto-reset circuit. Set both to `False` before `open()`:

```python
ser = serial.Serial()
ser.port = args.port
ser.baudrate = 115200
ser.dtr = False    # Set BEFORE open
ser.rts = False    # Set BEFORE open
ser.open()
```

**64KB read buffer:** Doctest verbose output can burst. Default 4KB buffers overflow silently:

```python
ser.reset_input_buffer()  # Flush stale data from prior run
```

**Two-phase timeout:** Separates boot latency from test runtime for distinct failure diagnostics:

| Phase | Timeout | Watches for | Failure meaning |
|-------|---------|-------------|-----------------|
| Boot | 15s | Boot marker string (e.g., `"unit tests starting..."`) | Flash failed, wrong port, boot loop |
| Test | 120s | `[doctest] Status: SUCCESS!` or `FAILURE!` | Test hung, decode infinite loop |

**ANSI escape code stripping:** ESP-IDF log colorization (`CONFIG_LOG_COLORS=y`) emits ANSI codes even over raw UART. Strip with:

```python
ansi_re = re.compile(r"\x1b\[[0-9;]*[a-zA-Z]")
clean = ansi_re.sub("", raw_text)
```

**Last-match-wins with post-flush:** A crash during doctest cleanup (after `Status: SUCCESS!` is printed) must override the pass. Read for 2 additional seconds after the terminal pattern, checking crash markers:

```python
crash_patterns = ["Guru Meditation Error", "abort() was called", "Backtrace:"]
```

### Exit codes

| Code | Meaning | CTest result |
|------|---------|-------------|
| 0 | Tests passed | PASS |
| 1 | Tests failed | FAIL |
| 2 | Timeout (boot or test) | FAIL |
| 3 | Device crashed | FAIL |
| 4 | Serial port error | FAIL |

## 6. Port Safety — CTest RESOURCE_LOCK

CTest's built-in `RESOURCE_LOCK` property prevents parallel tests from competing for the same COM port:

```cmake
set_tests_properties(esp32p4_unit_tests PROPERTIES
    TIMEOUT 180                       # CTest backstop
    RESOURCE_LOCK "COM_${COM_PORT}"   # Serializes access
    LABELS "hardware;esp32p4"         # Filterable: ctest -L hardware
)
```

Cross-process safety is handled by the OS — opening an in-use COM port fails immediately with a clear error.

## 7. ESP32-P4 Specific Gotchas

### PSRAM verification

PSRAM initialization can silently fail. Add an explicit test:

```cpp
TEST_CASE("PSRAM available and sufficient") {
    size_t psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    CHECK(psram >= (2U * 1024U * 1024U));  // 2MB minimum for decode buffers
}
```

### Watchdog (WDT)

Set `CONFIG_ESP_TASK_WDT_PANIC=n` in `sdkconfig.defaults` for testing. This makes WDT timeouts log a warning instead of rebooting, allowing the serial capture to detect and report the timeout gracefully.

### Stack sizing

- Main task stack: 64KB (`CONFIG_ESP_MAIN_TASK_STACK_SIZE=65536`)
- **Never allocate > 1KB on the stack.** Use `std::make_unique` for large objects (e.g., decoder instances with parameter set arrays)
- Assert stack high-water mark after heavy operations:

```cpp
UBaseType_t hwm = uxTaskGetStackHighWaterMark(nullptr);
ESP_LOGI(TAG, "Stack HWM: %u bytes remaining", (unsigned)(hwm * sizeof(StackType_t)));
```

### FreeRTOS yield

Long decode loops must yield periodically to prevent task starvation:

```cpp
if (frameCount % 10U == 0U)
    vTaskDelay(pdMS_TO_TICKS(1));  // Yield to system tasks
```

### Stale sdkconfig

ESP-IDF generates `sdkconfig` in the source directory and caches values that override `sdkconfig.defaults` on subsequent configures. Auto-remove on configure:

```cmake
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/sdkconfig")
    file(REMOVE "${CMAKE_CURRENT_SOURCE_DIR}/sdkconfig")
endif()
```

## 8. MSYS Environment on Windows

Windows Git Bash injects MSYS/MinGW environment variables that cause ESP-IDF's Python environment check to fail. Two mitigations:

1. **Build step:** Use `nomsys.ps1` wrapper for `cmake --preset` and `cmake --build`:
   ```bash
   pwsh -NoProfile -File scripts/nomsys.ps1 cmake --preset esp32p4
   ```

2. **Test step:** `ctest --preset esp32p4` invokes `cmake -P flash-and-test.cmake`, which calls `python -m esptool` directly (bypassing IDF's `idf.py` which triggers the MSYS check). The CMakePresets.json sets explicit `PATH` with the IDF Python venv, so esptool is found without MSYS contamination.

## 9. Adapting for Other Projects

To use this pattern in a new ESP-IDF project:

1. **Copy** `cmake/esp32p4_flash_and_test.cmake` and `cmake/esp32p4_serial_capture.py`
2. **Adjust** the boot marker string to match your app's startup log
3. **Adjust** success/failure patterns to match your test framework's output
4. **Adjust** timeouts for your hardware's boot time and test duration
5. **Add** `include(CTest)` + `add_test()` to your IDF project's `CMakeLists.txt`
6. **Add** `testPresets` to your `CMakePresets.json`

The CMake script reads `flasher_args.json` dynamically, so it works with any ESP-IDF target (ESP32, ESP32-S3, ESP32-P4, etc.) without modification.

## 10. Complete Workflow

```bash
# Desktop tests (from project root)
cmake --preset default && cmake --build --preset default && ctest --preset default

# ESP32-P4 tests (from test_apps/unit_tests/)
cd test_apps/unit_tests
pwsh -NoProfile -File ../../scripts/nomsys.ps1 cmake --preset esp32p4
pwsh -NoProfile -File ../../scripts/nomsys.ps1 cmake --build --preset esp32p4
ctest --preset esp32p4    # Flashes, captures, parses — no manual monitoring needed
```

## 11. File Reference

| File | Purpose |
|------|---------|
| `cmake/esp32p4_flash_and_test.cmake` | CMake -P orchestrator (JSON parse, flash, invoke serial, parse result) |
| `cmake/esp32p4_serial_capture.py` | Python serial helper (DTR/RTS, two-phase timeout, ANSI strip) |
| `test_apps/unit_tests/CMakeLists.txt` | IDF project with `include(CTest)` + `add_test()` |
| `test_apps/unit_tests/CMakePresets.json` | Configure + build + test presets for ESP32-P4 |
| `test_apps/unit_tests/sdkconfig.defaults` | Target config (PSRAM, WDT, stack, optimization) |
| `test_apps/unit_tests/main/test_runner.cpp` | Doctest runner with embedded fixtures + hw verification |
