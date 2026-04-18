# Sub0h264 — Full Suite Report
_2026-04-18 08:49:39Z_

## ✓ Unit tests (ctest --preset default)
_100% tests passed, 0 tests failed_

| Test | Result | Time (s) |
|---|---|---|
| Sub0h264_Tests | Passed | 10.11 |
| Sub0h264_Bench_Tests | Passed | 6.15 |

## ✓ Bench suite (ctest --preset bench)
_11 bench results_

| Bench | FPS | Median (ms) | Frames |
|---|---|---|---|
| Baseline CAVLC (short) | 620.6 | 80.6 | 50 |
| Flat black | 3046.2 | 82.1 | 250 |
| CAVLC 320x240 I+P | 802.3 | 37.4 | 30 |
| CABAC 320x240 I+P | 3098.2 | 9.7 | 30 |
| Scroll Baseline 640x480 | 379.6 | 79.0 | 30 |
| Scroll High 640x480 | 401.7 | 74.7 | 30 |
| Ball Baseline 640x480 | 53.1 | 564.7 | 30 |
| Ball High 640x480 | 53.2 | 564.0 | 30 |
| Still Baseline 640x480 | 1154.2 | 26.0 | 30 |
| Still High 640x480 | 1123.6 | 26.7 | 30 |
| Tapo C110 stream2 | 3432.9 | 34.7 | 119 |

## ✓ Desktop shootout (sub0h264 vs libavc)
_9 streams_

| Stream | Frames | sub0h264 | libavc | Ratio |
|---|---|---|---|---|
| Scroll-Base-640 | 30 | 2784.0 | 4877.3 | 0.57x |
| Scroll-High-640 | 30 | 2735.0 | 4405.3 | 0.62x |
| Ball-Base-640 | 30 | 876.9 | 1380.4 | 0.64x |
| Ball-High-640 | 30 | 1020.2 | 1270.6 | 0.80x |
| Still-Base-640 | 30 | 4821.6 | 6398.0 | 0.75x |
| Still-High-640 | 30 | 5058.2 | 5721.9 | 0.88x |
| Flat-Base-640 | 250 | 2563.7 | 6373.6 | 0.40x |
| CAVLC-Base-640 | 50 | 1879.8 | 3653.9 | 0.51x |
| Tapo-C110 | 119 | 3238.1 | 6345.0 | 0.51x |

## ✓ PSNR validation vs ffmpeg
_7 fixtures_

| Fixture | Frames | Avg PSNR (dB) | Min PSNR (dB) | Status |
|---|---|---|---|---|
| Tapo C110 | 119 | 56.33 | 41.03 | ✓ |
| wstress baseline | 5 | 99.00 | 99.00 | ✓ bit-exact |
| wstress complex_flat | 5 | 99.00 | 99.00 | ✓ bit-exact |
| wstress gradient | 5 | 99.00 | 99.00 | ✓ bit-exact |
| wstress i16x16 | 5 | 99.00 | 99.00 | ✓ bit-exact |
| wstress i4x4 | 5 | 99.00 | 99.00 | ✓ bit-exact |
| wstress i8x8 | 5 | 99.00 | 99.00 | ✓ bit-exact |

