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
- **The ITU-T H.264 specification is the authoritative source** — always audit code against the spec, not against reference implementations. ffmpeg and libavc are interpretations we use as guides/references, but the spec defines correct behavior.
- **Always cite ITU-T H.264 spec sections** in code comments where the logic implements a spec formula, table, or algorithm (e.g., `// ITU-T H.264 §8.5.12.1 Table 8-15`)
- Include section (§), table, equation, or figure numbers for traceability
- When fixing bugs, reference the spec section that defines the correct behavior
- **Prefer spec-based code audit over reference-based comparison** — when debugging, trace the code flow against the spec's pseudo-code and syntax tables rather than comparing against libavc/ffmpeg output. This ensures correctness for ALL streams, not just the ones the reference implementation happens to handle.
- Cross-reference libavc/ffmpeg only as secondary validation after verifying against the spec
- **Track unimplemented spec sections** — clearly document which spec sections are not yet implemented (e.g., `// TODO §7.3.3.2: pred_weight_table()`), so missing support is visible and discoverable

### Tests
- All new features must have corresponding tests in `tests/`
- Tests must pass locally before committing: `ctest --preset default`
- A git pre-push hook runs tests automatically: `git config core.hooksPath .githooks`
- **Test against the H.264 spec** — include spec section/table references in test comments
- **PSNR vs raw source** — quality tests compare decoded output against the raw uncompressed source YUV (ground truth), NOT against another decoder's output (ffmpeg/libavc). The raw source sets the absolute quality bar; any conforming decoder should achieve the same PSNR within rounding.
- **Regression tests** — when fixing a bug, add a test that verifies the correct behavior and would fail with the old code
- Reference decoders (libavc, ffmpeg) are useful for **debugging** (tracing bit offsets, MB types) but are NOT the quality reference. The spec is authoritative; reference decoders are interpretations.

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

### Comparison Scripts
- `scripts/gen_frame_compare.py` — side-by-side PNG comparison images
- `scripts/check_chroma_mc.py` — per-MB chroma MC vs ffmpeg comparison
- `scripts/compare_idr_ref.py` — cross-decoder IDR chroma comparison
- `scripts/verify_total_zeros.py` — VLC table validation vs spec Table 9-7
- `scripts/trace_pframe_bits.py` — manual bitstream slice header + skip_run decode
- `scripts/check_rbsp_bits.py [offset]` — dumps raw RBSP bits and tries VLC matching

### CRC Comparison
CRC32 is a **hash** — close numeric values do NOT indicate similar images. CRC tests in `test_frame_verify.cpp` are **implementation regression tests** (snapshots of our current output). When fixing decode bugs, CRC values change and must be updated. They are NOT spec-referenced tests. Use PSNR vs raw source for quality validation.

## Debugging & Investigation Techniques

Proven approaches for finding H.264 decode bugs:

### 1. Per-frame PSNR comparison vs raw uncompressed source
Start here. Compare Y/U/V PSNR per frame against the raw source YUV (pre-encoding ground truth). Find the first frame with significant quality drop. This isolates I-frame vs P-frame issues. Do NOT compare against ffmpeg output at binary level — both decoders are spec interpretations. ffmpeg can be used for debugging reference but the raw source is the quality metric.

### 2. Per-MB pixel diff map
When a frame has errors, generate per-MB max/mean diff vs ffmpeg. This pinpoints which MBs are wrong. Pattern analysis (every 3rd row, rightmost column, etc.) reveals systematic issues (intra MBs, partition boundaries).

### 3. Bit offset alignment check
Compare our per-coded-MB bit positions against libavc's trace output. The `[LIBAVC-P] MB(x) CODED bit=y` traces can be extended by modifying the `i2_cur_mb_addr < N` filter in `ih264d_parse_pslice.c`. If bit positions diverge, the residual parsing consumed wrong number of bits.

### 4. Raw bitstream manual decode
Use `scripts/trace_pframe_bits.py` or inline Python to read RBSP bits at specific positions. Manually decode ue(v)/se(v) values to verify skip_run, mb_type, MVD independently of our C++ code.

### 5. Spec section cross-reference
For every code path, cite the specific ITU-T H.264 section/table/equation. Audit field-by-field against the spec syntax tables (§7.3.x). The spec is authoritative — reference decoders (libavc, ffmpeg) are interpretations.

### 6. VLC table validation
Use `static_assert` prefix-free checks (test_spec_tables.cpp) and cross-check against multiple reference implementations. The tc=3 total_zeros table has a non-monotonic length pattern that looks wrong but is correct.

### 7. Reference decoder output comparison (for debugging only)
Decode the same bitstream with libavc (`avcdec.exe`) and ffmpeg. Useful for tracing bit offsets and MB decisions. If both agree on a value and we differ, likely a bug in our code. But the SPEC is authoritative — reference decoders are interpretations, not ground truth. Quality measurement should always be PSNR vs raw source.

### Common pitfalls found
- **§7.3.4 skip_run**: After a skip_run is exhausted, the next MB is coded (no intervening skip_run read). Getting this wrong shifts the entire bitstream.
- **Intra in P-slice**: I-MB decoders write to `currentFrame_`, but P-slices use a separate DPB frame. Must copy decoded MB to the correct target.
- **Multi-reference**: ref_idx must be stored per-partition and used for both MC reference lookup AND MV predictor directional shortcuts (§8.4.1.3.1).
- **P_8x8 sub-partition**: MVDs must actually be stored (not just consumed for bit alignment). Inner scope variable shadowing can silently discard them.

## Branch Strategy
- `main` — stable releases
- `develop` — active development
