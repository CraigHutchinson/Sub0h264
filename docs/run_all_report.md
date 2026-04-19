# Sub0h264 — Full Suite Report
_2026-04-19 08:05:48Z_

## Performance Summary
_Δ vs previous run · trend over last 9 points_

| KPI | Current | Previous | Δ | Trend (last 6) |
|---|---|---|---|---|
| Tapo bench fps | 3304.60 | 2656.80 | ▲+647.80 ✓ | ▇▆▆█▁▅ |
| Ball-High fps (P0 crux) | 51.70 | 32.90 | ▲+18.80 ✓ | █▇▆▅▁▇ |
| Desktop sub0/libavc Tapo ratio | 0.43 | 0.47 | ▼-0.04 ✗ |  |
| Desktop sub0/libavc Ball-High ratio | 0.69 | 0.83 | ▼-0.14 ✗ |  |
| Tapo min PSNR | 99.00 | 99.00 | · |  |
| wstress baseline min PSNR | 99.00 | 99.00 | · |  |

## ✓ Unit tests (ctest --preset default)
_100% tests passed, 0 tests failed_

| Test | Result | Time (s) | Δ Time (s) | Trend (Time (s)) |
|---|---|---|---|---|
| Sub0h264_Tests | Passed | 10.51 | ▼-5.91 ✓ | ▁▁▁▁▂▂█▁ |
| Sub0h264_Bench_Tests | Passed | 6.32 | ▼-2.71 ✓ | ▁▁▁▁▂▂█▁ |

## ✓ Bench suite (ctest --preset bench)
_11 bench results_

| Bench | FPS | Δ FPS | Trend (FPS) | Median (ms) | Δ Median (ms) | Trend (Median (ms)) | Frames |
|---|---|---|---|---|---|---|---|
| Baseline CAVLC (short) | 599.5 | ▲+154.50 ✓ | █▇▅▃▁▆ | 83.4 | ▼-29.00 ✓ | ▁▁▂▄█▁ | 50 |
| Flat black | 2870.1 | ▲+803.70 ✓ | █▇▇▇▁▆ | 87.1 | ▼-33.90 ✓ | ▁▁▁▁█▂ | 250 |
| CAVLC 320x240 I+P | 783.7 | ▲+232.60 ✓ | █▇▄▅▁▇ | 38.3 | ▼-16.10 ✓ | ▁▁▄▂█▁ | 30 |
| CABAC 320x240 I+P | 2972.7 | ▲+670.30 ✓ | ▆▆█▃▁▅ | 10.1 | ▼-2.90 ✓ | ▁▁▁▄█▂ | 30 |
| Scroll Baseline 640x480 | 322.9 | ▲+52.70 ✓ | █▆▃▅▁▃ | 92.9 | ▼-18.10 ✓ | ▁▁▄▂█▄ | 30 |
| Scroll High 640x480 | 365.6 | ▲+136.60 ✓ | ▇█▅▅▁▆ | 82.1 | ▼-48.90 ✓ | ▁▁▂▂█▁ | 30 |
| Ball Baseline 640x480 | 52.9 | ▲+17.00 ✓ | █▇▆▆▁▆ | 567.2 | ▼-269.60 ✓ | ▁▁▂▂█▁ | 30 |
| Ball High 640x480 | 51.7 | ▲+18.80 ✓ | █▇▆▅▁▇ | 579.8 | ▼-332.80 ✓ | ▁▁▁▂█▁ | 30 |
| Still Baseline 640x480 | 1055.9 | ▲+364.90 ✓ | ▇█▆▅▁▆ | 28.4 | ▼-15.00 ✓ | ▁▁▂▂█▁ | 30 |
| Still High 640x480 | 1105.8 | ▲+457.20 ✓ | ▇█▆▅▁▇ | 27.1 | ▼-19.10 ✓ | ▁▁▂▂█▁ | 30 |
| Tapo C110 stream2 | 3304.6 | ▲+647.80 ✓ | ▇▆▆█▁▅ | 36.0 | ▼-8.80 ✓ | ▁▁▁▁█▂ | 119 |

