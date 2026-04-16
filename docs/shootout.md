# Sub0h264 vs libavc Shootout

Performance comparison between Sub0h264 and [libavc](https://github.com/ArtifexSoftware/libavc)
(Android/Ittiam H.264 decoder) on both desktop and ESP32-P4.

Both decoders are built from source and linked as static libraries — no
subprocess overhead. libavc runs single-core (`u4_num_cores = 1`) for fair
comparison.

## ESP32-P4 Results (360 MHz RISC-V, single-core, no SIMD)

| Stream | Frames | Sub0h264 (fps) | libavc (fps) | Ratio |
|--------|--------|----------------|--------------|-------|
| Scroll-Base-640 | 30 | **24.8** | 17.7 | **1.40x** |
| Scroll-High-640 | 30 | **24.6** | 17.2 | **1.43x** |
| Ball-Base-640 | 30 | **12.0** | 10.9 | **1.10x** |
| Ball-High-640 | 30 | **12.0** | 10.8 | **1.11x** |
| Still-Base-640 | 30 | **33.1** | 19.3 | **1.71x** |
| Still-High-640 | 30 | **32.9** | 18.9 | **1.74x** |
| Flat-Base-640 | 30 | **34.1** | 18.7 | **1.82x** |
| CAVLC-320 | 30 | 39.4 | 41.0 | 0.96x |
| CABAC-320 | 30 | **38.0** | 30.4 | **1.25x** |
| **Tapo-C110** | **119** | **25.3** | **22.4** | **1.13x** |

Sub0h264 wins **9 of 10** streams, ranging from 1.10x to 1.82x faster.
The only loss is CAVLC-320 (0.96x, essentially tied).

**Production target met**: Tapo C110 stream decodes at 25.3 fps on
ESP32-P4, exceeding the 25 fps camera frame rate.

## Desktop Results (x86-64, MSVC Release)

| Stream | Frames | Sub0h264 (fps) | libavc (fps) | Ratio |
|--------|--------|----------------|--------------|-------|
| Scroll-Base-640 | 30 | 2869 | 4667 | 0.61x |
| Scroll-High-640 | 30 | 2716 | 4215 | 0.64x |
| Ball-Base-640 | 30 | 863 | 1245 | 0.69x |
| Ball-High-640 | 30 | 851 | 1215 | 0.70x |
| Still-Base-640 | 30 | 4373 | 5337 | 0.82x |
| Still-High-640 | 30 | 4129 | 4883 | 0.85x |
| Flat-Base-640 | 250 | 2373 | 6111 | 0.39x |
| CAVLC-Base-640 | 50 | 1816 | 3619 | 0.50x |
| Tapo-C110 | 119 | 3082 | 6126 | 0.50x |

On desktop with x86 SIMD, libavc is 1.2-2.6x faster due to hand-tuned
SSE/AVX intrinsics. Sub0h264 uses generic C++ without platform SIMD.

## Why Sub0h264 wins on ESP32-P4

On x86 with SIMD, libavc's hand-tuned assembly dominates. On RISC-V
without SIMD extensions, both decoders run generic C/C++ code and
Sub0h264's advantages emerge:

- **Simpler architecture** — header-only, no function pointer dispatch
- **Better cache locality** — single-pass decode with inline prediction
- **Lower overhead** — no thread management, buffer manager, or display
  manager infrastructure
- **C++23 optimisations** — constexpr tables, zero-cost abstractions

## Build & Run

### Desktop Shootout (MinGW required for libavc)

```bash
cmake -G "MinGW Makefiles" -B build-shootout -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ \
      test_apps/shootout
cmake --build build-shootout
./build-shootout/sub0h264_shootout --fixtures-dir tests/fixtures
```

libavc source must be at `docs/reference/libavc/` (clone from GitHub).

### ESP32-P4 Shootout

```bash
cd test_apps/esp_shootout

# Configure
pwsh -NoProfile -File ../../scripts/nomsys.ps1 cmake --preset esp32p4

# Build
pwsh -NoProfile -File ../../scripts/nomsys.ps1 cmake --build --preset esp32p4

# Flash
ESPPORT=COM5 pwsh -NoProfile -File ../../scripts/nomsys.ps1 \
    cmake --build --preset esp32p4 --target flash

# Capture results (resets device, captures serial output)
python cmake/esp32p4_serial_capture.py \
    --port COM5 --test-timeout 300 \
    --log-file shootout.log \
    --success-pattern "Shootout complete"
```

### Generate Report

```bash
# From desktop shootout:
./build-shootout/sub0h264_shootout --fixtures-dir tests/fixtures 2> shootout.log
python scripts/gen_bench_report.py --shootout shootout.log --outdir docs

# From CTest bench suite:
ctest --preset bench 2> bench.log
python scripts/gen_bench_report.py --bench bench.log --outdir docs
```

## Benchmark Vectors

| ID | Fixture | Resolution | Frames | Profile | Content |
|----|---------|-----------|--------|---------|---------|
| Scroll-Base-640 | bench_scroll_baseline_640x480.h264 | 640x480 | 30 | Baseline | Diagonal scroll 4px/frame |
| Scroll-High-640 | bench_scroll_high_640x480.h264 | 640x480 | 30 | High | CABAC diagonal scroll |
| Ball-Base-640 | bench_ball_baseline_640x480.h264 | 640x480 | 30 | Baseline | Bouncing ball on noise |
| Ball-High-640 | bench_ball_high_640x480.h264 | 640x480 | 30 | High | CABAC bouncing ball |
| Still-Base-640 | bench_still_baseline_640x480.h264 | 640x480 | 30 | Baseline | Static checkerboard (skip-heavy) |
| Still-High-640 | bench_still_high_640x480.h264 | 640x480 | 30 | High | CABAC static scene |
| Flat-Base-640 | bench_flat_baseline_640x480.h264 | 640x480 | 30 | Baseline | Flat black (best-case ceiling) |
| CAVLC-320 | scrolling_texture_baseline.h264 | 320x240 | 30 | Baseline | Synthetic scroll I+P |
| CABAC-320 | scrolling_texture_high.h264 | 320x240 | 30 | High | CABAC scroll I+P |
| Tapo-C110 | tapo_c110_stream2_high.h264 | 640x360 | 119 | High | Real-world camera stream |

## Methodology

- **Validate**: decode stream, verify frame count > 0
- **Warm-up**: one discard pass (prime caches)
- **Measure**: 2 timed passes, report median fps
- **libavc**: single-core, PSRAM output buffers on ESP32, `_aligned_malloc` on desktop
- **Sub0h264**: `std::make_unique<H264Decoder>` per pass (fresh state)
- **Output**: `SHOOTOUT_JSON` lines on stdout for automated parsing
