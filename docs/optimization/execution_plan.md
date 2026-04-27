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

These are **hard gates**. A commit that fails any of them is reverted; the
opportunity brief is updated to explain why and what alternative is being
tried.

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

Pseudo-code for one phase (one opportunity):

```
1. Read the brief: docs/optimization/opportunities/<L*>.md
2. Capture baseline:
   - python scripts/run_all_suites.py --esp-port COM5  (full ESP run)
   - Record commit + timestamp; this is the "before" row in history
3. Implement per the brief
4. Local verification:
   - cmake --build --preset default
   - ctest --preset default                  # G-DESKTOP
   - python scripts/run_jm.py psnr <fixtures> # G-PSNR family
5. ESP verification:
   - python scripts/run_all_suites.py --esp-port COM5
   - Inspect the Δ table: G-PERF-NORM and G-PERF-CRUX must pass
   - If borderline: run twice more (variance check)
6. Commit:
   - Subject: "perf(L<x.y>): <one-line summary>  +<Y>.<Z> fps Ball-High"
   - Body cites: brief ID, before/after KPIs, variance window
7. Push; update opportunity brief status to `implemented` with measured Δ
```

If any gate fails: revert the commit, update the brief with what happened,
move to the next opportunity. We don't accumulate broken state.

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

## ESP32 cadence

ESP32 measurement requires manual flash + ~6 minutes of serial capture.
We do NOT bench ESP32 on every commit. Cadence:

- **Every commit (desktop):** `python scripts/run_all_suites.py --quick`
  — runs ctest, bench, shootout, and quick PSNR. Catches PSNR regressions
  + desktop perf shifts. Append to `docs/run_all_history.jsonl`.
- **End of every phase:** full ESP32 run via
  `python scripts/run_all_suites.py --esp-port COM5`. The result is the
  authoritative phase-result number (desktop is just a leading indicator).
- **3-run variance check:** before declaring a phase complete, run ESP
  three times. If Ball-High variance > 0.5 fps, investigate (thermal,
  background tasks, USB power) before claiming a Δ.

## Rollback policy

A commit gets reverted if any of these is true after measurement:

- **G-PSNR / G-PSNR-W / G-PSNR-P** drops below 99.0 dB on any tracked
  fixture (this is non-negotiable; bit-exact regressions are bugs even
  if PSNR is "high enough" for visual quality)
- **G-DESKTOP** ctest fails
- **G-PERF-CRUX** Ball-High *regresses* by ≥ 0.5 fps with 3-run confirmation
- **G-PERF-NORM** any non-crux stream regresses by > 0.5 fps with 3-run
  confirmation

If a regression is borderline (e.g. a 0.3 fps drop on Scroll), keep the
commit, file a follow-up note in the brief's "Risks" section.

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
3. **Approve the rollback policy** — single-flag-fail = revert; one
   exception is documented (borderline non-crux drops).

Once approved, execution begins by:
1. Implementing the 5 tooling fixes (small commit each, with their own
   ESP-baseline-unchanged validation)
2. Starting opportunity 1 ([L3.1 reference prefetch](opportunities/L3.1_reference_prefetch.md))

A separate doc should be added per phase as it lands — the brief itself
gets updated with the measured Δ + commit link, and `performance_log.md`
gets the row.
