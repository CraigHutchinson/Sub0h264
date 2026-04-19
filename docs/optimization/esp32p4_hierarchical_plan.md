# Plan: Hierarchical Optimization Strategy for ESP32-P4

## Context

The decoder currently achieves 12-33 fps on ESP32-P4 (360 MHz RISC-V, in-order,
32 KB L1, 32 MB PSRAM @ 200 MHz) across 640x480 content. The worst case is
"ball" (bouncing ball on noise) at **12 fps** — this is the crux. Most content
with motion + residual hovers around real-time.

**Primary objective:** Consistent **25 fps minimum** across all content. Above
25 fps provides no marketable benefit; reducing the worst case is the goal.

**Key constraints:**
- ESP32-P4 is the priority target; x86 SIMD is "extra"
- No PIE usage yet — document opportunities for future workflow
- Prefer proper fixes over hacks; API/architectural refactors welcome
- Maintain spec compliance and bit-exact output (PSNR regression < 0.01 dB)
- Strong KPI metric system required

---

## Per-opportunity implementation docs

Before any optimisation lands in code, the approach for that specific
opportunity must be captured in a standalone doc under
[`docs/optimization/opportunities/`](opportunities/). Each doc is designed to
be handed to an agent as a self-contained brief: options reasoned, chosen
approach stated, files/pseudocode identified, validation gates defined. The
main plan below remains the roadmap and ordering; the per-opportunity docs
are the implementation contract.

| ID | Title | Doc |
|----|-------|-----|
| L1.1 | Row-based decode+deblock streaming pipeline | [opportunities/L1.1_row_based_deblock.md](opportunities/L1.1_row_based_deblock.md) |
| L1.2 | Eliminate currentFrame_ duplication | [opportunities/L1.2_eliminate_currentframe.md](opportunities/L1.2_eliminate_currentframe.md) |
| L1.3 | Static max-bounded context arrays | [opportunities/L1.3_static_context_arrays.md](opportunities/L1.3_static_context_arrays.md) |
| L2.1 | Diagonal MC 2-pass separable filter | [opportunities/L2.1_diagonal_mc_2pass.md](opportunities/L2.1_diagonal_mc_2pass.md) |
| L2.2 | Skip-MB / zero-MV fast paths | [opportunities/L2.2_skip_zero_mv_fast_path.md](opportunities/L2.2_skip_zero_mv_fast_path.md) |
| L2.3 | Deblocking BS cache | [opportunities/L2.3_deblock_bs_cache.md](opportunities/L2.3_deblock_bs_cache.md) |
| L2.4 | Horizontal-edge column-major transpose | [opportunities/L2.4_horizontal_edge_transpose.md](opportunities/L2.4_horizontal_edge_transpose.md) |
| L3.1 | Reference prefetch into stack buffer | [opportunities/L3.1_reference_prefetch.md](opportunities/L3.1_reference_prefetch.md) |
| L3.2 | Frame buffer cache-line alignment | [opportunities/L3.2_frame_alignment.md](opportunities/L3.2_frame_alignment.md) |
| L3.3 | Direct-to-frame MC for zero-CBP MBs | [opportunities/L3.3_direct_frame_mc.md](opportunities/L3.3_direct_frame_mc.md) |
| L3.4 | Per-MB context struct packing | [opportunities/L3.4_mb_context_packing.md](opportunities/L3.4_mb_context_packing.md) |
| L4.1 | Branchless clipU8 | [opportunities/L4.1_branchless_clipu8.md](opportunities/L4.1_branchless_clipu8.md) |
| L4.2 | Deblocking inline row pointers | [opportunities/L4.2_deblock_inline_rows.md](opportunities/L4.2_deblock_inline_rows.md) |
| L4.3 | CAVLC coeff_token LUT | [opportunities/L4.3_cavlc_coeff_token_lut.md](opportunities/L4.3_cavlc_coeff_token_lut.md) |
| L4.4 | Fully unrolled 4×4 IDCT | [opportunities/L4.4_idct_4x4_unroll.md](opportunities/L4.4_idct_4x4_unroll.md) |
| L4.5 | getSample elimination (compounds with L3.1) | [opportunities/L4.5_getsample_elimination.md](opportunities/L4.5_getsample_elimination.md) |
| L5.1 | Compiler hints + alignment | [opportunities/L5.1_compiler_hints.md](opportunities/L5.1_compiler_hints.md) |
| L5.2 | BitReader cache refill | [opportunities/L5.2_bitreader_refill.md](opportunities/L5.2_bitreader_refill.md) |
| L5.3 | PIE/SIMD opportunity catalog (doc-only) | [opportunities/L5.3_pie_simd_catalog.md](opportunities/L5.3_pie_simd_catalog.md) |

