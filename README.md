# Sub0h264

> **Patent Notice:** H.264/AVC may be covered by patents in some jurisdictions.
> The patent portfolio is administered by [Via Licensing Alliance](https://via-la.com/licensing-programs/avc-h-264/)
> (formerly MPEG LA). Most patents have expired; the last known US patents expire
> ~2027-2028. This project provides software under the MIT license which grants
> **copyright only, not patent rights**. Users are responsible for determining
> whether a patent license is required for their use case. See
> [Via LA](https://via-la.com/licensing-programs/avc-h-264/) or the
> [Wikimedia patent tracker](https://meta.wikimedia.org/wiki/Have_the_patents_for_H.264_MPEG-4_AVC_expired_yet%3F).

H.264 decoder targeting ESP32-P4 RISC-V and x86 desktop.
Header-only C++23 library with zero external dependencies.

**Max resolution:** 640x480 | **Target:** Constrained Baseline + High profile I/P-frame decode

## Specification Compliance

| Feature | Status | Notes |
|---------|--------|-------|
| **Annex B byte stream** | Complete | Start code detection, emulation prevention |
| **NAL unit parsing** | Complete | All NAL types recognised |
| **SPS/PPS parsing** | Complete | Baseline, Main, High profiles. VUI parsed but ignored |
| **I-slice (CAVLC)** | Complete | 52+ dB PSNR vs raw source. Pixel-perfect vs ffmpeg |
| **P-slice (CAVLC)** | Complete | 52+ dB PSNR. Skip, 16x16, 16x8, 8x16, 8x8 partitions |
| **I-slice (CABAC)** | Complete | **99 dB bit-exact** on Tapo C110 (640x368) and all wstress fixtures vs JM and ffmpeg |
| **P-slice (CABAC)** | Complete | **99 dB bit-exact** on Tapo C110 (119 frames) incl. rolling intra refresh; all P_L0 partitions + P_8x8 sub_mb_type {8x8, 8x4, 4x8, 4x4} |
| **B-slice** | Not implemented | Slice header parsed, decode rejected |
| **Intra 4x4 prediction** | Complete | All 9 directional modes per §8.3.1.2 |
| **Intra 8x8 prediction** | Complete | All 9 modes per §8.3.2 (V, H, DC, DDL, DDR, VR, HD, VL, HU) |
| **Intra 16x16 prediction** | Complete | All 4 modes (V, H, DC, Plane) |
| **Chroma prediction** | Complete | All 4 modes (DC, H, V, Plane) |
| **Luma MC (6-tap)** | Complete | All 16 fractional positions per §8.4.2.2.1 |
| **Chroma MC (bilinear)** | Complete | Eighth-pel precision per §8.4.2.2.2 |
| **4x4 IDCT + dequant** | Complete | Butterfly per §8.5.12, dequant per §8.5.12.1 |
| **8x8 IDCT + dequant** | Complete | High profile per §8.5.12, normAdjust8x8 per Table 8-16 |
| **Deblocking filter** | Complete | BS 0-4, alpha/beta/tc0 tables per §8.7 |
| **CAVLC entropy** | Complete | coeff_token, level, total_zeros, run_before |
| **CABAC entropy** | Complete | Arithmetic engine, context init, all binarisations |
| **Weighted prediction** | Complete | Parsed + applied for P-slices, all partition types §8.4.2.3 |
| **DPB management** | Complete | Short-term + long-term refs, all 6 MMCO ops §8.2.5.4, L0 reordering §8.2.4.3 |
| **Scaling lists** | Applied | §7.4.2.1.1.1 Table 7-2 fall-back rules; scaled 4x4/8x8 dequant + DC Hadamard paths. JVT-default fixture IDR bit-exact at 84 dB vs JM, P-frame drift tracked |
| **MBAFF** | Not implemented | Progressive frame_mbs_only=1 only |
| **FMO** | Not implemented | Single slice group only |
| **Multiple references** | Complete | Per-partition ref_idx, full L0 list with reordering §8.2.4.3 |
| **Error concealment** | Not implemented | Frame skipped on decode error |

### Technical Readiness

| Profile | I-frames | P-frames | B-frames | Quality |
|---------|----------|----------|----------|---------|
| Constrained Baseline | Production | Production | N/A | **99 dB bit-exact** |
| Main (CABAC) | Production | Production | Not started | **99 dB bit-exact** |
| High (8x8 + CABAC, incl. Tapo C110 640x368) | Production | Production | Not started | **99 dB bit-exact** |

Validated against JM 19.0 reference + ffmpeg per-frame on: Tapo C110 (119
frames, rolling intra refresh), all 6 wstress synthetics (baseline / i4x4 /
i8x8 / i16x16 / gradient / complex_flat), wstress_p8x8_sub (forcing 8x4 /
4x8 / 4x4 sub-partitions) — every fixture 99 dB bit-exact. Lockstep CABAC
bin trace vs JM matches bit-for-bit on P-frames.

## Performance

**Crucial figure — ESP32-P4 shootout** (360 MHz RISC-V, 32 MB PSRAM,
sub0h264 vs libavc reference). Captured 2026-04-19 at commit
[069f264](docs/optimization/esp32p4_hierarchical_plan.md):

| Stream | sub0h264 | libavc | Ratio | 25 fps target |
|---|---|---|---|---|
| **Ball-High-640** *(crux — random MVs, every MB has residual)* | **12.0 fps** | 10.8 fps | **1.11×** | **✗ −13.0** |
| **Ball-Base-640** | **12.1 fps** | 11.0 fps | **1.10×** | **✗ −12.9** |
| Scroll-Base-640 | 24.9 fps | 17.7 fps | 1.40× | ≈ (−0.1) |
| Scroll-High-640 | 24.7 fps | 17.3 fps | 1.43× | ≈ (−0.3) |
| Tapo-C110 640x368 | 30.2 fps | 22.4 fps | 1.35× | ✓ |
| Still-Base-640 | 33.1 fps | 19.3 fps | 1.71× | ✓ |
| Still-High-640 | 32.8 fps | 18.9 fps | 1.74× | ✓ |
| Flat-Base-640 | 34.0 fps | 18.7 fps | 1.82× | ✓ |
| CABAC-320 | 42.5 fps | 30.3 fps | 1.40× | ✓ |
| CAVLC-320 | 44.8 fps | 40.9 fps | 1.10× | ✓ |

**Headline:** sub0h264 beats libavc on every stream (ratio 1.10–1.82×).
7/10 streams pass the 25 fps gate. **The crux is Ball** — bouncing ball on
noise forces every MB to have residual (no skip fast path) and randomly
distributed MVs (all 16 fractional MC positions exercised). Lifting Ball
from 12 → 25 fps is the optimisation roadmap.

**Desktop (Windows MSVC Release, x86-64)** at the same commit:

| Stream | sub0h264 | libavc | Ratio |
|---|---|---|---|
| Ball-Base-640 | 827.6 fps | 1253.9 fps | 0.66× |
| Ball-High-640 | 876.7 fps | 1261.6 fps | 0.69× |
| Tapo-C110 | 3183.8 fps | 7434.2 fps | 0.43× |
| Flat-Base-640 | 2395.1 fps | 7116.2 fps | 0.34× |

**Optimisation roadmap:** [docs/optimization/esp32p4_hierarchical_plan.md](docs/optimization/esp32p4_hierarchical_plan.md)
with 19 per-opportunity implementation briefs under
[docs/optimization/opportunities/](docs/optimization/opportunities/). The
top-impact items (L2.1 diagonal MC 2-pass, L3.1 reference prefetch, L4.5
getSample elimination) together target +7 fps on Ball. History + delta +
trend in `docs/run_all_report.md` (regenerated by
`python scripts/run_all_suites.py`).

## Build

### Desktop
```bash
cmake --preset default
cmake --build --preset default
ctest --preset default          # 253 tests
```

### ESP32-P4
```bash
cd test_apps/unit_tests
cmake --preset esp32p4
cmake --build --preset esp32p4
ESPPORT=COM9 ctest --preset esp32p4
```

## Spec Reference Source

- Canonical ITU source (always resolve latest in-force first):
	[H.264 Recommendation Index](https://www.itu.int/rec/T-REC-H.264)
- Revision-specific source page example (superseded revisions may still exist):
	[H.264 (08/21)](https://www.itu.int/rec/T-REC-H.264-202108-S)
- Rebuild local mirror and normalized index under `docs/reference/itu/h264/{revision}/`:

```bash
python scripts/sync_h264_spec.py
```

- The sync script writes a per-revision manifest at:
	`docs/reference/itu/h264/{revision}/normalized/manifest.json`
	which records the resolved current in-force revision link.

## Project Structure

| Directory | Purpose |
|-----------|---------|
| `components/sub0h264/` | Decoder library (ESP-IDF component or standalone static lib) |
| `components/sub0h264/src/` | Header-only implementation |
| `test_apps/hello_h264/` | ESP-IDF hello-world demo |
| `test_apps/unit_tests/` | ESP-IDF unit test runner |
| `tests/` | Shared test sources (desktop + ESP32-P4) |
| `tests/fixtures/` | H.264 test bitstreams + raw YUV ground truth |
| `scripts/` | Analysis, comparison, and fixture generation tools |
| `docs/` | Performance log, style guide, spec references |

## Implementation Plan

### Phase 1: Quality — COMPLETE
- CABAC coefficient decode: **99 dB bit-exact** (11 bugs fixed, verified via JM lockstep)
- I_4x4 / I_8x8 / I_16x16 intra prediction: all modes spec-compliant
- P_L0_16x16 / 16x8 / 8x16 / P_8x8 with sub_mb_type {8x8, 8x4, 4x8, 4x4}
- Weighted prediction (P-slices, all partition types)
- DPB + MMCO + L0 reordering
- Scaling lists (§7.4.2.1.1 resolution + scaled dequant applied)
- Tapo C110 + all wstress fixtures 99 dB bit-exact vs JM

### Phase 2: Performance — current focus
Target: **Ball-Base/High 640x480 ≥ 25 fps on ESP32-P4** (currently 12 fps).

See [docs/optimization/esp32p4_hierarchical_plan.md](docs/optimization/esp32p4_hierarchical_plan.md)
for the full 5-layer roadmap; briefs in
[docs/optimization/opportunities/](docs/optimization/opportunities/):

1. **L3.1 reference prefetch** — scratch buffer eliminates 441 PSRAM
   reads/MB (+2 fps)
2. **L2.1 diagonal MC 2-pass separable filter** — HIGHEST IMPACT, rebuilds
   the j/e/f/i/p/q quarter-pel paths (+3.5 fps)
3. **L4.5 getSample elimination** — compounds with L3.1 (+1.5 fps)
4. **L1.1 row-based decode+deblock streaming** — keeps deblock in L1 (+2 fps)
5. Micro-opts: branchless clipU8, inline row ptrs, compiler hints (+1.5 fps)
6. PIE/SIMD MC 6-tap kernel (catalogued in L5.3, implementation deferred)

### Phase 3: B-frame Support — future
- B-slice decode loop (§7.3.4)
- Direct spatial/temporal MV derivation (§8.4.1.1)
- Bi-directional MC (§8.4.2)
- B-slice weighted prediction

### Phase 4: Robustness — future
- Error concealment (copy previous frame on decode error)
- Level-conformance validation
- Resilience gaps enumerated in [docs/known_issues.md](docs/known_issues.md)

## License

MIT — see [LICENSE.md](LICENSE.md)
