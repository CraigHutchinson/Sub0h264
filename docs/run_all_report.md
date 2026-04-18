# Sub0h264 — Full Suite Report
_2026-04-18 08:53:05Z_

## ✓ Unit tests (ctest --preset default)
_100% tests passed, 0 tests failed_

| Test | Result | Time (s) |
|---|---|---|
| Sub0h264_Tests | Passed | 10.17 |
| Sub0h264_Bench_Tests | Passed | 6.33 |

## ✓ Desktop shootout (sub0h264 vs libavc)
_9 streams_

| Stream | Frames | sub0h264 | libavc | Ratio |
|---|---|---|---|---|
| Scroll-Base-640 | 30 | 3203.4 | 5450.6 | 0.59x |
| Scroll-High-640 | 30 | 3127.6 | 4886.8 | 0.64x |
| Ball-Base-640 | 30 | 1000.9 | 1446.5 | 0.69x |
| Ball-High-640 | 30 | 996.7 | 1415.7 | 0.70x |
| Still-Base-640 | 30 | 5166.2 | 6345.2 | 0.81x |
| Still-High-640 | 30 | 4949.7 | 5779.2 | 0.86x |
| Flat-Base-640 | 250 | 2786.4 | 6850.3 | 0.41x |
| CAVLC-Base-640 | 50 | 2127.8 | 4185.5 | 0.51x |
| Tapo-C110 | 119 | 3711.0 | 7168.7 | 0.52x |

## ✓ PSNR validation vs ffmpeg
_7 fixtures_

| Fixture | Frames | Avg PSNR (dB) | Min PSNR (dB) | Status |
|---|---|---|---|---|
| Tapo C110 | 119 | 99.00 | 99.00 | ✓ bit-exact |
| wstress baseline | 5 | 99.00 | 99.00 | ✓ bit-exact |
| wstress complex_flat | 5 | 99.00 | 99.00 | ✓ bit-exact |
| wstress gradient | 5 | 99.00 | 99.00 | ✓ bit-exact |
| wstress i16x16 | 5 | 99.00 | 99.00 | ✓ bit-exact |
| wstress i4x4 | 5 | 99.00 | 99.00 | ✓ bit-exact |
| wstress i8x8 | 5 | 99.00 | 99.00 | ✓ bit-exact |
