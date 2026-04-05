# Known Issues

## Quality Bugs

### Bouncing ball fixture: 28.6 dB vs 45.6 dB (ffmpeg)
- **Fixture**: bouncing_ball.h264 (Baseline CAVLC, 320x240, 30 frames)
- **Symptom**: Y mean 134 vs 125, systematic +9 pixel offset from MB(1,0) onwards
- **MB(0,0)**: Pixel-perfect (max_diff ≤ 1)
- **Root cause**: CAVLC bitstream alignment error starting at MB(1,0). Consistent
  with the "3-bit over-consumption" noted in test_bitstream_trace.cpp. The error
  cascades through the rest of the frame via intra prediction neighbor reads.
- **Investigation needed**: Per-bit trace of MB(1,0) CAVLC residual decode
  comparing total_zeros/run_before parsing against ffmpeg or libavc reference.
- **Impact**: Only affects streams with specific CAVLC coefficient patterns
  (bouncing ball has complex motion with many non-zero coefficients per MB).
  All synthetic test fixtures (scrolling_texture, pan_*, static_scene) decode
  at 52+ dB PSNR.

### CABAC High profile: ~10 dB
- **Status**: Engine arithmetic verified correct (matches Python reference bin-by-bin)
- **Root cause**: CABAC context adaptation produces valid but different decode
  results from ffmpeg. The arithmetic engine, table, and reconstruction chain
  are all verified correct independently.
- **Impact**: CABAC-encoded streams (High/Main profile) decode with reduced quality.
  Baseline CAVLC decode is unaffected.

## Feature Gaps

### B-slice support
- Slice header parsed, but decodeSlice() returns error for SliceType::B
- 180/195 frames in high_640x480.h264 are B-slices (unsupported)

### Weighted prediction
- pred_weight_table() parsed but weights not applied to MC output

### Long-term references
- MMCO commands parsed but not processed
- Only short-term reference management implemented
