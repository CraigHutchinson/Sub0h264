# Trace Tool Design

## Goals

A first-class diagnostic tool that:
1. Shares the same decode code paths as the library (no separate decoder)
2. Adds zero overhead when not used (compile-time elimination)
3. Supports command-line filtering by MB position, block, event type
4. Supports multiple detail levels (summary, per-MB, per-block, per-coefficient)
5. Can be used interactively for debugging and in CI for regression testing

## Architecture

```
┌─────────────────────────────────────────────┐
│  sub0h264 library (components/sub0h264/)    │
│                                             │
│  Decoder (decoder.hpp)                      │
│    └── DecodeTrace callback system          │
│         ├── Always compiled (zero cost)     │
│         ├── Callback: std::function<>       │
│         └── Filter: mbX, mbY, blk, type    │
└─────────────────────────────────────────────┘
         │ callback events
         ▼
┌─────────────────────────────────────────────┐
│  sub0h264_trace tool (test_apps/trace/)     │
│                                             │
│  CLI binary that links sub0h264 library     │
│  and registers a trace callback.            │
│                                             │
│  Arguments:                                 │
│    <input.h264>                             │
│    --frame N          (decode frame N only) │
│    --mb X,Y           (filter to one MB)    │
│    --block B          (filter to one 4x4)   │
│    --level summary|mb|block|coeff|pixel     │
│    --dump <output.yuv> (dump decoded frame) │
│    --compare <ref.yuv> (per-pixel diff)     │
│                                             │
│  Output levels:                             │
│    summary: per-frame PSNR, frame count     │
│    mb:      per-MB type, QP, bit offset     │
│    block:   per-4x4 nC, tc, pred mode, bits │
│    coeff:   raw coefficients + dequant      │
│    pixel:   prediction + residual + output  │
└─────────────────────────────────────────────┘
```

## Trace Event Expansion

Current events cover MB start/end and a few block-level traces. Expand to:

| Event | Detail Level | Data |
|-------|-------------|------|
| FrameStart | summary | frameIdx, sliceType, QP, width×height |
| FrameDone | summary | frameIdx, bitOffset, PSNR (if ref available) |
| MbStart | mb | mbX, mbY, mbType, bitOffset |
| MbDone | mb | mbX, mbY, bitOffset, QP |
| BlockPredMode | block | scanIdx, rasterIdx, predMode, MPM |
| BlockResidual | block | scanIdx, nC, totalCoeff, totalZeros, bits |
| BlockCoeffs | coeff | scanIdx, raw levels[], zigzag coeffs[] |
| BlockDequant | coeff | scanIdx, dequant coeffs[] |
| BlockPixels | pixel | scanIdx, pred[16], residual[16], output[16] |
| ChromaDc | coeff | plane, raw[], dequant[] |

## Implementation Plan

1. **Expand TraceEvent** — add event types, structured data union
2. **Add trace points** — at block prediction, residual decode, and pixel output
3. **Create CLI tool** — `test_apps/trace/` with argparse-style argument handling
4. **Remove ad-hoc printf** — replace scattered `#if SUB0H264_TRACE` blocks
   with structured trace events that work in both callback and printf modes
5. **Add regression test mode** — `--compare ref.yuv --threshold 0.5` for CI

## API Changes

The decoder's trace system already supports callbacks. The key change:
add trace events at every significant decode step, gated by
`trace_.shouldTrace(mbX, mbY)` (callback-only, no compile flag needed).

For performance: the `shouldTrace()` check is a single pointer comparison
(callback_ != nullptr) followed by filter checks. When no callback is set,
the entire trace path is eliminated by the branch predictor.

## Build

```cmake
# test_apps/trace/CMakeLists.txt
add_executable(sub0h264_trace main.cpp)
target_link_libraries(sub0h264_trace PRIVATE Sub0h264::Sub0h264)
```

Desktop only (not ESP32-P4) — trace output goes to stdout/files.
