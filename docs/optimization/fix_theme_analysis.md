# Fix Theme Analysis (Last 14 Days)

Review of **37 fix commits** over 14 days to identify recurring patterns in
bugs. Hypothesis: code was written for specific test fixtures rather than
being spec-complete, leading to systematic gaps. This document identifies
the categories and proposes a preventive audit plan.

## Commit density breakdown

```
2026-04-17 :  2 fix commits (today — I_8x8 + width-stress investigation)
2026-04-16 :  9 fix commits (benchmark infrastructure + CABAC cleanup)
2026-04-15 :  7 fix commits (multi-ref CABAC P-frame resolution)
2026-04-11 :  3 fix commits (ESP32 build + chroma CBP + I_4x4 DC pred)
2026-04-10 :  2 fix commits (CABAC root-cause + IDCT normalization)
2026-04-09 : ~8 fix commits (CABAC table rangeLPS + systematic audit)
2026-04-08 :  2 fix commits (chroma pred context + neighbor state refactor)
2026-04-05 : ~6 fix commits (DiagDownRight + bouncing ball investigation)
```

---

## Theme 1: CABAC Context Derivation — 13+ fixes (DOMINANT)

Every time a new content pattern or test fixture is added, another CABAC
`ctxIdxInc` derivation is found wrong. Almost all reference §9.3.3.1.1.x
subsections:

| # | Fix | Spec ref |
|---|-----|----------|
| 1 | transform_size_8x8_flag context from neighbors | §9.3.3.1.1.10 |
| 2 | I-in-P mb_type uses P-slice ctx 17-20 not I-slice 3-10 | §9.3.3.1.2 |
| 3 | ref_idx unbounded unary matching JM | §9.3.3.1.2 |
| 4 | ref_idx context from neighbors (not hardcoded 0) | §9.3.3.1.1.6 |
| 5 | Skip MB CABAC neighbor CBP must be 0 not default 0x2F | §9.3.3.1.1.4 |
| 6 | Chroma CBP condTermFlag inversion (chroma sense opposite luma) | §9.3.3.1.1.4 |
| 7 | intra_chroma_pred_mode ctx from neighbor chroma modes | §9.3.3.1.1.7 |
| 8 | CABAC context offsets + coded_block_flag wiring | §9.3.3.1.1.9 |
| 9 | I_4x4 dcPredModePredictedFlag (force DC when neighbor unavail) | §8.3.1.1 |
| 10 | 6 rangeLPS errors in cCabacTable (packed table typos) | §9.3.3.2 |
| 11 | MVD 3rd-order Exp-Golomb (was k=0, should be k=3) | §9.3.2.3 Table 9-37 |
| 12 | Per-partition ref_idx lookup (was single-ref) | §8.4.1.3 |
| 13 | rem_intra4x4_pred_mode bypass→context at ctxIdx=69 | §9.3.3.1 |

**Root cause pattern:** CABAC context derivation was often implemented by
reading the parent section prose but missing sub-table rules or
negative-availability clauses. Each bug hides until a specific bitstream
exercises that exact context path.

---

## Theme 2: Intra Prediction Neighbor Availability — 5+ fixes

| # | Fix | Spec ref |
|---|-----|----------|
| 1 | I_8x8 block-3 top-right (today — next MB not yet decoded) | §6.4.10.4 Table 6-3 |
| 2 | I_4x4 DiagDownRight: extended reference array | §8.3.1.2 |
| 3 | Intra-in-P: copy P-frame pixels before intra decode | §8.3.1 |
| 4 | Intra-in-P: copy decoded MB to DPB target for neighbor use | §8.3.1 |
| 5 | dcPredModePredictedFlag: force DC both when either unavail | §8.3.1.1 |

**Root cause pattern:** Availability rules at §6.4.10.4 Table 6-3 have three
orthogonal dimensions (frame edge / MB edge / scan order within MB). Each
combination needs explicit handling. Ad-hoc boolean checks (e.g.
`absX + c < frame.width()`) cover only the frame-edge dimension.

**Critical observation:** I_4x4 has a properly-derived compile-time table
`cTopRightUnavailScan[16]` based on §6.4.11 — but I_8x8 had a manual
`absX + c < frame.width()` check that missed the "next MB not yet decoded"
case. The I_4x4 approach should be the model for all block-availability
logic.

---

## Theme 3: Multi-reference / DPB — 6+ fixes

