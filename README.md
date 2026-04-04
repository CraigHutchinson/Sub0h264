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
| **I-slice (CABAC)** | Partial | Engine verified correct. Quality ~10 dB (target 50 dB) |
| **P-slice (CABAC)** | Partial | Arithmetic engine correct, context adaptation incomplete |
| **B-slice** | Not implemented | Slice header parsed, decode rejected |
| **Intra 4x4 prediction** | Complete | All 9 directional modes per §8.3.1.2 |
| **Intra 8x8 prediction** | Partial | V, H, DC modes. Diagonal modes fallback to DC |
| **Intra 16x16 prediction** | Complete | All 4 modes (V, H, DC, Plane) |
| **Chroma prediction** | Complete | All 4 modes (DC, H, V, Plane) |
| **Luma MC (6-tap)** | Complete | All 16 fractional positions per §8.4.2.2.1 |
| **Chroma MC (bilinear)** | Complete | Eighth-pel precision per §8.4.2.2.2 |
| **4x4 IDCT + dequant** | Complete | Butterfly per §8.5.12, dequant per §8.5.12.1 |
| **8x8 IDCT + dequant** | Complete | High profile per §8.5.12, normAdjust8x8 per Table 8-16 |
| **Deblocking filter** | Complete | BS 0-4, alpha/beta/tc0 tables per §8.7 |
| **CAVLC entropy** | Complete | coeff_token, level, total_zeros, run_before |
| **CABAC entropy** | Complete | Arithmetic engine, context init, all binarisations |
| **Weighted prediction** | Stub | Table parsed, weights not applied to MC |
| **DPB management** | Basic | Short-term refs only. No MMCO, no long-term refs |
| **Scaling lists** | Parsed | Stored but not applied to 8x8 dequant |
| **MBAFF** | Not implemented | Progressive frame_mbs_only=1 only |
| **FMO** | Not implemented | Single slice group only |
| **Multiple references** | Partial | ref_idx stored per partition, DPB lookup by frameNum |
| **Error concealment** | Not implemented | Frame skipped on decode error |

### Technical Readiness

| Profile | I-frames | P-frames | B-frames | Quality |
|---------|----------|----------|----------|---------|
| Constrained Baseline | Production | Production | N/A | 52+ dB |
| Main (CABAC) | In progress | In progress | Not started | ~10 dB |
| High (8x8 + CABAC) | In progress | In progress | Not started | ~10 dB |

## Performance

See [docs/performance_log.md](docs/performance_log.md) for historical tracking.

| Stream | Desktop (x86) | ESP32-P4 (360 MHz) |
|--------|---------------|---------------------|
| CAVLC 640x480 I+P (50 frames) | 1048 fps | 13.9 fps |
| CAVLC 320x240 I+P (30 frames) | 2019 fps | 30.9 fps |
| CABAC 320x240 I+P (30 frames) | 584 fps | 10.9 fps |

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

### Phase 1: CABAC Quality (current)
- Fix CABAC coefficient decode quality gap (10 dB -> 50 dB)
- Complete I_8x8 intra prediction (remaining diagonal modes)
- Apply scaling lists to 8x8 dequant
- Add High profile (m,n) init for contexts 460-1023

### Phase 2: B-frame Support
- B-slice decode loop (§7.3.4)
- Direct spatial/temporal MV derivation (§8.4.1.1)
- Bi-directional MC (§8.4.2)
- MMCO processing (§7.3.3.3)
- Long-term reference pictures

### Phase 3: Weighted Prediction
- Apply luma/chroma weights to MC output (§8.4.2.3)
- Implicit/explicit weight derivation for B-slices

### Phase 4: Robustness
- Reference picture list reordering (apply, not just parse)
- Error concealment (copy previous frame on decode error)
- DPB sliding window and MMCO

### Phase 5: Performance
- Deblock filter BS precomputation (12-30% of decode time)
- ESP32-P4 PIE vectorisation (IDCT butterfly, MC 6-tap filter)
- Zero-copy I-slice decode (directly into DPB frame)
- PSRAM access pattern optimisation

## License

MIT — see [LICENSE.md](LICENSE.md)
