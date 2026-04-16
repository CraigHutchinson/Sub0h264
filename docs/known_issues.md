# Known Issues

## Active Quality Issues

### CABAC quality at 640x368 (Tapo C110 stream)
- IDR frame diverges at ~MB(15,0): first 15 MBs correct, then ~6 dB PSNR
- 320x240 CABAC streams decode at 51+ dB — issue is resolution-specific
- Not caused by DPB or MMCO — the IDR I-slice itself decodes wrong pixels
- Needs investigation: likely a CABAC context or residual path issue at wider frames

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

### CABAC P-frame quality — FIXED (6 dB → 48+ dB avg, 11 bugs)
All P-frame slices verified bit-exact against JM reference decoder.
- **MVD 3rd-order Exp-Golomb** (cabac_parse.hpp): k=0→k=3 per §9.3.2.3
- **P_8x8 motion/MVD storage** (decoder.hpp): overwrote all 16 blocks per §6.4.2.1
- **Skip MB CBP** (decoder.hpp): left at default 0x2F instead of 0 per §9.3.3.1.1.4
- **ref_idx context** (decoder.hpp): hardcoded ctxInc=0, now from neighbors §9.3.3.1.1.6
- **ref_idx unbounded unary** (cabac_parse.hpp): truncated→unbounded matching JM §9.3.3.1.2
- **Per-partition ref lookup** (decoder.hpp): single ref→per-partition via DPB L0 §8.4.2
- **Immediate refIdx storage** (decoder.hpp): all partition types for neighbor context
- **I-in-P mb_type contexts** (cabac_parse.hpp): used I-slice ctx 3-10 instead of P-slice 17-20 §9.3.3.1.2
- **transform_size_8x8_flag neighbor context** (decoder.hpp): fixed ctxInc=0→neighbor-dependent §9.3.3.1.1.10
- **DRY refactor** (decoder.hpp): extracted decodeCabacTransform8x8Flag helper for I_NxN + P-inter paths

### DPB non-reference slot reuse — FIXED (6 → 119 frames on Tapo stream)
- **getDecodeTarget evicting active refs** (dpb.hpp): When MMCO op=1 unmarked a
  reference, the slot stayed `occupied=true` but `isReference=false`. With
  `max_num_ref_frames=1` (2 DPB slots), getDecodeTarget had no free slots and
  evicted the only remaining reference. Fix: prefer reusing occupied non-reference
  slots before evicting active references. Impact: Tapo C110 stream went from 6
  decoded frames to 119 (matches ffmpeg).

### CABAC syntax fixes applied (earlier)
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

### 8x8 inter transform — FIXED
- Flag decoded with neighbor context, 8x8 residual + IDCT applied for P-inter

### Constrained intra prediction
- Flag parsed, not enforced in prediction
