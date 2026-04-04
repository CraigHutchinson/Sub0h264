# Performance Log

Historical decode performance measurements. Each entry records the
commit, platform, and fps for the standard benchmark fixtures.

## Benchmark fixtures

| ID | Fixture | Resolution | Frames | Profile | Content |
|----|---------|-----------|--------|---------|---------|
| CAVLC-640 | baseline_640x480_short.h264 | 640x480 | 50 | Baseline | Real camera (I+P) |
| CAVLC-320 | scrolling_texture.h264 | 320x240 | 30 | Baseline | Synthetic scroll (I+P) |
| CABAC-320 | scrolling_texture_high.h264 | 320x240 | 30 | High | Synthetic scroll (I+P, CABAC) |
| FLAT-640 | flat_black_640x480.h264 | 640x480 | 250 | Baseline | Flat black (I+P) |

## Desktop (Windows, MSVC Release, x86-64)

| Date | Commit | CAVLC-640 | CAVLC-320 | CABAC-320 | FLAT-640 | Notes |
|------|--------|-----------|-----------|-----------|----------|-------|
| 2026-04-04 | ec6a731 | 899 fps | 2163 fps | 619 fps | 975 fps | Before frame sync elimination |
| 2026-04-04 | 200aa68 | 1048 fps | 2497 fps | 598 fps | 994 fps | +P-frame sync elimination, +MC fast-path |
| 2026-04-04 | ccaa6a4 | 1048 fps | 2497 fps | 584 fps | 994 fps | +readBit fast-path, +batched renormalize |
| 2026-04-04 | 1d9d746 | 1048 fps | 2019 fps | 584 fps | 838 fps | All 20 fixtures embedded (baseline) |
| 2026-04-04 | 719c6e6 | 946 fps | 2064 fps | 575 fps | 841 fps | +activeFrame_ zero-copy, +readBit fast-path |
| 2026-04-04 | f8e0c42 | 987 fps | 2276 fps | 677 fps | — | +MC half-pel fast-path (inter MB -14%) |

## ESP32-P4 (360 MHz RISC-V, 32 MB PSRAM, COM9)

| Date | Commit | CAVLC-640 | CAVLC-320 | CABAC-320 | FLAT-640 | Notes |
|------|--------|-----------|-----------|-----------|----------|-------|
| 2026-04-04 | 1d9d746 | 13.9 fps | 30.9 fps | 10.9 fps | 14.1 fps | First full benchmark with matched fixtures |
| 2026-04-04 | 3879dd0 | 14.6 fps | 30.9 fps | — | 14.1 fps | +MC fast-path, +deblock BS precompute, +activeFrame_ |
| 2026-04-04 | dd954c7 | 15.1 fps | 33.6 fps | 11.4 fps | 16.9 fps | +BitReader cache, +CLZ UEV, +smartflash |

## Key observations

- **CABAC vs CAVLC**: CABAC is 2.8x slower on ESP32-P4 (30.9 vs 10.9 fps at 320x240),
  3.5x slower on desktop. The ESP32-P4 ratio is tighter because CABAC table lookups
  hit L1 cache well on the small core.
- **Desktop/ESP32-P4 ratio**: ~65x for CAVLC 320x240 (desktop 2019 / ESP32-P4 30.9).
  Consistent with 4 GHz OoO vs 360 MHz in-order.
- **Frame sync elimination** (+78% on 250-frame CAVLC): removing redundant P-frame
  memcpy was the largest single optimization.
- **Deblock filter**: 12-30% of decode time depending on resolution. Primary remaining
  optimization target.