**Rule:** an agent MUST read the opportunity doc before writing code. If the
chosen approach changes mid-implementation, update the doc first — the doc is
the source of truth for the commit's approach, and the commit message cites
both the plan phase and the opportunity doc.

---

## ✅ Prerequisite (Phase P0) — RESOLVED 2026-04-18

**Tapo IDR now 99 dB bit-exact across all 119 frames** (fixed via 11 CABAC
bugs + P-inter 4x4 zigzag unscan + CABAC I_8x8/DC bugs — see
`docs/known_issues.md` sessions 9-11). All 6 wstress synthetic fixtures and
wstress_p8x8_sub also 99 dB bit-exact. JM lock-step confirms bin-for-bin
match on P-frames too.

The historical P0 investigation notes below are kept for audit. Optimisation
work may now proceed against the bit-exact baseline.

---

## Historical P0 investigation (retained for audit)

**Status: OPEN** (confirmed 2026-04-17 — Tapo IDR PSNR 6.43 dB vs raw source)

Before any optimization work begins, the Tapo C110 stream's CABAC decode
divergence at ~MB(15,0) must be resolved. Per `docs/known_issues.md`:

> IDR frame diverges at ~MB(15,0): first 15 MBs correct, then ~6 dB PSNR.
> 320x240 CABAC streams decode at 51+ dB — issue is resolution-specific.
> Not caused by DPB or MMCO — the IDR I-slice itself decodes wrong pixels.
> Needs investigation: likely a CABAC context or residual path issue at
> wider frames.

**Why this blocks optimization work:**
1. Tapo is the production-target fixture — optimizing a broken decode produces
   meaningless fps numbers
2. The KPI system measures fps on this stream; quality gate ensures we're not
   "faster by being more wrong"