| # | Fix | Spec ref |
|---|-----|----------|
| 1 | DPB non-ref slot reuse (MMCO adaptive marking) | §8.2.5.4 |
| 2 | DPB eviction double-assignment | §8.2.5.3 |
| 3 | Per-partition ref_idx for 16x16/16x8/8x16 | §7.3.5.1 |
| 4 | Per-partition reference lookup via L0 list | §8.4.2 |
| 5 | Multi-ref: actual ref_idx for MV prediction + MC | §8.4.1.3 |
| 6 | Immediate refIdx storage (not deferred) for neighbor context | §9.3.3.1.1.6 |

**Root cause pattern:** Initial implementation assumed single reference
frame. Multi-ref required re-plumbing many paths (MV prediction, MC, CABAC
neighbor context). Similar refactor scope would be needed for B-frames.

---

## Theme 4: Partition / Motion — 4+ fixes

| # | Fix | Spec ref |
|---|-----|----------|
| 1 | P_8x8 per-sub-partition MVD storage (was overwriting all 16) | §6.4.2.1 |
| 2 | setPartitionMotion cover ALL P-slice mb_type values | §6.4.2.1 |
| 3 | MV prediction B=A, C=A substitution when unavailable | §8.4.1.3 |
| 4 | te(v) ref_idx decode: invert bit | §9.1 |

**Root cause pattern:** Coverage of mb_type enumeration incomplete — fixed
for known types (16x16, 16x8, 8x16) first, with 8x8 sub-partitions added
later when tests exposed them.

---

## Theme 5: Transform / Dequant Arithmetic — 4+ fixes

| # | Fix | Spec ref |
|---|-----|----------|
| 1 | CABAC DC double-dequant in I_16x16 | §8.5.12.1 |
| 2 | IDCT normalization — ffmpeg-style qmul + dual >>6 | §8.5.12.2 |
| 3 | CLZ Exp-Golomb: left-align peeked bits before CLZ | §9.1 |
| 4 | QP wrapping: `((qp % 52) + 52) % 52` for negative deltas | §7.4.5 |

**Root cause pattern:** Arithmetic rounding and normalization subtleties
easy to get wrong. Test fixtures don't catch these when ranges are small.

---

## Theme 6: Unimplemented Branches Still in Codebase (⚠️ PRODUCTION RISK)

These `assert(...)` statements will **abort the decoder** when triggered.
On embedded, this is a hard crash. Not all are documented as gaps.

| Location | Feature | Crash-on-trigger? |
|----------|---------|-------------------|
| `decoder.hpp:770` | CABAC I_PCM | No — silently returns true (**silent corruption**) |
| `decoder.hpp:861` | B-slices, SI/SP | Yes (assert) |
| `decoder.hpp:873` | disable_deblocking_filter_idc == 2 | Yes |
| `decoder.hpp:1237` | CAVLC I_NxN with pps.transform8x8Mode_ == 1 | Yes |
| `decoder.hpp:1940` | P_8x8 sub_mb_type != 0 (8x4/4x8/4x4) | Yes |
| `decoder.hpp:2311` | CAVLC P-inter transform_8x8_mode with cbpLuma>0 | Yes |
| `sps.hpp` (scaling_list_data parsed) | **Scaling lists NOT applied** to dequant | No — silently decodes with flat matrix |
| `pps.hpp:103` | FMO | Yes (Result::ErrorUnsupported) |
| `slice.hpp` | MVC | Partial — always uses non-MVC variant |
| `decoder.hpp:1054` | QP wrap correctness TODO | No (silently wraps — may be wrong) |

**Silent corruption cases** (worst kind):
- CABAC I_PCM returns true without decoding PCM bytes → uses undefined pixels
- Scaling lists parsed in SPS/PPS but never applied → decode uses flat matrix (16) instead of specified weights
- QP wrapping without proper spec validation

---

## Theme 7: Resolution/Width-Dependent Bugs (⚠️ UNCAUGHT UNTIL TODAY)

No synthetic fixtures exist between 320x240 (20 MBs wide) and 640x480
(40 MBs wide). No fixtures with non-16-aligned height. No fixtures with
frame cropping. Today's I_8x8 bug slipped through for this reason.

**Test coverage gap actively being closed:** 10 new `wstress_*` fixtures
added this session.

---

## Cross-cutting Pattern: "Implemented for the test, not for the spec"

