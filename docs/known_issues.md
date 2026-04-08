# Known Issues

## Active Quality Issues

### CABAC coefficient decode: ~14 dB (target 45+ dB)
- **Fixtures**: All CABAC fixtures (cabac_min_u128, cabac_1mb_noisy, etc.)
- **Root cause**: CABAC engine state diverges during prediction mode decode.
  ctx[68] (prev_intra4x4) starts with MPS=0 at typical QPs, making prevFlag=1
  (use MPM) the LPS. After ~7 consecutive LPS decodes, pState reaches 0 and
  the context behavior changes. The engine arithmetic is verified bin-exact
  against an independent reference implementation.
- **Key finding**: Forcing CBP=0 for the first MB produces pixel-perfect output,
  proving the shared pipeline (dequant/IDCT/prediction) is correct.
- **Bitstream overrun**: The CABAC decode reads past the end of the RBSP data.
  Overrun protection now stops decoding when the bitstream is exhausted.
- **Investigation**: Spec-only agent working on independent u128 decode from spec.

## Closed / Fixed Issues

### CAVLC quality — FIXED (bouncing ball 29.5 → 46.1 dB)
- **MPM bug** (decoder.hpp): When either neighbor MB is unavailable, MPM must
  be forced to DC(2). Was using min(available_mode, DC) which gave MPM=1
  instead of 2 for blocks at frame boundary. Impact: +16.6 dB bouncing ball,
  +38 dB horizontal gradient. All 8 gradient axes now match ffmpeg within 0.5 dB.
- **P-frame reference ordering** (decoder.hpp): `getReference(0)` was called
  BEFORE `buildRefListL0()`, using stale L0 list from previous frame. Impact:
  +34 dB minimum PSNR on all P-frame fixtures. All motion patterns (9 types ×
  7 resolutions) now match ffmpeg exactly.
- **DPB minimum size** (dpb.hpp): Ensure at least 2 DPB entries even with
  numRefFrames=0 to prevent decode target and reference sharing same buffer.

### CABAC syntax fixes applied
- **rem_intra4x4_pred_mode** (cabac_parse.hpp): Changed from bypass bins to
  context-coded bins at ctxIdx=69, LSB-first ordering. Per ffmpeg/spec.
- **coded_block_flag for DC blocks** (cabac_parse.hpp): Removed skip for
  ctxBlockCat 0/3. Spec §7.3.5.3.3 always decodes coded_block_flag.
- **end_of_slice_flag** (decoder.hpp): Added decodeTerminate() after each
  CABAC MB per §7.3.4. Required for correct inter-MB engine state.
- **Bitstream overrun protection** (decoder.hpp): CABAC I and P loops bail
  when bitstream exhausted to prevent infinite decode on corrupt data.

## Resilience / Error Handling Status

### Present (verified)
- NAL forbidden_zero_bit rejection
- SPS/PPS parameter range validation (invalid ID, out-of-range values)
- Slice header field validation
- CBP code range check (0-47 for ue(v))
- QP range enforcement (0-51 clamping)
- Exp-Golomb overflow detection (>31 leading zeros)
- CABAC engine range check
- CABAC end_of_slice_flag per MB (§7.3.4)
- Bitstream exhaustion check in CABAC loops
- Reference frame null check before MC
- Frame allocation verification
- DPB slot availability
- Motion vector reference clamping
- Pixel output clipping [0,255]
- Start code detection (3-byte and 4-byte)
- Emulation prevention byte handling
- Chroma format validation

### TODO: Resilience gaps to implement
- §7.4.2.1: SPS seq_parameter_set_id strict range [0, 31]
- §7.4.3: slice_type range validation (reject > 9)
- §7.4.5: mb_qp_delta causing QP outside [-(26+QpBdOffset), +(25+QpBdOffset)]
- §8.4.2.2: Motion compensation out-of-frame sample clamping (border extension)
- §9.2: CAVLC table index bounds checking (nC, tc, trailing ones)
- §9.3: CABAC codIRange falling to 0 detection (corrupted arithmetic)
- §A.3: Level-specific constraints (max MBs per frame, max DPB frames)
- §B.1: Incomplete start code robustness (truncated streams)
- Annex C: Hypothetical Reference Decoder (HRD) conformance testing
- §6.4.1: MB address wrapping for multi-slice boundaries
- Error concealment: DC prediction fallback for corrupted MBs
- Profile/level compatibility check before decode attempt

## Feature Gaps

### B-slice support
- Slice header parsed, decode returns error for SliceType::B
- Requires: L1 reference list, bi-prediction, direct mode

### Scaling list application
- Lists parsed in SPS/PPS, not applied to dequantization
- Flat scaling (weight=16) always used

### 8x8 inter transform
- Flag decoded, value discarded — always uses 4x4 for inter residual

### Constrained intra prediction
- Flag parsed, not enforced in prediction