3. May require structural changes that interact with optimization surface
   (e.g. if it's a CABAC context indexing bug at MB column > 15)

**Investigation approach:**
1. Capture lock-step CABAC entropy trace via `sub0h264_trace --level entropy`
2. Compare against JM reference decoder bin-by-bin
3. Identify first divergent bin at or before MB(15,0)
4. Trace root cause via the established JM-comparison workflow (see
   `project_cabac_investigation.md` memory for methodology)

**Exit criteria:** Tapo IDR Y-PSNR ≥ 40 dB vs ffmpeg reference YUV, all MBs
match JM bin-for-bin. Close the active issue in `docs/known_issues.md`.

**No optimization phases start until P0 is resolved.**

### P0 Brainstorm — Related Issue Hypotheses & Test Gaps

The bug manifests as: first 15 MBs correct, diverges from pixel (253, 0) with
diff=1 (then compounds). MB(15,0) = column 15 row 0. **320x240 CABAC works
(20 MBs wide), 640x368 CABAC fails — so this is NOT a pure "width > 15" issue**;
it's something resolution-specific that manifests at column 15 in wide frames.

**Known test coverage gaps** (why this slipped through):
- ❌ No CABAC fixture between 320x240 (20 MBs wide) and 640x480 (40 MBs wide)
- ❌ No frames with non-16-aligned height (all fixtures are 16-aligned)
- ❌ No fixtures exercising SPS frame cropping
- ❌ No single-row ultra-wide fixtures (16x1, 20x1, 40x1 MB)
- ❌ No fixtures exercising mb_qp_delta across wide rows
- ❌ No fixtures exercising specifically I_8x8 (High profile) at wide resolutions
- ❌ Tapo capture itself is our only "real" High profile test

**Hypothesis list (ranked by likelihood):**

| # | Hypothesis | How to verify |
|---|-----------|----------------|
| 1 | **Cumulative rounding** in intra prediction neighbor sampling at wide frames — off-by-one compounds | Generate 640x16 I_16x16 plane-mode fixture; check per-MB PSNR |
| 2 | **Intra prediction top-right neighbor unavailable logic** — 320x240 has special case at MB 19 (last col), 640 exposes MB 15+ | Audit intra_pred.hpp for `topRightAvailable` edge cases at column 15 |
| 3 | **Frame cropping misapplied** during reconstruction — we decode 640x368 but ffmpeg reference is 640x360 | Our PSNR script compares first 360 rows only; this should be fine. BUT — if cropping params affect intra prediction neighbor availability, it changes decode |
| 4 | **CABAC neighbor context array width** — `cabacNeighbor_.init(widthInMbs, heightInMbs)` may have a bug with width > 20 | Grep cabac_neighbor.hpp for fixed-width assumptions |
| 5 | **I_8x8 intra prediction** at MB column 15+ has a bug | Generate 16x1 MB I_8x8 CABAC fixture; PSNR per MB |
| 6 | **8x8 IDCT/dequant** triggers at I_8x8 (needs `transform_size_8x8_flag = 1`); may have wide-frame issue | Test I_8x8 at 640 wide vs 320 wide |
| 7 | **Intra mode buffer overflow** — mbIntra4x4Modes_ sized correctly? Array bounds at wide frames | Audit decoder.hpp:562 — `totalMbs * 16U` should be fine but verify |
| 8 | **Chroma DC Hadamard at wide frames** — 2x2 DC hadamard result depends on QP which depends on mb_qp_delta cumulative across wide rows | Test chroma-heavy wide fixture |
| 9 | **Signed byte overflow** — some `int8_t` counter rolling over at column 16 | Grep for `int8_t` or `char` used as counters |
| 10 | **Deblocking not the issue** — bug is in IDR which has no deblock-across-slice-boundary issue; disable deblock to confirm | Build with `SUB0H264_SKIP_DEBLOCK`, re-measure PSNR |

**Suggested new synthetic fixtures to add:**

| Fixture | Size | Purpose |
|---------|------|---------|
| `cabac_16mb_wide_row.h264` | 16×1 MB | Isolate column-wise bugs in CABAC path |
| `cabac_24mb_wide_row.h264` | 24×1 MB | Between 20 (works) and 40 (fails) |
| `cabac_40mb_wide_row.h264` | 40×1 MB | Reproduce Tapo-style width with single row |
| `cabac_640x368_idr_only.h264` | 40×23 MB, IDR | Minimal repro matching Tapo dimensions |
| `cabac_640x368_i8x8.h264` | 40×23 MB, force I_8x8 | Isolate I_8x8 at wide frame |
| `cabac_640x368_i4x4.h264` | 40×23 MB, force I_4x4 | Isolate I_4x4 at wide frame |
| `cabac_640x368_i16x16.h264` | 40×23 MB, force I_16x16 | Isolate I_16x16 at wide frame |
| `cabac_320x360_cropped.h264` | frame_cropping enabled | Isolate cropping parser/decode interaction |

This matrix would let us bisect: which intra mode, which resolution width,
which height-alignment condition triggers the bug.

**Recommended P0 investigation order:**
1. **Generate the wide-row synthetics** — fast to produce with ffmpeg, adds
   regression coverage permanently ✓ DONE
2. **Test hypothesis 10 first** (disable deblock) — 30-second experiment, rules
   out the entire deblocking path ✓ DONE — ruled out
3. **Bisect by intra mode** — which of I_4x4 / I_8x8 / I_16x16 fails at width 40?
   ✓ DONE — isolated 3 I_8x8 bugs + 1 I_16x16 DC-only bug
4. **Once mode isolated, lock-step JM compare** at MB(15,0) of the minimal
   failing fixture — ⏳ NEXT SESSION

### P0 Investigation state (2026-04-17, end of session 2)

**3 bugs fixed:**
1. I_8x8 block-3 top-right availability (§6.4.10.4)
2. 8x8 IDCT h(7) butterfly wrong input (§8.5.12.2 Eq 8-332)
3. 8x8 IDCT output sign at positions 1 and 6 (§8.5.12.2 Eq 8-338)

**Remaining bugs (separate and independent):**
- **Tapo C110 at 6.59 dB** — Per-MB mode trace revealed MB 15 uses
  **I_16x16 mbType=3** (DC-only), NOT I_8x8 as initially assumed.
  Next step: lock-step JM comparison of MB 15 DC coefficient decode.
- **wstress gradient fixtures at 25 dB** — separate I_8x8 bug. wstress
  uses `-x264opts analyse=i8x8` to force I_8x8, exposing a subtle IDCT
  rounding or intra prediction issue in real-content coefficient patterns.
  Wstress_wide24 passes at 55 dB because x264 chose I_16x16/I_4x4 for
  that content.

**Diagnostic available** via `SUB0H264_P0_I8X8_DIAG` (misnamed — covers
all intra modes' MB entry logging on row 0).

### P0 Investigation Results (2026-04-17)

**Hypothesis #10 (deblock): RULED OUT** — disabling deblock produces identical
6.43 dB PSNR on Tapo IDR. Bug is in intra / entropy / transform paths.

**Bisect results (new width-stress synthetics at 640x368):**

| Fixture | Content | Result |
|---------|---------|--------|
| `wstress_tapo_size_baseline_640x368` | CAVLC gradient | **99 dB PASS** |
| `wstress_tapo_size_i16x16_640x368` | CABAC I_16x16 only | **99 dB PASS** |
| `wstress_tapo_size_i4x4_640x368` | CABAC I_4x4 only | **99 dB PASS** |
| `wstress_tapo_size_i8x8_640x368` | **CABAC I_8x8 only** | **15 dB FAIL** |
| `wstress_tapo_size_gradient_640x368` | CABAC mixed | 15 dB FAIL |
| `wstress_wide16_gradient_256x16` | CABAC 16-wide | 28 dB DEGRADED |
| `wstress_wide24_gradient_384x16` | CABAC 24-wide | 28 dB DEGRADED |
| `wstress_wide40_gradient_640x16` | CABAC 40-wide | 20 dB FAIL |
| `wstress_wide40_baseline_640x16` | CAVLC 40-wide | 99 dB PASS |

**ROOT CAUSE ISOLATED:**
- CAVLC works at all widths → width-dependency is NOT in bitstream parsing
  or reconstruction
- CABAC I_16x16 works → 16x16 intra prediction + entropy are fine
- CABAC I_4x4 works → 4x4 intra prediction + entropy are fine
- **CABAC I_8x8 FAILS** → bug is in the **I_8x8 intra prediction or 8x8
  transform path** under CABAC
- PSNR degrades with width (28→28→20 dB), suggesting **cumulative error
  propagating horizontally via top-row neighbor samples** in I_8x8
  intra prediction

**Revised P0 hypothesis list (narrowed):**

| # | Hypothesis | Evidence |
|---|-----------|----------|
| A | I_8x8 intra prediction neighbor sampling (top-right availability at MB columns 16+) | Degrading-with-width pattern |
| B | I_8x8 reference sample filtering (§8.3.2.1, 121 low-pass filter) applied incorrectly at wider frames | Every I_8x8 decode triggers this |
| C | 8x8 IDCT + dequant path issue (only 8x8-transform MBs hit this) | Explains why I_16x16 and I_4x4 work |
| D | `transform_size_8x8_flag` CABAC context wrong at wider frames (we fixed this once; might have regression or additional case) | Known historical fuzz |
| E | I_8x8 intra mode decode (rem_intra8x8_pred_mode CABAC binarization) | Only I_8x8 path |

**Next action:** Use new `wstress_tapo_size_i8x8_640x368.h264` as the minimal
repro fixture for JM lock-step comparison. The failing fixture is only 4650
bytes and ~1 frame, trivial to trace bin-by-bin.

---

## Profile Breakdown (ESP32-P4, 640x480 real camera content)

| Section | % of time | Frame budget @ 12 fps | Target @ 25 fps |
|---------|-----------|------------------------|-----------------|
| Inter MB (motion compensation) | **67.7%** | 56.4 ms | **≤ 27 ms** |
| Deblocking filter | 13.8% | 11.5 ms | ≤ 5.5 ms |
| Entropy + transform | 16.4% | 13.7 ms | ≤ 6.5 ms |
| Intra MB | 2.0% | 1.7 ms | ≤ 0.8 ms |
| Frame sync/overhead | 0.1% | — | — |
| **Total** | 100% | 83.3 ms | **40 ms** |

**Ball benchmark is worst case** because noise → every MB has residual (no
skip fast path), and random MVs → all 16 fractional positions exercised,
including the expensive diagonal positions (j2D, e, f, i, p, q).

---

## The 5 Layers

### Layer 1 — Architecture (frame-level)

#### L1.1 Row-based decode+deblock streaming pipeline
**Files:** `decoder.hpp` (decodeSlice MB loop, lines 642-865; deblock pass, lines 890-906)
**Problem:** Two-pass (decode all → deblock all) means deblock re-reads a 460 KB
frame from PSRAM. Deblock is essentially PSRAM-bound.
**Fix:** After each MB row's decode completes, immediately deblock the previous
row (top edges now available). Two rows (~20 KB) stay warm in L1.
**Expected:** +2-3 fps on ball, +15-20% deblock-bound content

#### L1.2 Eliminate `currentFrame_` duplication
**Files:** `decoder.hpp` (lines 546-551, 912-926)
**Problem:** Fallback path still does 460 KB memcpy per frame when
`activeFrame_ != decodeTarget`.
**Fix:** Always decode into DPB target. Remove `currentFrame_` member per
TODO at line 532.
**Expected:** +1-2% (mostly already achieved)

#### L1.3 Static max-bounded context arrays
**Files:** `decoder.hpp` (lines 557-562), context members
**Problem:** Per-frame `std::vector::resize` of 6 arrays (~284 KB total).
**Fix:** Use `std::array` pre-allocated at construction for max supported
resolution. Eliminates heap churn during decode.
**Expected:** +1%

---

### Layer 2 — Algorithm (MB-level)

#### L2.1 Diagonal MC fast path with row-cached intermediates ⭐ HIGHEST IMPACT
**Files:** `inter_pred.hpp` (lines 226-322, diagonal dispatch)
**Problem:** Positions (1-3,1-3) call `j2D` per pixel → 6×6=36 `getSample`
calls per output pixel, each with 2 clamp branches. For a 16×16 block at (2,2):
256 × 36 = 9,216 calls. Ball benchmark exercises these heavily.
**Fix:** Two-pass separable filter:
1. Pre-compute horizontal 6-tap into `int16_t` temp buffer of size
   `(width) × (height+5)` (unclipped intermediates, per §8.4.2.2.1)
2. Run vertical 6-tap on temp buffer with `(sum+512)>>10` final clip
Specialize the 9 diagonal positions. For quarter-pel positions (e, f, i, j, p, q)
reuse cached horizontal/vertical half-pel rows.
**Expected:** +3-4 fps on ball (25-30% improvement in MC cost)
**Risk:** Medium — must preserve unclipped intermediate path per spec. Validate
with bit-exact test across all 16 fractional positions.

#### L2.2 Skip MB & zero-MV fast paths
**Files:** `inter_pred.hpp` (chromaMotionComp), `decoder.hpp` (decodePSkipMb)
**Fix:** Add explicit `dx==0 && dy==0` chroma fast path (row memcpy).
Zero-MV skip MBs become pure memcpy from reference.
**Expected:** +0.5-1 fps on still/scroll content; minor on ball

#### L2.3 Deblocking BS cache per-MB
**Files:** `decoder.hpp`, `deblock.hpp` (BS recomputation at lines 342-362, 406-424, 473-491, 536-556)
**Fix:** Cache BS in a `uint8_t[totalMbs * 8]` array during MB decode.
Deblock reads cached BS, doesn't recompute. Chroma BS reuses luma BS.
**Expected:** +0.5-1 fps (20-30% of deblock time)

#### L2.4 Horizontal-edge column-major transpose
**Files:** `deblock.hpp` (horizontal edge loop, lines 495-524)
**Problem:** Horizontal edge access pattern strides through `yRow()` per filter
tap → 8 scattered cache-line touches per 4-pixel column.
**Fix:** Transpose a 16×8 neighborhood into a local buffer, filter as vertical
(sequential), transpose back. Converts 8 cache misses → 2-3.
**Expected:** +0.5 fps (10-15% of deblock)

---

### Layer 3 — Data Flow (cache/memory)

#### L3.1 Reference prefetch into local buffer ⭐ SECOND HIGHEST IMPACT
**Files:** `inter_pred.hpp` (lumaMotionComp, chromaMotionComp)
**Problem:** Reference frame is in PSRAM (460 KB). Fractional-pel MC touches
a 21×21 region per 16×16 block → up to 441 scattered PSRAM reads.
**Fix:** Before MC, copy the `(width+5)×(height+5)` reference region into a
stack-allocated scratch buffer (441 bytes for 16×16). All filter computation
then operates on L1-resident data. Clamping applied once during copy.
**Expected:** +2 fps on ball; eliminates MC's PSRAM pressure
**Risk:** Low — simple rectangular copy

#### L3.2 Frame buffer cache-line alignment
**Files:** `frame.hpp` (allocate method, lines 37-51)
**Problem:** `std::vector<uint8_t>` default alignment is 8-16 bytes, not the
32-byte ESP32-P4 cache line. MB column boundaries may straddle cache lines.
**Fix:** Custom aligned allocator or `std::unique_ptr<uint8_t[]>` with
`std::aligned_alloc(32, ...)`. Align strides to 32 bytes.
**Expected:** +5-8% overall (compound benefit)

#### L3.3 Direct-to-frame MC for zero-CBP MBs
**Files:** `decoder.hpp` (CABAC inter MB path, around line 3196)
**Problem:** Skip MBs and zero-residual MBs still MC into a stack `predLuma[256]`
then memcpy out. When CBP == 0, MC output IS final output.
**Fix:** Check CBP first. If zero, MC directly into `target.yMb(mbX, mbY)`.
Eliminates 384 bytes of per-MB memcpy.
**Expected:** +0.5 fps

#### L3.4 Per-MB context struct packing
**Files:** `decoder.hpp` (context members)
**Fix:** Pack `nnzLuma[16]`, `motion[16]`, `qp`, `nnzCb[4]`, `nnzCr[4]`, flags
into a single `MbContext` struct. Neighbor lookup during deblocking pulls all
needed data in one cache burst.
**Expected:** +2-3%

---

### Layer 4 — Implementation (function-level)

#### L4.1 Branchless `clipU8`
**Files:** `transform.hpp` (line 45)
**Fix:** Replace nested ternary with bit-manipulation form. On in-order RISC-V,
saves 2-6 cycles per call × thousands of calls per frame.
**Expected:** +2-3% overall

#### L4.2 Deblocking inline row pointers
**Files:** `deblock.hpp` (lines 369, 500-507)
**Fix:** Pre-compute 16 row pointers once per edge:
`const uint8_t* rowPtrs[16]; for (r=0;r<16;++r) rowPtrs[r] = frame.yRow(pixY+r)+edgeX;`
Eliminates 16 multiplications per edge. Same for horizontal edges.
**Expected:** +5-10% of deblock time

#### L4.3 CAVLC `coeff_token` LUT
**Files:** `cavlc.hpp` (matchCoeffTokenTable, lines 72-110)
**Fix:** Replace linear scan with binary search by prefix match (≤62 entries
per nC, ≤6 comparisons) or two-level LUT.
**Expected:** +5-8% of entropy time

#### L4.4 Fully unrolled 4×4 IDCT
**Files:** `transform.hpp` (inverseDct4x4AddPred)
**Fix:** Replace 4-iteration loops with fully unrolled butterfly.
`[[gnu::always_inline]]` + `[[gnu::hot]]`. Compiler may do this already;
explicit is safer on in-order cores.
**Expected:** +3-5% of transform time

#### L4.5 `getSample` elimination via L3.1 prefetch
**Files:** `inter_pred.hpp`
**Fix:** Once L3.1 is in place, the filter inner loop can use direct pointer
indexing (no clamp branches). 4 branches per sample eliminated.
**Expected:** +1.5 fps (compounds with L3.1)

---

### Layer 5 — Micro (instruction-level)

#### L5.1 Compiler hints + alignment
**Fix:** Add `__restrict__` to MC/deblock pointer params; `[[gnu::hot]]` on
hot functions; `alignas(32)` on stack scratch buffers; `__builtin_expect` on
CABAC MPS path (MPS decoded ~90% of time).
**Expected:** +1-2%

#### L5.2 BitReader cache refill
**Files:** `bitstream.hpp` (refillCache, line 252)
**Fix:** Use `memcpy` of 8 bytes + `__builtin_bswap64` instead of byte-by-byte
loop. Compiler lowers `memcpy(8)` to inline word loads on RISC-V.
**Expected:** +1-2% of entropy

#### L5.3 PIE/SIMD opportunity map (DOCUMENT ONLY — do NOT implement)
Create `docs/optimization/pie_opportunities.md`:

| Function | Candidate op | Est. speedup |
|----------|--------------|--------------|
| `lumaMotionComp` 6-tap | PIE 16×uint8 MAC | **4-8×** per row (highest priority) |
| `lumaMotionComp` vertical | Transposed MAC | 4-8× per col |
| `chromaMotionComp` | 8×uint16 weighted avg | 2-4× |
| `inverseDct4x4AddPred` | 8×int16 butterfly | 2-3× |
| `inverseDct8x8AddPred` | 8×int16 8-pt DCT | 4-6× |
| `filterLumaWeak/Strong` | 16×uint8 absdiff+filter | 2-4× |
| `clipU8` batch | Saturating pack | eliminated |
| Full-pel memcpy | 128-bit aligned store | 2-4× |

Highest PIE priority: `lumaMotionComp` (67.7% of decode time).

---

## KPI Metric System

### Primary KPIs (tracked in every bench run)

| Metric | Current | Target | Source |
|--------|---------|--------|--------|
| **Ball-Base-640 fps** (crux) | 12.0 | **25.0** | ESP32 shootout |
| Ball-High-640 fps | 12.0 | 25.0 | ESP32 shootout |
| **P0 fps (min across all)** | 12.0 | **25.0** | `min(bench results)` |
| P50 fps (median) | ~25 | ≥25 | median |
| Variance (fps stddev per stream) | TBD | < 0.5 | Measure across 3 passes |
| PSNR regression | 0.0 dB | **< 0.01 dB** | vs raw source YUV |
| Stack max depth | TBD | < 8 KB | `uxTaskGetStackHighWaterMark` |
| Heap total | ~2.3 MB | < 2.5 MB | `heap_caps_get_info` |

### Per-layer diagnostic metrics

Extend `SectionProfile` (`decode_timing.hpp`) with:
- **MC position histogram** — count of (dx,dy) combinations per frame
- **Skip ratio** — skipped MBs / total MBs
- **Zero-CBP ratio** — coded MBs with CBP=0 / coded MBs
- **BS filtered count** — edges where filter actually ran (not alpha-rejected)
- **PSRAM miss counter** — ESP-IDF perf counter if available

### Measurement protocol per optimization phase

1. Run ESP32 shootout before change: record fps per stream (all 10 vectors)
2. Apply optimization
3. Run ESP32 shootout after: record fps
4. Desktop regression: run `ctest --preset default` — all tests pass
5. PSNR regression: compute mean PSNR vs raw source for each fixture
6. Commit with `PERF:` prefix, include before/after table in commit message
7. Update `docs/performance_log.md` with new row

---

## Implementation Order (ranked by expected ball fps impact)

| Phase | Optimization | Layer | Ball fps Δ | Cumulative |
|-------|--------------|-------|-----------|------------|
| 0 | Baseline (current) | — | — | 12.0 |
| 1 | Document decoder architecture | — | 0 | 12.0 |
| 2 | Extend profile + KPI infrastructure | — | 0 | 12.0 |
| 3 | L3.1 Reference prefetch to stack buffer | L3 | **+2.0** | 14.0 |
| 4 | L2.1 Diagonal MC fast path (2-pass FIR) | L2 | **+3.5** | 17.5 |
| 5 | L4.5 `getSample` elimination (compounds) | L4 | +1.5 | 19.0 |
| 6 | L1.1 Row-based deblock pipeline | L1 | +2.0 | 21.0 |
| 7 | L4.1 Branchless clipU8 + L4.2 inline row ptrs | L4 | +1.0 | 22.0 |
| 8 | L3.2 Cache-line alignment | L3 | +1.0 | 23.0 |
| 9 | L3.3 Direct-to-frame MC + L2.2 zero-MV chroma | L3+L2 | +0.5 | 23.5 |
| 10 | L2.3 BS cache + L2.4 horizontal transpose | L2 | +1.0 | 24.5 |
| 11 | L5.1 Compiler hints | L5 | +0.5 | 25.0 |
| 12 | L4.3 CAVLC LUT + L4.4 IDCT unroll | L4 | +0.5 | 25.5 |
| 13 | L1.3 Static arrays + L5.2 BitReader refill | L1+L5 | +0.5 | 26.0 |
| 14 | L5.3 PIE opportunity doc (no impl) | L5 | 0 | 26.0 |

Estimates are compounding (each builds on prior). Re-measure after each phase;
re-prioritize if actual Δ diverges from estimate.

---

## Risk Assessment

| Optimization | Risk | Mitigation |
|--------------|------|------------|
| L2.1 Diagonal MC fast path | Medium — unclipped intermediate arithmetic must match spec §8.4.2.2.1 | Bit-exact test vs ffmpeg for all 16 fractional positions; PSNR check |
| L3.1 Reference prefetch | Low — off-by-one in margin | Boundary test with MVs at frame edges |
| L1.1 Row-based deblock | Low — edge case at last row | Test single-row frame; PSNR regression |
| L4.1 Branchless clipU8 | Low — signed overflow | Static analysis; test INT32 extremes |
| L3.2 Aligned allocator | Low — memory leak if wrong dtor | Use RAII wrapper (`std::unique_ptr` + aligned deleter) |
| All others | Very low | Existing VLC/IDCT/deblock unit tests catch errors |

**Invariant:** All optimizations must preserve bit-exact decode output. PSNR
regression test vs raw source is the quality gate. Any pixel-level diff must
be investigated — H.264 decode should produce identical results across
implementations (within spec's allowed rounding tolerance).

---

## Critical Files to Modify

| File | Phases | Notes |
|------|--------|-------|
| `components/sub0h264/src/inter_pred.hpp` | 3, 4, 5, 9 | MC fast paths, prefetch, clamping elimination |
| `components/sub0h264/src/decoder.hpp` | 6, 9, 10, 13 | Row-based deblock, direct MC, BS cache, static arrays |
| `components/sub0h264/src/deblock.hpp` | 7, 10 | Inline row ptrs, horizontal transpose |
| `components/sub0h264/src/transform.hpp` | 7, 12 | Branchless clipU8, IDCT unroll |
| `components/sub0h264/src/frame.hpp` | 8 | 32-byte aligned allocation |
| `components/sub0h264/src/cavlc.hpp` | 12 | coeff_token LUT |
| `components/sub0h264/src/bitstream.hpp` | 13 | BitReader refill |
| `components/sub0h264/src/decode_timing.hpp` | 2 | Extend SectionProfile with histograms |
| `docs/decoder_architecture.md` | 1 | Document 5-layer hierarchy (new file or extend existing) |
| `docs/optimization/pie_opportunities.md` | 14 | PIE/SIMD catalog (new file) |
| `docs/performance_log.md` | every phase | Per-phase before/after results |

---

## Verification

After each phase:
1. `ctest --preset default` — all existing tests pass
2. ESP32 shootout: all 10 streams decode without crash, P0 fps ≥ previous P0
3. PSNR regression check: mean PSNR vs raw source unchanged (< 0.01 dB drop)
4. Update `docs/performance_log.md` with measured fps per stream
5. Commit with measured ball fps before/after in message

End-to-end verification (after phase 14):
1. ESP32 shootout Ball-Base-640 ≥ 25 fps **AND** Ball-High-640 ≥ 25 fps
2. All 10 ESP32 streams ≥ 25 fps (P0 = 25 fps achieved)
3. Variance across 3 passes < 0.5 fps per stream
4. Desktop shootout: no regression (sub0h264 fps not lower than baseline)
5. PIE opportunity doc complete with cycle estimates per function
6. Updated architecture doc describes 5-layer model
