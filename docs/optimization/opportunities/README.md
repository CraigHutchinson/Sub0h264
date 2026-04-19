# Optimisation opportunity briefs

Self-contained, agent-executable briefs for each optimisation on the
[ESP32-P4 hierarchical plan](../esp32p4_hierarchical_plan.md). One doc per
opportunity. Read the doc before writing code.

## Doc template

Each brief follows this shape:

1. **Header** — layer, expected gain, risk, complexity (S/M/L size)
2. **Context** — why this opportunity exists, profile data
3. **Problem** — the specific inefficiency
4. **Options** — 1–3 reasoned alternatives with pros/cons
5. **Chosen approach** — pick + rationale (this is the contract)
6. **Implementation** — files, line ranges, pseudocode, data structures
7. **Validation** — golden-fixture PSNR gates, unit tests, bench targets
8. **Risks & mitigations** — bit-exactness, edge cases
9. **Follow-ups / compounding opportunities** — what this unlocks

## Convention

- Before committing code: if implementation diverges from the doc, **update
  the doc first** so the doc and code stay in sync.
- Commit message cites the plan phase (e.g. `phase 3`) AND the opportunity
  ID (`L3.1`) and references the brief.
- Every brief ends with a validation section that names specific golden
  fixtures (Tapo C110, wstress_*, wstress_p8x8_sub, bench streams) that
  MUST remain ≥ 99 dB PSNR vs JM for the commit to land.

## Index

See [../esp32p4_hierarchical_plan.md](../esp32p4_hierarchical_plan.md) for
the master index with each brief's link, expected fps delta, and
implementation phase number.

## Status legend

- **pending** — no work started
- **spiked** — prototyping underway, doc may be stale
- **implemented** — merged; doc is a historical record
- **deferred** — reviewed, not proceeding (doc explains why)
