# Sub0h264 Project Rules

## Build & Test (Desktop)

```bash
cmake --preset default          # Configure
cmake --build --preset default  # Build
ctest --preset default          # Run tests
```

## Spec Source and Local Mirror

- Canonical ITU source (discover latest in-force revision here first):
  https://www.itu.int/rec/T-REC-H.264
- Revision pages are supersedable; do not hardcode a single historical revision as authoritative.
- Rebuild local mirror + normalized index with:

```bash
python scripts/sync_h264_spec.py
```

- Agent-first local lookup entrypoint:
  `docs/reference/itu/h264/{revision}/normalized/`
- Source provenance and resolved in-force revision are recorded in:
  `docs/reference/itu/h264/{revision}/normalized/manifest.json`
- **Look up specific spec text** (sections, tables, equations, pseudocode) with the `/spec-lookup` skill:
  ```
  /spec-lookup §9.3.3.1.3
  /spec-lookup Table 9-45
  /spec-lookup 8.5.12.1
  ```
  Requires `page-index.json` — generate it once after syncing:
  ```bash
  pip install pymupdf
  python scripts/extract_spec_page_index.py
  ```

## Bash — Command Execution Rules

These rules are **mandatory** — violations break the permission allow-list and waste the user's time re-approving commands.

### No combined or chained commands
- **Never** chain commands with `&&`, `||`, or `;` in a single Bash tool call.
- **Never** prefix a command with `cd /path &&` or pass `-C /path` to combine a directory change with another operation.
- Run **one command per Bash tool call**. If commands are independent, use parallel Bash tool calls instead of chaining.

### No inline shell scripts
- **Never** write multi-line inline shell scripts (heredocs, `bash -c '...'`, `for` loops, `if`/`then` blocks) directly in a Bash tool call.
- Instead, **write the script to a file** first (using the Write tool), then execute it:
  - Reusable scripts → `scripts/` directory (committed).
  - Temporary/investigative scripts → `scripts/tmp_*.sh` (gitignored). Promote to a permanent name if reused.
- Single-line pipes (e.g., `git log --oneline | head -5`) are acceptable when trivial, but anything beyond a simple pipe should become a script file.

### Why
The Bash permission allow-list matches on the **first token** of the command (e.g., `git`, `pwsh`, `cmake`, `python`). Prefixing with `cd`, wrapping in `bash -c`, or chaining with `&&` changes the first token and forces the user to manually approve every invocation.

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

### Constexpr Table Generation from Spec Formulas
- **When a lookup table can be derived from a spec formula, implement it as a `constexpr` function or IIFE in C++** — do NOT copy raw numeric arrays from reference implementations.
- The `constexpr` code expresses the spec formula directly, making the derivation auditable and self-documenting.
- Use `static_assert` to spot-check computed values against known spec entries.
- Example: zigzag tables, dequant scale factors, CABAC rangeTabLPS (Table 9-45), state transition tables.
- **Why**: Tables copied from reference decoders (libavc, ffmpeg) may contain transcription errors or use a different encoding. A `constexpr` formula guarantees the table matches the spec and can be verified at compile time. This caught the CABAC quality bug where a packed table differed from the spec.
- If the spec formula is too complex for `constexpr` (e.g., requires runtime data), use a clearly documented initialization function with spec references in every line.

### Spec References
- **The ITU-T H.264 specification is the authoritative source** — always audit code against the spec, not against reference implementations. ffmpeg and libavc are interpretations we use as guides/references, but the spec defines correct behavior.
- **Always cite ITU-T H.264 spec sections** in code comments where the logic implements a spec formula, table, or algorithm (e.g., `// ITU-T H.264 §8.5.12.1 Table 8-15`)
- Include section (§), table, equation, or figure numbers for traceability
- When fixing bugs, reference the spec section that defines the correct behavior
- **Prefer spec-based code audit over reference-based comparison** — when debugging, trace the code flow against the spec's pseudo-code and syntax tables rather than comparing against libavc/ffmpeg output. This ensures correctness for ALL streams, not just the ones the reference implementation happens to handle.
- Cross-reference libavc/ffmpeg only as secondary validation after verifying against the spec
- **Track unimplemented spec sections** — clearly document which spec sections are not yet implemented (e.g., `// TODO §7.3.3.2: pred_weight_table()`), so missing support is visible and discoverable

### Spec-Only Agent Verification Workflow
When a CABAC or complex decode issue cannot be resolved by code inspection:

1. **Create a minimal reproduction fixture** — the smallest possible H.264 bitstream that triggers the bug (e.g., 1MB uniform value, single-frame IDR). Use `scripts/tmp_gen_cabac_minimal.py` as template.

2. **Launch a spec-only agent** in an isolated worktree to decode the fixture from the ITU-T H.264 specification only. The agent must NOT read our decoder code (`components/sub0h264/src/`) or reference implementations (`docs/reference/libavc/`). It CAN use web search for spec table values and read shared infrastructure (bitstream.hpp, frame.hpp, sps.hpp, pps.hpp).

3. **The agent traces the decode bit-by-bit**: slice header parsing, CABAC init, mb_type, prediction modes, CBP, residual coefficients, reconstruction. It produces the expected pixel output.

4. **Compare the agent's independent decode against our decoder** to find the first point of divergence. The agent's fresh perspective, uncontaminated by our assumptions, often identifies bugs that hours of reasoning miss.

**Example**: The condTermFlag bug (unavailable neighbor → 0, not 1) was found because a spec-only agent independently decoded mb_type as I_16x16 using ctx[3] (ctxInc=0), while our code used ctx[5] (ctxInc=2). The agent had no prior assumption about which context index to use — it derived it fresh from the spec.

**When to use this workflow**: When the CABAC engine arithmetic is verified correct but the decode still produces wrong output. When reasoning about the issue goes in circles for >30 minutes. When our Python traces reproduce the same bug (indicating shared assumptions).

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
- `BlockResidual` — per-block CAVLC/CABAC decode stats (nC, totalCoeff, bits) + raw coefficients
- `BlockCoeffs` — dequantized coefficient array (16 or 64 entries)
- `BlockPixels` — prediction[16] + output[16] for per-block reconstruction verification
- `BlockPredMode` — intra prediction mode + MPM for each 4x4 block
- `LumaDcDequant` — luma I_16x16 DC after Hadamard + scaling
- `MvPrediction` — motion vector predictor, delta, and final MV per partition

### Usage in Tests
```cpp
decoder->trace().setCallback([&](const TraceEvent& e) {
    if (e.type == TraceEventType::ChromaDcDequant && e.mbX == 5 && e.mbY == 0)
        // capture values from e.data
});
```

### Reference Decoders for Comparison
- **JM** — ITU-T official H.264 reference decoder at `docs/reference/jm/`. The authoritative ground truth for CABAC conformance testing. Lock-step bin trace instrumentation preserved in `tools/jm_trace/jm_trace.patch`. Use `sub0h264_trace --level entropy` to generate comparable output. **Always use JM as the reference for bitstream parsing questions — do NOT write custom Python parsers, which introduce their own bugs.**
- **libavc** — cloned at `docs/reference/libavc/`. Build with MinGW: `cmake -G "MinGW Makefiles" -DCMAKE_C_COMPILER=gcc`. The `avcdec` tool can decode fixtures. Instrumented traces compare per-MB bit offsets and dequant values.
- **ffmpeg** — used for reference YUV output: `ffmpeg -i fixture.h264 -vframes 1 -pix_fmt yuv420p ref.yuv`

### Investigation & Comparison Scripts
- `scripts/gen_frame_compare.py` — generates side-by-side PNG comparison images
- `scripts/gen_diff_image.py` — visual diff: [ours | ffmpeg | diff | |diff|] with MB grid
- `scripts/compare_mb_pixels.py` — per-MB pixel diff between two YUV files
- `scripts/find_first_pixel_diff.py` — finds exact first differing pixel in two YUV files
- `scripts/diff_traces.py` — diff our block-level trace against libavc coefficient dump
- `scripts/check_chroma_quality.py` — per-plane (Y/U/V) PSNR comparison
- `scripts/check_chroma_mc.py` — per-MB chroma MC vs ffmpeg comparison
- `scripts/compare_idr_ref.py` — cross-decoder IDR chroma comparison
- `scripts/verify_total_zeros.py` — VLC table validation vs spec Table 9-7
- `scripts/trace_pframe_bits.py` — manual bitstream slice header + skip_run decode
- `scripts/check_rbsp_bits.py [offset]` — dumps raw RBSP bits and tries VLC matching
- `scripts/trace_cabac_mb0.py` — pure-Python CABAC engine with spec Table 9-45 rangeTabLPS

### Full Trace Tool
- `test_apps/trace/` — `sub0h264_trace` CLI: decode + dump YUV + per-MB/block trace
- `tests/test_full_trace.cpp` — generates comprehensive per-block trace file for libavc comparison
- Usage: `sub0h264_trace input.h264 --dump output.yuv --level block --mb 3,0`

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

