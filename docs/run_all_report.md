# Sub0h264 — Full Suite Report
_2026-04-27 09:38:44Z_

## KPI Gates
_0 hard fail · 0 soft fail · 7 skipped (stage not run)_

| Gate | Description | Threshold | Current | Result |
|---|---|---|---|---|
| G-PSNR | Tapo C110 bit-exact | gte 99.0 | 99.00 | ✓ |
| G-PSNR-W | wstress baseline bit-exact | gte 99.0 | 99.00 | ✓ |
| G-PERF-CRUX | ESP32 Ball-High ≥ 12.0 fps (no regression vs baseline) | gte 12.0 | — | — |
| G-PERF-NORM-TAPO | ESP32 Tapo ≥ 30.0 fps (no regression) | gte 30.0 | — | — |
| G-PERF-NORM-SCROLL-H | ESP32 Scroll-High ≥ 24.0 fps | gte 24.0 | — | — |
| G-PERF-NORM-STILL-H | ESP32 Still-High ≥ 32.0 fps | gte 32.0 | — | — |
| G-LIBAVC-LEAD | ESP32 sub0/libavc ratio ≥ 1.0× on Ball-High | gte 1.0 | — | — |
| G-PERF-TARGET-BALL | ESP32 Ball-High ≥ 25.0 fps (Phase 2 target) | gte 25.0 | — | — |
| G-PERF-TARGET-BALL-BASE | ESP32 Ball-Base ≥ 25.0 fps (Phase 2 target) | gte 25.0 | — | — |

## Performance Summary
_Δ vs previous run · trend over last 9 points_

| KPI | Current | Previous | Δ | Trend (last 7) |
|---|---|---|---|---|
| Desktop Tapo bench fps | 3019.10 | 3304.60 | ▼-285.50 ✗ | ▇▆▆█▁▅▃ |
| Desktop Ball-High fps | 41.70 | 51.70 | ▼-10.00 ✗ | █▇▆▅▁▇▃ |
| Tapo min PSNR | 99.00 | 99.00 | · |  |
| wstress baseline min PSNR | 99.00 | 99.00 | · |  |

## ✓ Unit tests (ctest --preset default)
_100% tests passed, 0 tests failed_

| Test | Result | Time (s) | Δ Time (s) | Trend (Time (s)) |
|---|---|---|---|---|
| Sub0h264_Tests | Passed | 14.59 | ▲+4.08 ✗ | ▁▁▁▁▂▂█▁▆ |
| Sub0h264_Bench_Tests | Passed | 8.49 | ▲+2.17 ✗ | ▁▁▁▁▂▂█▁▆ |

## ✓ Bench suite (ctest --preset bench)
_11 bench results_

| Bench | FPS | Δ FPS | Trend (FPS) | Median (ms) | Δ Median (ms) | Trend (Median (ms)) | Frames |
|---|---|---|---|---|---|---|---|
| Baseline CAVLC (short) | 508.5 | ▼-91.00 ✗ | █▇▅▃▁▆▃ | 98.3 | ▲+14.90 ✗ | ▁▁▂▄█▁▅ | 50 |
| Flat black | 2820.0 | ▼-50.10 ✗ | █▇▇▇▁▆▅ | 88.7 | ▲+1.60 ✗ | ▁▁▁▁█▂▂ | 250 |
| CAVLC 320x240 I+P | 710.8 | ▼-72.90 ✗ | █▇▄▅▁▇▅ | 42.2 | ▲+3.90 ✗ | ▁▁▄▂█▁▃ | 30 |
| CABAC 320x240 I+P | 2938.9 | ▼-33.80 ✗ | ▆▆█▃▁▅▅ | 10.2 | ▲+0.10 ✗ | ▁▁▁▄█▂▂ | 30 |
| Scroll Baseline 640x480 | 396.8 | ▲+73.90 ✓ | █▆▃▅▁▃▇ | 75.6 | ▼-17.30 ✓ | ▁▁▄▂█▄▁ | 30 |
| Scroll High 640x480 | 380.2 | ▲+14.60 ✓ | ▇█▅▅▁▆▇ | 78.9 | ▼-3.20 ✓ | ▁▁▂▂█▁▁ | 30 |
| Ball Baseline 640x480 | 42.7 | ▼-10.20 ✗ | █▇▆▆▁▆▃ | 701.9 | ▲+134.70 ✗ | ▁▁▂▂█▁▄ | 30 |
| Ball High 640x480 | 41.7 | ▼-10.00 ✗ | █▇▆▅▁▇▃ | 719.1 | ▲+139.30 ✗ | ▁▁▁▂█▁▄ | 30 |
| Still Baseline 640x480 | 922.6 | ▼-133.30 ✗ | ▇█▆▅▁▆▄ | 32.5 | ▲+4.10 ✗ | ▁▁▂▂█▁▃ | 30 |
| Still High 640x480 | 858.2 | ▼-247.60 ✗ | ▇█▆▅▁▇▄ | 35.0 | ▲+7.90 ✗ | ▁▁▂▂█▁▃ | 30 |
| Tapo C110 stream2 | 3019.1 | ▼-285.50 ✗ | ▇▆▆█▁▅▃ | 39.4 | ▲+3.40 ✗ | ▁▁▁▁█▂▄ | 119 |

## ✗ Desktop shootout
_binary not found: D:\Craig\GitHub\Sub0h264\build-shootout\sub0h264_shootout.exe_

_(no rows)_

## ✓ PSNR validation vs ffmpeg
_2 fixtures_

| Fixture | Frames | Avg PSNR (dB) | Δ Avg PSNR (dB) | Trend (Avg PSNR (dB)) | Min PSNR (dB) | Δ Min PSNR (dB) | Trend (Min PSNR (dB)) | Status |
|---|---|---|---|---|---|---|---|---|
| Tapo C110 | 119 | 99.00 | · | ▁▁▁██████ | 99.00 | · | ▁▁▁██████ | ✓ bit-exact |
| wstress baseline | 5 | 99.00 | · | ▁▁▁▁▁▁▁▁▁ | 99.00 | · | ▁▁▁▁▁▁▁▁▁ | ✓ bit-exact |