Reviewing fixes chronologically, almost every fix has the shape:

1. A new fixture type is added (specific content, resolution, profile)
2. Decode produces wrong output
3. Investigation reveals a code path used by the new fixture was never
   exercised by prior fixtures
4. Fix adds the missing branch/condition
5. Spec citation confirms the code NOW matches spec — but the original
   implementation was incomplete

The **I_8x8 top-right bug** is a textbook example:
- I_4x4 had a proper spec-derived `cTopRightUnavailScan` table
- I_8x8 had a manual `absX + c < frame.width()` check
- Same problem, different solutions — the I_4x4 one was spec-complete,
  the I_8x8 one was "good enough for small fixtures"

---

## Recommended Preventive Audit — Focus Areas

### Priority 1 — Remove the "assert(false)" cliff
Replace runtime asserts with either:
(a) full implementation, or
(b) graceful error return (`Result::ErrorUnsupported`) with a logged warning
(c) feature detection in SPS/PPS that rejects streams at init time

Impact: avoids hard crashes on real-world streams that use these features.

### Priority 2 — Audit all §6.4.10.4 neighbor availability uses
Every block-level decode that uses neighbors must derive availability
from the spec-complete `cTopRightUnavailScan`-style table, never from
ad-hoc boolean checks. Known locations:

- `intra_pred.hpp:intraPred4x4Luma` ✅ uses table
- `intra_pred.hpp:intraPred8x8Luma` ⚠️ **FIXED TODAY** (manual check)
- `intra_pred.hpp:intraChromaPred` — audit required
- `intra_pred.hpp:intraPred16x16Luma` — uses 16-pixel top row, no top-right extension (likely OK)
- `cabac_neighbor.hpp` methods — audit required

### Priority 3 — Audit all CABAC context derivations systematically
Create a test matrix: for each CABAC syntax element in §9.3.3.1.1.x,
generate a fixture that exercises every ctxIdxInc branch. Count:
§9.3.3.1.1.1 through §9.3.3.1.1.13 — ~13 context derivation subsections.
Target: one fixture per neighbor-state combination per element.

### Priority 4 — Apply scaling lists when present
Currently silent. Real-world High-profile streams use non-flat scaling
lists. Impact: silent quality degradation for such streams.

### Priority 5 — Implement I_PCM CABAC
Currently `return true` (silent). I_PCM is rare but legal per spec.
Any stream containing I_PCM produces garbage pixels.

### Priority 6 — Fill remaining mb_type coverage
P_8x8 sub-partitions 8x4/4x8/4x4 — assert. x264 will not emit these at
default settings, but conforming bitstreams can.

### Priority 7 — Add resolution coverage matrix
`wstress_*` fixtures started today. Extend to:
- All combinations of {CABAC, CAVLC} × {I_4x4, I_8x8, I_16x16} × {16, 24, 32, 40 MBs wide}
- Non-16-aligned heights (360, 480, 720, 1080 cropped)
- Frame-cropping-enabled fixtures (crop params > 0)

---

## Metrics

Track these KPIs over time to detect regression of the pattern:

| Metric | Current | Target |
|--------|---------|--------|
| Open "Active Quality Issues" in known_issues.md | 1 | 0 |
| `assert(false)` in hot paths | 5 | 0 (graceful return) |
| Silent-corruption branches (returns true without work) | 2 | 0 |
| `[UNCHECKED §X.Y]` markers in src/ | ~10 | 0 |
| `[PARTIAL §X.Y]` markers in src/ | ~5 | 0 |
| CABAC context §9.3.3.1.1.x audited in tests | Partial | All 13 subsections |
| Width-stress resolutions in regression suite | 10 (new today) | 24 (full matrix) |

---

## Conclusion

The fix history shows **sustained churn in the same spec subsections**,
indicating our implementation was built incrementally against fixtures
rather than comprehensively against the spec. The I_8x8 availability bug
fixed today is a direct analog to the I_4x4 availability that was already
spec-complete — two similar surfaces with very different completeness
levels.

**Most impactful next action:** Audit all `assert(false)`, silent-corruption
branches, and `[UNCHECKED]` / `[PARTIAL]` markers before further
optimization. Every one of these is a latent bug waiting for the wrong
bitstream. The P0 optimization plan should be paused until these are
either implemented or converted to graceful errors with explicit
documentation.
