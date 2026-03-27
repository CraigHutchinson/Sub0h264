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

Working directory must be `test_apps/hello_h264/` for all ESP-IDF commands.

## Commit Rules

### Style
Follow `STYLE_GUIDE.md` for all C++ code. Key points:
- 4 spaces, no tabs
- `noexcept` on all hot-path functions
- `SUB0H264_` prefix for all configuration macros
- `sub0h264::` namespace for all public API
- Never include `<iostream>` (adds ~200KB on ESP32)

### Tests
- All new features must have corresponding tests in `tests/`
- Tests must pass locally before committing: `ctest --preset default`
- A git pre-push hook runs tests automatically: `git config core.hooksPath .githooks`

## Branch Strategy
- `main` — stable releases
- `develop` — active development
