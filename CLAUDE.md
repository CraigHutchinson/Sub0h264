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

### Spec References
- **Always cite ITU-T H.264 spec sections** in code comments where the logic implements a spec formula, table, or algorithm (e.g., `// ITU-T H.264 §8.5.12.1 Table 8-15`)
- Include section (§), table, equation, or figure numbers for traceability
- When fixing bugs, reference the spec section that defines the correct behavior
- Cross-reference libavc (Android Open Source Project) function names when validating against a known-good implementation

### Tests
- All new features must have corresponding tests in `tests/`
- Tests must pass locally before committing: `ctest --preset default`
- A git pre-push hook runs tests automatically: `git config core.hooksPath .githooks`
- **Test against the H.264 spec** — include spec section/table references in test comments
- **Regression tests** — when fixing a bug, add a test that verifies the correct behavior and would fail with the old code
- Reference decoders (libavc, ffmpeg) can be used to generate ground-truth values for tests

## Decoder Trace System

The decoder has a built-in trace system (`decode_trace.hpp`) for debugging and validation.

### Trace Modes
1. **Printf trace** — compile with `-DSUB0H264_TRACE=1`. Set `trace_.enabled = true` and use `filterMbX`/`filterMbY` to filter specific MBs. Zero cost when disabled.
2. **Callback trace** — always available, even in release builds. Set a callback via `decoder.trace().setCallback(fn)` to capture structured `TraceEvent` records programmatically. Used by unit tests to validate intermediate decode values against libavc reference.

### Trace Events
- `MbStart/MbEnd` — bit offset at MB boundaries
- `ChromaDcDequant` — dequanted chroma DC values after Hadamard + scaling
- `BlockResidual` — per-block CAVLC decode stats (nC, totalCoeff, bits)
- `LumaDcDequant` — luma I_16x16 DC after Hadamard + scaling

### Usage in Tests
```cpp
decoder->trace().setCallback([&](const TraceEvent& e) {
    if (e.type == TraceEventType::ChromaDcDequant && e.mbX == 5 && e.mbY == 0)
        // capture values from e.data
});
```

### Reference Decoders for Comparison
- **libavc** — cloned at `docs/reference/libavc/`. Build with MinGW: `cmake -G "MinGW Makefiles" -DCMAKE_C_COMPILER=gcc`. The `avcdec` tool can decode fixtures. Instrumented traces compare per-MB bit offsets and dequant values.
- **h264bitstream** — at `docs/reference/h264bitstream/`. Header-only bitstream analyzer. The `h264_analyze` tool parses NAL/slice headers.
- **ffmpeg** — used for reference YUV output: `ffmpeg -i fixture.h264 -vframes 1 -pix_fmt yuv420p ref.yuv`

### Comparison Scripts
- `scripts/gen_frame_compare.py` — generates side-by-side PNG comparison images
- `scripts/analyze_decode.py` — analyzes YUV dump for decode coverage (MB counts, partial blocks)
- `scripts/compare_block7.py [mbx] [mby]` — pixel-level comparison for a specific MB
- `scripts/gen_vlc_from_libavc.py` — generates coeff_token VLC tables from libavc reference
- `scripts/check_rbsp_bits.py [offset]` — dumps raw RBSP bits and tries VLC matching

### CRC Comparison
CRC32 is a **hash** — close numeric values do NOT indicate similar images. Use CRC as a **binary pass/fail** check only. For progress tracking, use pixel-level metrics (Y/U/V diff counts and max values).

## Branch Strategy
- `main` — stable releases
- `develop` — active development
