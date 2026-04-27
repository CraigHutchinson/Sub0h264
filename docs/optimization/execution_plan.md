# Performance Execution Plan

How we drive Phase 2 (performance) work end-to-end: per-phase workflow, KPI
gates, ESP32 cadence, rollback policy, and execution ordering. Pairs with
[esp32p4_hierarchical_plan.md](esp32p4_hierarchical_plan.md) (the roadmap)
and the per-opportunity briefs under [opportunities/](opportunities/).

## Status (2026-04-19, baseline commit `069f264` / `4c0a3e1`)

| KPI | Baseline | Target | Gap |
|---|---|---|---|
| **ESP32 Ball-High-640 fps (P0 crux)** | **12.0** | **25.0** | **+13.0** |
| ESP32 Ball-Base-640 fps | 12.1 | 25.0 | +12.9 |
| ESP32 Scroll-Base-640 fps | 24.9 | 25.0 | +0.1 |
| ESP32 Scroll-High-640 fps | 24.7 | 25.0 | +0.3 |
| ESP32 Tapo-C110 fps | 30.2 | ≥25 | ✓ |
| ESP32 Still-Base/High, Flat, CAVLC-320, CABAC-320 | 32.8–44.8 | ≥25 | ✓ |
| ESP32 sub0/libavc median ratio | 1.40× | ≥1.0× | ✓ |
| Bit-exact PSNR vs JM (Tapo, all wstress, p8x8_sub) | 99.0 dB | <0.01 dB regression | ✓ |
| Desktop ctest --preset default | 351 pass | 100% | ✓ |

**Definition of done:** Ball-Base AND Ball-High ≥ 25.0 fps on ESP32-P4
across 3 consecutive runs (variance <0.5 fps), with all other gates
maintained.

## KPI gates (every phase commit must satisfy)