## ✓ Desktop shootout (sub0h264 vs libavc)
_9 streams_

| Stream | Frames | sub0h264 | Δ sub0h264 | Trend (sub0h264) | libavc | Δ libavc | Trend (libavc) | Ratio | Δ Ratio | Trend (Ratio) |
|---|---|---|---|---|---|---|---|---|---|---|
| Scroll-Base-640 | 30 | 2717.6 | ▲+613.50 ✓ | █▄▇▆▆▁▄ | 4511.3 | ▲+1068.60 ✓ | ▇▅▇▅█▁▄ | 0.60x | ▼-0.01 ✗ | ▆▂▄█▁▆▅ |
| Scroll-High-640 | 30 | 2614.4 | ▲+585.20 ✓ | ▇▄▇▆█▁▄ | 4206.4 | ▲+1018.00 ✓ | ▇▅▆▅█▁▄ | 0.62x | ▼-0.02 ✗ | █▁▅▅▃▅▁ |
| Ball-Base-640 | 30 | 827.6 | ▲+126.20 ✓ | ▆▄▇▅█▁▃ | 1253.9 | ▲+313.10 ✓ | ▅▆▇▅█▁▄ | 0.66x | ▼-0.09 ✗ | █▁▄▄▄█▂ |
| Ball-High-640 | 30 | 876.7 | ▲+188.50 ✓ | ▅▇▇█▇▁▄ | 1261.6 | ▲+428.40 ✓ | ▇▆█▂▆▁▆ | 0.69x | ▼-0.14 ✗ | ▁▃▁█▃▄▁ |
| Still-Base-640 | 30 | 4170.1 | ▲+436.90 ✓ | ▄▅▇▁█▁▂ | 5230.1 | ▲+1894.90 ✓ | ▇█▇▇▇▁▅ | 0.80x | ▼-0.32 ✗ | ▃▂▃▁▄█▃ |
| Still-High-640 | 30 | 4044.2 | ▲+545.70 ✓ | ▃▇▆█▇▁▃ | 5089.9 | ▲+1616.10 ✓ | ▄▇▇█▆▁▅ | 0.79x | ▼-0.22 ✗ | ▃▃▃▃▅█▁ |
| Flat-Base-640 | 250 | 2395.1 | ▲+412.50 ✓ | ▆▆█▇▆▁▄ | 7116.2 | ▲+1976.00 ✓ | ▄▅▇▇▆▁█ | 0.34x | ▼-0.05 ✗ | █▆▇▄▅▅▁ |
| CAVLC-Base-640 | 50 | 1888.7 | ▲+400.40 ✓ | ▅▅█▅▅▁▅ | 4119.3 | ▲+1098.50 ✓ | ▅▄█▄▅▁▇ | 0.46x | ▼-0.03 ✗ | ▄▅▅▆█▃▁ |
| Tapo-C110 | 119 | 3183.8 | ▲+861.50 ✓ | ▅▅▇▅█▁▄ | 7434.2 | ▲+2500.10 ✓ | ▆▄▇▅▇▁█ | 0.43x | ▼-0.04 ✗ | ▂▅▆▅█▃▁ |

## ✓ PSNR validation vs ffmpeg
_2 fixtures_

| Fixture | Frames | Avg PSNR (dB) | Δ Avg PSNR (dB) | Trend (Avg PSNR (dB)) | Min PSNR (dB) | Δ Min PSNR (dB) | Trend (Min PSNR (dB)) | Status |
|---|---|---|---|---|---|---|---|---|
| Tapo C110 | 119 | 99.00 | · | ▁▂▂▂█████ | 99.00 | · | ▁▁▁▁█████ | ✓ bit-exact |
| wstress baseline | 5 | 99.00 | · | ▁▁▁▁▁▁▁▁▁ | 99.00 | · | ▁▁▁▁▁▁▁▁▁ | ✓ bit-exact |
