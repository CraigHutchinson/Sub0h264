# Sub0h264 Project Rules

## Build & Test (Desktop)

```bash
cmake --preset default          # Configure
cmake --build --preset default  # Build
ctest --preset default          # Run tests
```

## Build (ESP32-P4)

**IMPORTANT:** All ESP-IDF cmake/idf.py commands MUST be run through the `nomsys.ps1` wrapper
to strip MSYS/Mingw env vars that cause IDF's Python check to reject the environment.
From bash shells (including Claude Code), prefix every IDF command with:
```bash
pwsh -NoProfile -File scripts/nomsys.ps1 <command> [args...]
```

```bash
# Configure
pwsh -NoProfile -File scripts/nomsys.ps1 cmake --preset esp32p4

# Build
pwsh -NoProfile -File scripts/nomsys.ps1 cmake --build --preset esp32p4

# Flash (COM5)
ESPPORT=COM5 pwsh -NoProfile -File scripts/nomsys.ps1 cmake --build --preset esp32p4 --target flash

# Monitor
pwsh -NoProfile -File scripts/nomsys.ps1 python -m esp_idf_monitor -p COM5 build/esp32p4/hello_h264.elf
```

Working directory must be `test_apps/hello_h264/` for hello-world commands.

## Test (ESP32-P4)

Working directory: `test_apps/unit_tests/`

```bash
# Configure + build
pwsh -NoProfile -File ../../scripts/nomsys.ps1 cmake --preset esp32p4
pwsh -NoProfile -File ../../scripts/nomsys.ps1 cmake --build --preset esp32p4

# Run tests on device (flash + serial capture + parse results)
ctest --preset esp32p4
```

CTest flashes the firmware, captures serial output, parses doctest results, and reports pass/fail.

## Commit Rules

### Style
Follow `STYLE_GUIDE.md` and `docs/ai-guides/coding-style.md` for all C++ code. Key points:
- 4 spaces, no tabs
- `noexcept` on all hot-path functions
- `SUB0H264_` prefix for all configuration macros
- `sub0h264::` namespace for all public API
- Never include `<iostream>` (adds ~200KB on ESP32)
- **No magic numbers** — all numeric literals must be named `constexpr` with spec references
- **No large stack allocations** — use `std::make_unique` for objects > 1KB
- Prefer full words over abbreviations (see permitted list in coding-style.md)

### Tests
- All new features must have corresponding tests in `tests/`
- Tests must pass locally before committing: `ctest --preset default`
- A git pre-push hook runs tests automatically: `git config core.hooksPath .githooks`

## Branch Strategy
- `main` — stable releases
- `develop` — active development