### 8. Full block-level trace + libavc comparison
Use `test_full_trace.cpp` to generate a per-block trace (prediction mode, raw coefficients, dequant coefficients, output pixels) and `scripts/diff_traces.py` to compare coefficient-by-coefficient against libavc's dump. This definitively identifies whether the bug is in CAVLC/CABAC, dequant, IDCT, or prediction.

### 9. CABAC bin-by-bin engine trace
For CABAC bugs, use `CabacEngine::enableBinTrace(FILE*)` to log every decodeBin result. Compare against `scripts/trace_cabac_mb0.py` which has a pure-Python CABAC engine using the spec's Table 9-45 rangeLPS directly. Divergence between the spec-table Python engine and our C++ engine indicates a bug in our packed `cCabacTable`.

### Common pitfalls found
- **§7.3.4 skip_run**: After a skip_run is exhausted, the next MB is coded (no intervening skip_run read). Getting this wrong shifts the entire bitstream.
- **Intra in P-slice**: I-MB decoders write to `currentFrame_`, but P-slices use a separate DPB frame. Must copy decoded MB to the correct target.
- **Multi-reference**: ref_idx must be stored per-partition and used for both MC reference lookup AND MV predictor directional shortcuts (§8.4.1.3.1).
- **P_8x8 sub-partition**: MVDs must actually be stored (not just consumed for bit alignment). Inner scope variable shadowing can silently discard them.

## Spec Compliance Status

### Fully implemented and tested
- **NAL parsing** — §7.3.1 (types 1,5,7,8)
- **SPS/PPS** — §7.3.2 (including High profile extensions, scaling lists parsed)
- **Slice header** — §7.3.3 (I/P slices, pred_weight_table, ref list reordering, MMCO)
- **CAVLC** — §9.2 (coeff_token, levels, total_zeros, run_before — all verified bit-exact vs libavc)
- **CABAC engine** — §9.3.1-9.3.3 (arithmetic engine, context init, renormalize — verified)
- **I_4x4 prediction** — §8.3.1.2 (all 9 modes including DDR fix)
- **I_8x8 prediction** — §8.3.2 (all 9 modes with reference sample filtering)
- **I_16x16 prediction** — §8.3.3 (V/H/DC/Plane)
- **Chroma prediction** — §8.3.4 (DC/H/V/Plane)
- **4x4 IDCT** — §8.5.12.2 (matches libavc, verified with 7 unit tests)
- **8x8 IDCT** — §8.5.12.2 (full butterfly implementation)
- **4x4/8x8 dequant** — §8.5.12.1 (matches spec formula exactly)
- **Deblocking** — §8.7 (BS precomputation, alpha/beta tables)
- **Inter P_L0_16x16/16x8/8x16** — §8.4.1 (MV prediction, multi-ref)
- **Inter P_8x8** — §8.4.1 (sub-partition MVDs, per-8x8 MC)
- **Luma MC** — §8.4.2.2 (6-tap filter, quarter-pel, fast paths)
- **Chroma MC** — §8.4.2.2 (bilinear, eighth-pel)
- **Weighted prediction** — §8.4.2.3 (parsed + applied for P-slices, all partition types)
- **Reference list reordering** — §8.2.4.3 (L0 list construction + reordering commands)
- **DPB MMCO** — §8.2.5.4 (all 6 operations: unmark short/long, assign LT, mark all unused)
- **8x8 inter transform** — §7.3.5 (CABAC P-slices: flag decoded, 8x8 residual + IDCT applied)

### Implemented but quality issues
- **CABAC residual decode** — §9.3.3.1.3: Coefficient decode matches independent Python reference, but output pixels wrong. Likely root cause: `cCabacTable[128][4]` (packed rangeTabLPS) may differ from spec Table 9-45. Active investigation — see `project_cabac_investigation.md` in memory.

### Not yet implemented
- **B-slices** — §7.3.4, §8.4.1.1 (slice header parsed, decode returns error)
- **MBAFF** — §7.3.3 (field_pic_flag parsed but interlace not decoded)
- **SI/SP slices** — §7.3.3 (slice_qs_delta parsed, no decode)
- **Constrained intra** — §8.3 (flag parsed, not enforced in prediction)
- **Scaling list application** — §8.5.12.1 (lists parsed in SPS/PPS, not applied to dequant)
- **FMO** — §7.3.2.3 (slice groups not supported)
- **ASO** — arbitrary slice ordering not handled
- **Error concealment** — no recovery from bitstream errors

## Branch Strategy
- `main` — stable releases
- `develop` — active development
