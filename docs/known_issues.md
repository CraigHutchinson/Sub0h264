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

**Major insight update (2026-04-17, session 4):** JM reference decoder
source audited — our 8x8 IDCT arithmetic (transform.hpp inverse8x8)
matches JM's `inverse8x8()` in transform.c line-for-line (including all
3 fixes applied). ffmpeg's `ff_h264_idct8_add` uses different variable
names but equivalent arithmetic (b5 = -JM's b5, compensated at output).

**All three reference decoders (ours, JM, ffmpeg) produce identical
output for same input** — per manual trace of MB(0,0) block 0 with
input coeffs (-7160, -15, 0, ..., 0): all compute residual -112 at
every column → output 16 uniform.

**Session 5 (2026-04-17) — deblock 8x8-mode awareness applied + DRY refactors:**
- transform.hpp: extracted `dct8Butterfly()` helper — single source of truth
  for the 1-D 8-point butterfly (was duplicated across H/V passes; the h(7)
  bug had to be fixed twice before this DRY). Behaviour bit-identical to
  prior code (all tests pass).
- deblock.hpp / decoder.hpp: added per-MB `mbTransform8x8_[]` tracking and
  pass to `deblockMb`. Per §8.7.2, when q-side MB uses 8x8 transform, the
  internal 4x4 edges (vertical x=4,12 / horizontal y=4,12) are skipped.
- deblock.hpp: chroma BS now reuses precomputed luma `edgeBs[]/hEdgeBs[]`
  via `bs = edgeBs[cRow >> 1]` — eliminates ~8x recomputation per MB
  (was calling `computeBs()` 16 times per chroma edge; now 0).

**Result:** Tapo C110 still 6.59 dB; wstress gradient still 25 dB. Confirms
prior session's note that deblock is not the cause of the I_8x8 PSNR gap
(SUB0H264_SKIP_DEBLOCK gives identical PSNR). The deblock fix is a spec
correctness improvement (matches JM/ffmpeg output for 8x8-mode MBs) and a
performance win (chroma BS recomputation eliminated).

**Session 6 (2026-04-17) — 8x8 dequant scale table ROOT CAUSE FIX:**
Lock-step CABAC trace vs JM (JM rebuilt with TRACE=1) showed OUR decoder
produces IDENTICAL scan coefficients to JM for MB(0,0) block 0 in
wstress_gradient (pre-dequant level=-358 at (0,0), -1 at (0,1)) — so CABAC
and the zigzag are correct. Pixel output diverges: ours=16 uniform, JM=16,
16, 16, 16, 16, 16, 17, 17 at row 0.

Root cause located in `cDequantScale8x8` table in transform.hpp. JM's
`dequant_coef8[0]` row 0 = {20, 19, 25, 19, 20, 19, 25, 19}, ours was
{20, 18, 18, 16, 15, 13} — COMPLETELY WRONG VALUES (6 position classes
off by varying amounts). Class derivation was also wrong: class 1 and 2
were split artificially, class 3/4 mixed odd/even wrong.

**Fix:** Rewrote `cDequantScale8x8` and `cDequantPosClass8x8` to match
JM/spec Table 8-16:
  - class 0 (both %4==0):  20, 22, 26, 28, 32, 36
  - class 1 (one divby4, one %4==2): 25, 28, 33, 35, 40, 46
  - class 2 (both %4==2):  32, 35, 42, 45, 51, 58
  - class 3 (one divby4, one odd):   19, 21, 24, 26, 30, 34
  - class 4 (one %4==2, one odd):    24, 26, 31, 33, 38, 43
  - class 5 (both odd):    18, 19, 23, 25, 28, 32

**Results:**
| Fixture                          | Before fix | After scale fix |
|---------------------------------|-----------|-----------------|
| wstress_tapo_gradient (I_8x8)   | 25 dB     | **99 dB**        |
| wstress_tapo_i8x8 (I_8x8)       | 25 dB     | **99 dB**        |
| wstress_tapo_baseline (I_4x4)   | 99 dB     | 99 dB (unchanged) |
| wstress_tapo_complex_flat       | 35 dB     | TBD              |
| Tapo C110 (real, I_4x4 dominant) | 6.59 dB  | 6.58 dB (unchanged) |

**Tapo C110 ROOT CAUSE FIX (2026-04-17 session 7):**
Per-bin lock-step against JM (`scripts/lockstep_compare.py`) located the
first diverging bin at index 12588 in the IDR slice — MB 62 (an I_16x16
DC MB). Both decoders read bit=0 there, but the post-engine state diverged.
Tracing back to MB 60 (I_NxN with cbp=11, qp_delta=-1) and MB 61 (I_NxN
with cbp=0, mb_qp_delta absent), JM's `read_dQuant_CABAC` (cabac.c L1300)
resets `last_dquant = 0` whenever cbp==0 even though mb_qp_delta isn't
actually decoded. We were skipping the reset, leaving prevMbHadQpDelta
"sticky" at TRUE → wrong ctxInc=1 (ctx 61) for MB 62's mb_qp_delta bin0
where JM uses ctxInc=0 (ctx 60).

**Fix:** add `else { prevMbHadNonZeroQpDelta_ = false; }` in both the
CABAC I_NxN path (line ~2742) and CABAC P-inter path (line ~3495) where
we skip mb_qp_delta because cbp==0. Per spec §9.3.3.1.1.5: when
mb_qp_delta is absent, it is treated as having value 0.

**Result: Tapo C110 IDR 6.58 → 46.23 dB (bit-exact CABAC bin trace
vs JM, all 74618 bins match).** Frame avg 38.11 dB; P-frames still at
~26 dB.

**Session 7b — fix early P-frame truncation:**
P-frame 1's CABAC bins were also bit-exact (956/956 vs JM), but our
decoder stopped at MB 916/919, leaving the bottom-right corner of the
frame as uninitialised zeros (max diff 155 at pixel (624,352)+).

Root cause: both the CABAC I-slice loop and the CABAC P-slice loop
started each MB with `if (br.isExhausted()) break;`. CABAC has a 16-bit
lookahead, so the underlying BitReader's bitOffset can reach the end
of the slice NAL while the engine still has multiple bins available.
The check is wrong for CABAC. Termination is via end_of_slice_flag.

**Removed both isExhausted gates.** All ctest pass; wstress still 99 dB.

**Final result: Tapo C110 6.58 → 44.18 dB average (min 39.8 dB,
max 47.6 dB). 38 dB total improvement across 7 fixes this session.**

**Session 8 — I_8x8 intra prediction bugs (modes 4, 5, 7):**
Lock-step IDR pixel comparison vs JM showed first MB with diff > 0 was
the first MB using I_8x8 prediction. Audited mode 4 (VR), mode 5 (HD),
mode 6 (VL), mode 7 (HU) against spec §8.3.2.2.7-10 and JM
intra8x8_pred.c PredArray mappings. Found four distinct bugs:

1. **VR/HD filt121 first arg duplicated middle sample** (Eq 8-117):
   our `extTop[c-(r>>1)-1U+1U]` cancels to `extTop[c-(r>>1)]` — same as
   second arg. Should be `extTop[c-(r>>1)-1]`.

2. **VR/HD negative-zVR formula completely wrong** (Eq 8-118):
   our `ri = (-1-zVR) >> 1` lost half the index. Spec uses linear
   index `i = y - 2x - 1` (or mirror) into the left/top column.

3. **VR/HD zVR=-1 case used wrong index**: spec eq 8-116 gives a
   *constant* value (left[0]+2*Z+top[0]+2)>>2 for ALL zVR=-1 positions
   (which include (0,1), (1,3), (2,5), (3,7)). We used `fl[r-1]` which
   only happened to be right for r=1.

4. **HU `zHU` formula transposed**: spec is `zHU = x + 2y` (col + 2*row);
   we had `r + 2c`. Also our boundary case fired at `zHU == 14` instead
   of spec's `zHU == 13`.

**All wstress synthetic fixtures now bit-exact:**
| Fixture | Before | After |
|---|---|---|
| wstress_tapo_baseline (I_4x4)  | 99 dB | **99 dB** |
| wstress_tapo_i4x4              | 99 dB | **99 dB** |
| wstress_tapo_i8x8              | 99 dB | **99 dB** |
| wstress_tapo_i16x16            | 99 dB | **99 dB** |
| wstress_tapo_complex_flat      | 47.88 dB | **99 dB** |
| wstress_tapo_gradient          | 99 dB | **99 dB** |

**Tapo C110: 44.18 → 56.23 dB average (min 40.93 dB, max 57.94 dB).**
12 dB further improvement on top of session 7. All ctest pass.

**Session 9 — skip-MB prevHadDelta reset:**
Lock-step trace of P-frame 9 (slice 10 in JM, 1340 bins vs 920 normal —
likely a refresh frame) located divergence at bin 751 — same family of
bug as session 7. JM `read_skip_flag_CABAC_p_slice` (cabac.c L604)
ALSO resets `last_dquant = 0` when the MB is skipped, which we missed.

Fix: set `prevMbHadNonZeroQpDelta_ = false` in the CABAC P-slice skip
branch. After fix, ALL 1340 P-frame 9 CABAC bins match JM bit-exact.

Tapo final: **56.33 dB avg, 41.03 dB min** (Tapo IDR is now 99 dB
bit-exact vs JM; remaining gap is in P-frame motion-compensation /
MV-prediction reconstruction — a non-CABAC bug).

**Next session targets** (P-frame reconstruction):
- Frame 9 first diverging MB is (13, 0) — top row, max diff 234 in
  cols 8-15 (right-half pattern). Suggests P_L0_8x16 partition 1 MV
  prediction differs for MBs with no top neighbor (`b`/`c` unavailable).
- Diff cascades through inter-frame MC until next intra-refresh frame.
- All wstress fixtures bit-exact, so MC bug is specific to Tapo's
  rolling-intra-refresh / mid-frame slice pattern.

**Session 10 RESOLVED — P-inter 4x4 zigzag unscan missing (commit b9a5000):**
Eliminated Tapo P-frame divergence entirely. CABAC's `cabacDecodeResidual4x4`
returns coefficients in *scan* order (cZigzag4x4 indexing). Every other
call-site (I_4x4, I_NxN, chroma DC/AC) explicitly unscans to raster before
dequant/IDCT — but the CABAC P-inter 4x4 residual path was passing the
raster `coeffs` array straight into the decoder, leaving every coefficient
at the wrong (x,y) position. Tapo frames 0-8 had no coded 4x4 residual so
the bug was masked; frames 9+ (rolling-intra-refresh) hit it heavily.

**Final Tapo C110 result: 99.00 dB bit-exact across all 119 frames** vs
JM and ffmpeg. All 6 wstress synthetic fixtures remain bit-exact.

---

**Session 11 — P_8x8 sub_mb_type ∈ {8x4, 4x8, 4x4} fully bit-exact:**
Implemented per-sub-partition MVD decode with proper context (§9.3.3.1.1.7),
per-sub-partition MC dispatch using `cSubPartLayout[4][4][4]` table from
§7.3.5.2 / Table 7-13, and unified getMvdComp via mbMotion_ neighbour lookup.
Generator script: `scripts/gen_p8x8_sub_fixture.py` produces
`wstress_p8x8_sub_640x368.h264` (10 frames, per-4x4-tile motion forces
sub-partitioning); JM lockstep wrapper at `scripts/run_jm.py`.

**Final result: 99 dB bit-exact across all 10 frames vs JM and ffmpeg.**

Root cause of frame-7+ divergence (found via JM trace `mb` subcommand on
MB(0,9) at POC 14 = frame 7): in P_8x8/P_8x8ref0, the early refIdx loop
was setting `mbMotion_[].available = true` for all 4 sub-MBs upfront —
*before* their MVs were computed. Subsequent sub-partitions' MV-predictor
C neighbour lookups then saw `available=true` with `mv={0,0}` (zero-init
from slice start) and used a bogus (0,0) C predictor instead of falling
back to D. Fix: only set `.available` in `doPartitionMC` (when the MV is
actually written); change `refIdxCtxInc` to gate on `refIdx > 0` directly
(spec-equivalent since intra/unavailable have `refIdx=-1` from zero-init).

The earlier attempt (Session 11 partial) to add JM's §6.4.10.7 C-availability
override was masking the wrong root cause and broke `scrolling_texture_high`
(51.7 → 18.99 dB). The proper fix removes the polluting available=true
write entirely, so neither override nor manual fallback is needed.

---

**Session 10 investigation (historical, left for reference):**
Traced MB(13, 0) at frameCount_=9. MB is P_8x8 (mb_type=3), 4 sub-partitions
with MVs (0,0), (-2,0), (0,0), (-8,-26). Verified:
- JM POC 9 SH has `frame_num=10, pic_order_cnt_lsb=10`. Uses ref list
  reordering (`modification_of_pic_nums_idc=0, abs_diff=0` → shift by 1)
  + MMCO op 1 with `difference_of_pic_nums_minus1=0`.
- With `num_ref_idx_l0_active=1`, both JM and our default L0 have
  highest-frame_num ref first, so reordering is no-op in practice.
- MVP for part 3 = D fallback (MB(14, 0) C is unavailable pre-decode,
  our code correctly falls back to top-left D which is MB 13 part 0).
  Diag shows c={av=1, ri=0, mv=(0,0)} AFTER D fallback — correct.
- MVD values all match JM (confirmed via JM TRACE=1 `trace_dec.txt`).

So MV prediction matches JM. MC inputs (refX, refY, dx, dy) should match
too. But pixel output differs. Current best hypothesis: subtle issue in
MC interpolation for specific sub-pixel positions, or wrong reference
frame selection at DPB level for frame 9 (despite reordering being
no-op under single-active-ref assumption). Needs next session with
dumps of the actual pixel outputs of MC pre-deblock + pre-residual.

**Tapo C110 lock-step trace investigation (2026-04-17 cont'd):**
Rebuilt JM with TRACE=1 for full syntax-element trace_dec.txt. Compared
mb_type per-MB ours vs JM. First divergence at **MB 8** (first I_16x16
MB in Tapo IDR slice 0). JM mb_type=15 (predMode=DC, cbpLuma=15,
cbpChroma=0); ours mb_type=23 (same predMode/cbpLuma but cbpChroma=2).
We read 2 EXTRA chroma bins.

JM `readMB_typeInfo_CABAC_i_slice` (cabac.c L711-730) uses act_ctx
4,5,6,7,8 added to `ctxIdxOffset=3` → absolute ctx 7,8,9,10,11.
Our code uses ctx 6,7,8,9,10. Off-by-one would seem to be the bug, BUT
applying the JM values regresses wstress_gradient from 99 dB to 5.4 dB.
This suggests the JM mb_type_contexts[] base in JM is offset by 1 from
the spec ctxIdxOffset, OR the original ctx[6..10] is correct per spec
and JM's act_ctx values are an internal artifact.

**Trial fix preserved under `SUB0H264_MBTYPEI_JM_CTX`** preprocessor
guard (cabac_parse.hpp). Default-off; can be combined with future fixes
to test interactions.

**Real Tapo bug is elsewhere** — at MB 8 the I_NxN MBs 0-7 cbp values
match JM exactly (31, 15, 15, 31, 15, 31, 15, 23). So through mb_type
of MBs 0-7 our decoder agrees with JM. Engine state must diverge during
some MB 0-7 residual decode, but the cbp parse stays correct by chance.
The compounding bins eventually cause MB 8 chroma cbp to read wrong.

Last working MB in Tapo: **MB 256** (final nonzero MB; MB 257-919 are
all-zero output). decodeTerminate at end of MB 256 returns 1 (eos)
when JM correctly returns 0, due to accumulated CABAC engine drift.

**Next session targets:**
- Compare per-bin CABAC bin values for MB 0-7 (use the existing
  bin-trace patch in tools/jm_trace) to find the FIRST diverging bin.
- Likely candidate: a residual coefficient bin (level/run/sign) in
  one of the I_4x4 luma 4x4 blocks or the chroma DC.

**Diff pattern for wstress gradient frame 0 (all I_8x8):**
- MB(0,0): max diff = 2, mean = 0.69
- Diff grows monotonically left-to-right and top-to-bottom (worst = 25)
- This is classic prediction-propagation: ~2-pixel error at MB(0,0)
  cascades through intra-prediction neighbors

**Remaining hypothesis (Session 6):** Bug is in CABAC residual decode or
dequant for 8x8 blocks (`cabacDecodeResidual8x8` / `inverseQuantize8x8`).
Verified mathematically equivalent to spec for flat scaling, but worth
trace-comparing actual decoded coefficients against JM for MB(0,0).

Block-level analysis of Tapo MB 15 still confirms LEFT half matches
perfectly; RIGHT half diffs cascade via deblock from MB 16 (I_8x8).

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