These are **hard gates** for landing on `main`. A commit that fails any of
them does not land — but it is **not lost**. We park the attempt to a
branch (see [Park-to-branch policy](#park-to-branch-policy-not-revert)) so
we can revisit it after the first pass through the plan. Some optimisations
that look slower in isolation unlock bigger wins later.

| Gate | Threshold | Source |
|---|---|---|
| **G-PSNR**: Tapo bit-exact | ≥99.0 dB Y-PSNR vs JM (all 119 frames) | `python scripts/run_jm.py psnr <fixture>` |
| **G-PSNR-W**: wstress fixtures bit-exact | ≥99.0 dB on baseline / i4x4 / i8x8 / i16x16 / gradient / complex_flat | `run_all_suites --quick` PSNR stage |
| **G-PSNR-P**: P_8x8 sub-partitions bit-exact | ≥99.0 dB on wstress_p8x8_sub_640x368 | dedicated check |
| **G-DESKTOP**: ctest --preset default | 100% pass | unit tests |
| **G-PERF-NORM**: no normal-stream regression | every ESP32 stream not on the crux must not drop > 0.5 fps vs prior | run_all_suites ESP delta |
| **G-PERF-CRUX**: Ball-High ≥ prev Ball-High | the optimisation target must not regress | run_all_suites ESP delta |

Variance-aware: ESP32 numbers are jitter-prone. We require **3 consecutive
runs** to confirm a fps delta. The history-aware run_all_report shows a
sparkline trend that smooths out single-run noise.

## Per-phase workflow

There are two commit cadences inside a phase:

- **Iteration commits** (within a phase, while implementing): desktop full
  coverage every commit; ESP single-run gating only on "major" commits
  (the ones that actually change perf-relevant code, not tooling tweaks
  or doc updates). This keeps iteration fast while still catching
  ESP regressions early.
- **Phase-completion commit**: full ESP coverage with 3-run variance
  before declaring the phase done. This is the authoritative result that
  goes into `performance_log.md`.

Pseudo-code for one phase (one opportunity):

```
1. Read the brief: docs/optimization/opportunities/<L*>.md
2. Capture baseline (only at phase start):
   - python scripts/run_all_suites.py --esp-port COM5    (3-run variance)
   - Record commit + timestamp; this is the "before" row in history

3. ITERATION LOOP (one or more commits within the phase):
   a. Implement per the brief (one logical change per commit)
   b. Desktop verification (every commit):
      - cmake --build --preset default
      - ctest --preset default                            # G-DESKTOP
      - python scripts/run_all_suites.py --quick          # PSNR + bench
   c. ESP verification on "major" commits only:
      - python scripts/run_all_suites.py --esp-port COM5  (single run)
      - Inspect Δ table: any hard-gate fail → park-to-branch
      (Tooling tweaks, comments, docs commits don't trigger ESP runs.)
   d. Commit:
      - Subject: "perf(L<x.y>): <summary>  +<Y>.<Z> fps Ball-High (desktop)"
      - Body cites brief ID + before/after KPIs

4. PHASE COMPLETION (once the brief's expected Δ is met):
   - python scripts/run_all_suites.py --esp-port COM5 --esp-runs 3
   - Median Ball-High fps must satisfy G-PERF-CRUX with stddev < 0.5 fps
   - If variance high (> 0.5 fps): investigate (thermal, USB power) and
     re-run; do not commit until the variance is bounded
   - Append a row to docs/performance_log.md
   - Mark the brief status: implemented (with measured Δ + commit links)
   - Push to main
```

If a gate fails inside the iteration loop: see
[Park-to-branch policy](#park-to-branch-policy-not-revert) below.

## Execution ordering

Matches the plan's compounding-impact ordering. Brackets show the
opportunity ID + expected Ball-High Δ from the master plan:

| # | Opportunity | Expected Δ | Cumulative target | Gate |
|---|-------------|-----------|--------------------|------|
| 1 | [L3.1 reference prefetch](opportunities/L3.1_reference_prefetch.md) | +2.0 | 14.0 | borderline streams stay flat |
| 2 | [L2.1 diagonal MC 2-pass](opportunities/L2.1_diagonal_mc_2pass.md) | +3.5 | 17.5 | hardest validation: bit-exact across 16 fractional positions |
| 3 | [L4.5 getSample elimination](opportunities/L4.5_getsample_elimination.md) | +1.5 | 19.0 | compounds with L3.1 |
| 4 | [L1.1 row-based deblock](opportunities/L1.1_row_based_deblock.md) | +2.0 | 21.0 | watch deblock-heavy fixtures (Scroll) |
| 5 | [L4.1 branchless clipU8](opportunities/L4.1_branchless_clipu8.md) + [L4.2 inline rows](opportunities/L4.2_deblock_inline_rows.md) | +1.0 | 22.0 | small commits, easy to bisect |
| 6 | [L3.2 frame alignment](opportunities/L3.2_frame_alignment.md) | +1.0 | 23.0 | compound win |
| 7 | [L3.3 direct-frame MC](opportunities/L3.3_direct_frame_mc.md) + [L2.2 zero-MV chroma](opportunities/L2.2_skip_zero_mv_fast_path.md) | +0.5 | 23.5 | benefits Still + Tapo most |
| 8 | [L2.3 BS cache](opportunities/L2.3_deblock_bs_cache.md) + [L2.4 horizontal transpose](opportunities/L2.4_horizontal_edge_transpose.md) | +1.0 | 24.5 | deblock-bound content |
| 9 | [L5.1 compiler hints](opportunities/L5.1_compiler_hints.md) | +0.5 | **25.0** | gate met |
| 10 | [L4.3 CAVLC LUT](opportunities/L4.3_cavlc_coeff_token_lut.md) + [L4.4 IDCT unroll](opportunities/L4.4_idct_4x4_unroll.md) | +0.5 | 25.5 | safety margin |
| 11 | [L1.3 static arrays](opportunities/L1.3_static_context_arrays.md) + [L5.2 BitReader refill](opportunities/L5.2_bitreader_refill.md) | +0.5 | 26.0 | safety margin |
| — | [L1.2 eliminate currentFrame_](opportunities/L1.2_eliminate_currentframe.md) | +1–2% | — | opportunistic refactor; insert when convenient |
| — | [L3.4 mb context packing](opportunities/L3.4_mb_context_packing.md) | +2–3% | — | after L1.3 (static arrays) lands |
| — | [L5.3 PIE/SIMD catalogue](opportunities/L5.3_pie_simd_catalog.md) | doc only | — | post-Phase 2 if 25 fps not yet achieved |

**Re-prioritise after each phase.** Estimates are estimates; if measured Δ
diverges materially, re-rank using the measured numbers and update this
table.

**After the first full pass:** Phase 2.5 review (see
[Park-to-branch policy](#park-to-branch-policy-not-revert)) re-evaluates
parked attempts against the new main baseline. Each parked attempt
either gets revived + merged or formally retired. The reordering is
holistic — we look at the full set of parked + landed attempts and may
re-pair optimisations that didn't compound the way we expected.

## ESP32 cadence

ESP32 measurement requires manual flash + ~6 minutes of serial capture.
Cadence:

- **Every commit (desktop, full coverage):**
  `python scripts/run_all_suites.py --quick` — runs ctest, bench, shootout,
  and quick PSNR. PSNR regressions + desktop perf shifts caught here.
  Appends to `docs/run_all_history.jsonl`.
- **Major commits (ESP gating, single run):** any commit that touches
  perf-relevant code (decoder, MC, deblock, transform, intra/inter
  prediction). One ESP shootout to make sure nothing crashes or tanks.
  Tooling, doc, and test-only commits skip this.
- **Phase boundaries (full coverage, 3-run variance):**
  `python scripts/run_all_suites.py --esp-port COM5 --esp-runs 3`. Median
  + stddev across runs. Variance < 0.5 fps required before declaring the
  phase complete. This is the authoritative phase-result number that
  lands in `performance_log.md`.

## Park-to-branch policy (not revert)

When a hard gate fails on `main`, we **do not revert and discard**. The
attempt is preserved on a branch so we can revisit it later — some
optimisations that look slower in isolation enable bigger wins downstream
once paired with other changes.

**Procedure:**

1. Push the failing attempt to a parking branch:
   `git push origin HEAD:perf/L<x.y>-attempt-<N>`
   where `<N>` increments per attempt at the same opportunity (so
   `perf/L3.1-attempt-1`, `perf/L3.1-attempt-2`, …).
2. Update the brief's status to `parked` and add a section:
   ```
   ## Parked attempts
   - Attempt 1 (perf/L3.1-attempt-1): -0.4 fps Ball-High, +1.2 fps Scroll.
     Reason for parking: G-PERF-CRUX failed. Hypothesis for revisit: pair
     with L4.5 (getSample elimination) which removes the bounds-check
     overhead this attempt introduced.
   ```
3. Reset `main` to the pre-attempt commit:
   `git reset --hard HEAD~N` (where N = attempt commit count)
   then `git push --force-with-lease origin main` (only if the attempt
   was already pushed; otherwise just keep main clean).
4. Move to the next opportunity in the queue.

**Holistic review after the first pass:** once we've worked through every
opportunity in the queue (whether implemented or parked), schedule a
**Phase 2.5 review**:

- List every parked branch + its measured Δ at the time it was parked
- Re-attempt the parked changes against the new (post-first-pass) main
  baseline — measured Δ may now be different (better or worse) because
  surrounding code has changed
- Combine pairs of parked optimisations to test compounding hypotheses
  the original attempts couldn't validate
- Promote any that now pass the gates; permanently retire any that
  remain regressions even in the new context

**Branch hygiene:** parked branches are kept indefinitely until the
Phase 2.5 review concludes. After review, we either merge them or close
them with a "retired" tag in the brief.

### Rollback (the rare case)

A direct revert (no park branch) is reserved for **PSNR regressions** —
bit-exact correctness is non-negotiable, and a PSNR-failing change has no
plausible "compounding hypothesis" that justifies preservation. If
G-PSNR / G-PSNR-W / G-PSNR-P fails: revert immediately, file the bug,
investigate the root cause before re-attempting.

For all other gate failures: park, don't revert.

## Tooling gaps to fix BEFORE starting execution

Identified during this planning pass; small fixes that make the workflow
real:

1. **`run_all_suites.py` ESP capture path bug** — `stage_esp` looks for
   `cwd / "cmake" / "esp32p4_serial_capture.py"` but the script lives at
   `<repo>/cmake/esp32p4_serial_capture.py`. Fix to use `ROOT / "cmake"`.
2. **ESP stream-key collision** — current row format `{Stream, Decoder,
   FPS}` collapses sub0h264 and libavc rows under the same `Stream` key
   in history. Either split into two stages (sub0h264-only and libavc-
   only) or include `Decoder` in the history key.
3. **Headline KPIs missing ESP Ball-High** — add to `HEADLINE_KPIS`:
   ```python
   ("ESP32-P4 shootout (on-device)", "Ball-High-640", "FPS",
    "ESP32 Ball-High fps (PRIMARY KPI)"),
   ```
   Surface the crux at the very top of the report.
4. **KPI gate file** — `docs/optimization/kpi_gates.json` consumed by the
   report renderer. Each gate has a name, threshold, and a SQL-like query
   over the history rows. Report shows ✓/✗ per gate at the top.
5. **3-run variance helper** — `scripts/run_all_suites.py --esp-runs 3`
   to run the ESP shootout three times in succession and report median +
   stddev per stream. Useful at phase boundaries.

These are listed in priority order; items 1–3 are blocking, 4–5 are
quality-of-life. They sit on the critical path of "the first commit of
performance work" — fix them before opportunity 1 (L3.1) starts.

## Phase 2.5 — Holistic review (after first pass)

Once the queue has been worked through (every opportunity either
implemented or parked), we pause and do a holistic review **before**
deciding whether to declare Phase 2 complete or start a second pass.

**Inputs to the review:**

- Final ESP32 numbers per stream (from the latest `--esp-runs 3`)
- List of landed optimisations + their measured Δ
- List of parked attempts + their Δ at park-time + revisit hypothesis
- `docs/run_all_history.jsonl` trajectory (per-stream sparkline)

**Questions the review must answer:**

1. **Has the 25 fps gate been hit?** If yes, are we above with margin?
   If no, by how much, and which streams?
2. **Did the order match expectations?** For each landed phase, is
   measured Δ within ±50% of the brief's estimate? If a phase landed
   way under estimate, why? If way over, can we generalise?
3. **Re-evaluate parked attempts.** For each parked branch, re-run it
   against the current main:
   - Did the measured Δ change vs park-time? (Other landed changes may
     have removed the original problem.)
   - Does pairing with another change unlock the win? (Test the
     hypothesis written in the brief at park-time.)
4. **Are any new opportunities visible?** Profile the post-Phase-2 build
   on Ball-High; the time-percentage breakdown will look different from
   the pre-Phase-2 baseline. New hot spots may justify new briefs.
5. **PIE/SIMD decision.** If still under 25 fps, [L5.3 PIE/SIMD
   catalogue](opportunities/L5.3_pie_simd_catalog.md) becomes the next
   phase. If at or above 25 fps, PIE is deferred and Phase 2 is
   declared complete.

**Outputs of the review:**

- Updated execution plan (this doc) with measured-vs-estimated Δ table
- Promoted / retired status on each parked brief
- A new ordered queue if a second pass is justified
- Performance trajectory chart (Ball-High fps vs date) annotated with
  each phase's commit
- Decision: **Phase 2 complete**, or **start Phase 2 second pass**, or
  **start Phase 3 (PIE)**

## Reporting cadence

- **Per-commit (CI-style)**: append to `docs/run_all_history.jsonl`,
  regenerate `docs/run_all_report.md`. Commit both alongside the perf
  commit.
- **Per-phase (after ESP pass)**: append a row to
  `docs/performance_log.md` with date, commit, ESP fps per stream, brief
  ID, and notes. This is the human-readable historical record.
- **Per-month (or on demand)**: produce a "trajectory chart" — a
  Markdown plot of Ball-High fps vs date, annotated with each phase's
  commit. Helps spot if we're stalling.

## Confirmation point

This is a planning document. To start execution:

1. **Confirm the ordering** (1–11 above). If any opportunity should jump
   the queue, edit the table.
2. **Approve the gate thresholds** (specifically: 99.0 dB PSNR floor and
   0.5 fps regression cap on non-crux streams).
3. **Approve the park-to-branch policy** — perf-gate failures are pushed
   to `perf/L<x.y>-attempt-<N>` for later review; only PSNR failures
   trigger a direct revert.

Once approved, execution begins by:
1. Implementing the 5 tooling fixes (small commit each, with their own
   ESP-baseline-unchanged validation)
2. Starting opportunity 1 ([L3.1 reference prefetch](opportunities/L3.1_reference_prefetch.md))

A separate doc should be added per phase as it lands — the brief itself
gets updated with the measured Δ + commit link, and `performance_log.md`
gets the row. Parked attempts also update the brief's "Parked attempts"
section.
