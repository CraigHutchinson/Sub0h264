# Known Issues

## Unsupported features (graceful error)

Per fix_theme_analysis.md (2026-04-17), the following features trigger a
debug-build assert and a `DecodeStatus::Error` / `return false` in release
via `SUB0H264_UNSUPPORTED(msg)`. Streams using these features are rejected
cleanly rather than producing silent corruption or hard crashes.

| Feature | Location | Spec | Impl plan |
|---------|----------|------|-----------|
| B-slices, SI/SP slices | `decoder.hpp` | §7.3.4 | Phase B (large effort) |
| disable_deblocking_filter_idc == 2 | `decoder.hpp` | §8.7 | Phase B (per-MB slice tracking) |
| CABAC I_PCM | `decoder.hpp` (4 sites) | §9.3.3.1.1 | Phase B (byte-align + 320-byte raw copy) |
| CAVLC I_PCM | `decoder.hpp` | §7.3.5 | Phase B |
| P_8x8 sub_mb_type ∈ {8x4,4x8,4x4} | `decoder.hpp` | §7.3.5.2 | Phase B (per-sub-partition MV storage) |
| CAVLC I_NxN + transform_8x8_mode=1 | `decoder.hpp` | §7.3.5 | Phase B (rare combo) |
| CAVLC P-inter + transform_8x8_mode=1 + cbpLuma>0 | `decoder.hpp` | §7.3.5 | Phase B |
| Custom scaling lists (SPS/PPS) | `decoder.hpp` | §8.5.12.1 | **Backlog** — verified 2026-04-17: zero fixtures (incl. Tapo C110 High) use custom scaling lists. All current High-profile streams use flat weightScale=16. Implement only when a real stream requiring it appears. |
| FMO (slice groups) | `pps.hpp` | §7.3.2.3 | Out of scope (rare) |
| MBAFF (interlaced) | `sps.hpp` | §7.3.3 | Out of scope |
| MVC (multi-view) | `slice.hpp` | | Out of scope |

## Active Quality Issues

### CABAC I_8x8 decode bugs (Tapo C110 + width-stress fixtures)
**Status: MULTIPLE FIXES APPLIED, investigation continuing (2026-04-17)**.

**Investigation progress:**
- Deblocking ruled out (identical PSNR with SUB0H264_SKIP_DEBLOCK)
- Bisect via 10 new `wstress_*` synthetic fixtures isolated bug to CABAC
  I_8x8 path (CAVLC, I_4x4, I_16x16 all pass at 640x368)

**Fix #1 — Block-3 top-right availability** (intra_pred.hpp, §6.4.10.4):
Block 3 (bottom-right 8x8) was reading undecoded samples from MB(X+1, Y).
Fixed to mark unavailable, replicate top[7].

**Fix #2 — 8x8 IDCT h(7) wrong input** (transform.hpp, §8.5.12.2 Eq 8-332):
Odd butterfly used `(s7 >> 1)` instead of `(s1 >> 1)`. Verified against
libavc reference.

**Fix #3 — 8x8 IDCT sign error at output positions 1 and 6** (transform.hpp):
Verified via single-coefficient test: input s1=16 should produce monotonic
output (24, 20, 12, 6, -6, -12, -20, -24) but we produced sign flips
(24, -20, 12, 6, -6, -12, 20, -24). Swapped +/- on positions 1 and 6 in
both horizontal and vertical passes.

**Current fixture status after fixes:**
| Fixture | Before | After fix 1 | After fix 2 | After fix 3 |
|---------|--------|-------------|-------------|-------------|
| wstress_wide24_gradient | FAIL 29 | FAIL 29 | PASS 48 | PASS 55 |
| wstress_tapo_size_complex_flat | FAIL 19 | 31 | 31 | 35 |
| wstress_tapo_size_gradient | FAIL 15 | FAIL 20 | 25 | 25 |
| wstress_tapo_size_i8x8 | FAIL 15 | FAIL 20 | 25 | 25 |
| Tapo C110 (real) | FAIL 6.43 | 6.59 | 6.59 | 6.59 |

**Remaining bug(s)** — investigation continuing:
- Tapo C110 still at 6.59 dB (unchanged by IDCT fixes)
- New diff analysis shows Tapo first diff at MB(15, 0) block(1, 0) with
  rows 0-3 of MB(0,0) block 0 now matching perfectly. Bug has moved
  right and down in the frame as IDCT/prediction gets more correct.
- Gradient wstress fixtures still at 25 dB — likely additional subtle
  IDCT/dequant issue

**Next investigation:** Compare MB(15, 0) decoded coefficients against
JM reference bin-by-bin.

**Major insight update (2026-04-17, session 3):** Block-level analysis of
MB 15 reveals the LEFT half (blocks 0 and 2, cols 240-247) matches ffmpeg
PERFECTLY. Only the RIGHT half (blocks 1 and 3, cols 248-255) has tiny
1-pixel diffs at the MB 15/16 boundary. MB 16 uses CABAC I_8x8 (same
path as the 25 dB wstress gradient bug). Hypothesis: the Tapo divergence
at MB 15 is actually **MB 16's I_8x8 error propagating back via
deblocking** into MB 15's right edge. I_16x16 DC decode at MB 15 is
verified correct — fixing the remaining I_8x8 bug should eliminate
Tapo divergence as a side effect.

**Scope update (2026-04-17, session 2):** Per-MB mode tracing showed that
Tapo MB 15 uses **CABAC I_16x16 DC mode** (mbType=3, predMode=DC,
cbpLuma=0) while MB 16 is CABAC I_8x8.
Tapo MB 15 uses I_16x16 with mbType=3 (predMode=DC, cbpLuma=0, cbpChroma=0)
— a "DC-only" path that decodes only the 4x4 Hadamard DC coefficients,
with no AC residual. The 1-pixel diffs at MB(15, 0) block 1 come from
this DC-only decode path.

Mode distribution in Tapo IDR row 0 (from diagnostic):
- MB 0-7: I_4x4 CABAC (decode correctly)
- MB 8-10: I_16x16 (mbType 15, 19, 19)
- MB 11-13: I_4x4
- MB 14-15: **I_16x16 mbType=19, mbType=3** ← MB 15 = DC-only I_16x16
- MB 16+: I_8x8

The diff first appears at MB 15 because that's the first I_16x16 with
mbType=3 (DC-only). Earlier I_16x16 MBs with mbType=19 (different CBP)
work correctly — suggests the bug is specific to the cbpLuma=0 DC-only path
OR to the specific predMode=DC combined with specific neighbor state.

Additionally, wstress gradient fixtures still at 25 dB reveal a separate
I_8x8 IDCT rounding bug (tracked separately).

**New regression fixtures:** `tests/fixtures/wstress_*.h264` (10 files)
permanently cover the width/height/intra-mode gap.

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
