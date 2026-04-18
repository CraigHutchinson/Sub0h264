#!/usr/bin/env python3
"""Run the complete Sub0h264 test + benchmark suite and produce a unified report.

Stages (each can be skipped via flags):

  1. Desktop unit tests        ctest --preset default
  2. Desktop bench/perf suite  ctest --preset bench  (BENCH_JSON output)
  3. Desktop shootout          test_apps/shootout (sub0h264 vs libavc)
  4. PSNR validation           sub0h264_trace + ffmpeg over fixtures
  5. ESP32-P4 build/flash      idf.py build flash + serial capture (needs --port)

Outputs a markdown table per stage and an aggregated `docs/run_all_report.md`.
A JSONL line per result is appended to `docs/run_all_history.jsonl`.

Usage:
    python scripts/run_all_suites.py [options]

      --skip-tests       skip stage 1
      --skip-bench       skip stage 2
      --skip-shootout    skip stage 3
      --skip-psnr        skip stage 4
      --esp-port COM5    enable stage 5 with given serial port
      --esp-timeout S    serial-capture timeout (default 300)
      --quick            small fixture subset for stage 4 only
      --outdir DIR       report destination (default: docs)

The report is printed to stdout AND written to <outdir>/run_all_report.md.

This script intentionally calls each child command via the user's normal
invocations (ctest, cmake, idf.py via nomsys.ps1) so the local environment
permission allow-list is satisfied.
"""
from __future__ import annotations

import argparse
import dataclasses
import datetime as _dt
import glob
import json
import os
import re
import shlex
import subprocess
import sys
from pathlib import Path
from typing import Iterable

# Force UTF-8 stdout on Windows so Unicode markers render in CI logs.
if sys.platform == "win32":
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")

ROOT = Path(__file__).resolve().parent.parent
TRACE_BIN = ROOT / "build" / "test_apps" / "trace" / "Release" / "sub0h264_trace.exe"
# Prefer the MinGW build (has libavc); fall back to MSVC unity build (no libavc).
SHOOTOUT_CANDIDATES = [
    ROOT / "build-shootout" / "sub0h264_shootout.exe",
    ROOT / "build-shootout" / "sub0h264_shootout",
    ROOT / "build" / "test_apps" / "shootout" / "Release" / "sub0h264_shootout.exe",
]
SHOOTOUT_BIN = next((p for p in SHOOTOUT_CANDIDATES if p.exists()), SHOOTOUT_CANDIDATES[0])

# ── Result records ──────────────────────────────────────────────────────────

@dataclasses.dataclass
class StageResult:
    name: str
    ok: bool
    summary: str
    rows: list[dict]                   # list of {col: value} for the markdown table
    headers: list[str] | None = None   # column order; defaults to keys(rows[0])
    detail: str = ""                   # raw log excerpt on failure


# ── Helpers ─────────────────────────────────────────────────────────────────

def run(cmd: list[str], *, cwd: Path | None = None, timeout: int | None = None,
        env_extra: dict | None = None) -> subprocess.CompletedProcess:
    env = os.environ.copy()
    if env_extra:
        env.update(env_extra)
    return subprocess.run(
        cmd, cwd=cwd or ROOT, env=env, capture_output=True,
        text=True, timeout=timeout, check=False
    )

def md_table(rows: list[dict], headers: list[str] | None = None) -> str:
    if not rows:
        return "_(no rows)_"
    headers = headers or list(rows[0].keys())
    lines = ["| " + " | ".join(headers) + " |",
             "|" + "|".join("---" for _ in headers) + "|"]
    for r in rows:
        lines.append("| " + " | ".join(str(r.get(h, "")) for h in headers) + " |")
    return "\n".join(lines)


# ── Stage 1: ctest --preset default ─────────────────────────────────────────

def stage_ctest_default() -> StageResult:
    p = run(["ctest", "--preset", "default", "--output-on-failure"])
    rows: list[dict] = []
    # ctest line: "  1/2 Test #1: Sub0h264_Tests ............   Passed   10.05 sec"
    pat = re.compile(r"^\s*\d+/\d+\s+Test\s+#\d+:\s+(\S+)\s+\.+\s+"
                     r"(Passed|Failed|\*\*\*Failed)\s+(\d+\.\d+)\s+sec", re.M)
    for m in pat.finditer(p.stdout):
        rows.append({"Test": m.group(1), "Result": "Passed" if "Passed" in m.group(2) else "Failed",
                     "Time (s)": m.group(3)})
    summary_match = re.search(r"(\d+%) tests passed, (\d+) tests failed", p.stdout)
    summary = summary_match.group(0) if summary_match else f"exit={p.returncode}"
    return StageResult("Unit tests (ctest --preset default)",
                       ok=(p.returncode == 0),
                       summary=summary, rows=rows,
                       headers=["Test", "Result", "Time (s)"],
                       detail=p.stdout[-2000:] if p.returncode else "")


# ── Stage 2: ctest --preset bench ───────────────────────────────────────────

def stage_ctest_bench() -> StageResult:
    p = run(["ctest", "--preset", "bench", "--verbose"])
    rows: list[dict] = []
    # BENCH_JSON {...} can appear on either stream depending on how ctest pipes
    # the test executable's output. Schema: {id, frames, median_ms, fps, version}.
    for m in re.finditer(r"BENCH_JSON\s*(\{.*?\})", p.stdout + "\n" + p.stderr):
        try:
            obj = json.loads(m.group(1))
            rows.append({
                "Bench": obj.get("id", obj.get("stream", "?")),
                "FPS": f"{obj.get('fps', 0):.1f}",
                "Median (ms)": f"{obj.get('median_ms', 0):.1f}",
                "Frames": obj.get("frames", "?"),
            })
        except Exception:
            pass
    return StageResult("Bench suite (ctest --preset bench)",
                       ok=(p.returncode == 0),
                       summary=f"{len(rows)} bench results",
                       rows=rows,
                       headers=["Bench", "FPS", "Median (ms)", "Frames"],
                       detail=p.stdout[-2000:] if p.returncode else "")


# ── Stage 3: desktop shootout (sub0h264 vs libavc) ──────────────────────────

def stage_shootout() -> StageResult:
    if not SHOOTOUT_BIN.exists():
        return StageResult("Desktop shootout", ok=False,
                           summary=f"binary not found: {SHOOTOUT_BIN}", rows=[])
    p = run([str(SHOOTOUT_BIN.resolve()), "--fixtures-dir",
             str((ROOT / "tests/fixtures").resolve())])
    if p.returncode and not p.stdout:
        return StageResult("Desktop shootout", ok=False,
                           summary=f"exit {p.returncode}: {p.stderr[:200]}", rows=[])
    by_stream: dict[str, dict] = {}
    # SHOOTOUT_JSON markers are emitted on stderr.
    for m in re.finditer(r"SHOOTOUT_JSON\s*(\{.*?\})", p.stdout + "\n" + p.stderr):
        try:
            obj = json.loads(m.group(1))
            stream = obj.get("stream", "?")
            decoder = obj.get("decoder", "?")
            by_stream.setdefault(stream, {"Stream": stream, "Frames": obj.get("frames", "?")})
            by_stream[stream][decoder] = f"{obj.get('fps', 0):.1f}"
        except Exception:
            pass
    rows = list(by_stream.values())
    for r in rows:
        s = r.get("sub0h264")
        l = r.get("libavc")
        if s and l:
            try:
                r["Ratio"] = f"{float(s) / float(l):.2f}x"
            except Exception:
                r["Ratio"] = ""
        else:
            r["Ratio"] = ""
    headers = ["Stream", "Frames", "sub0h264", "libavc", "Ratio"]
    return StageResult("Desktop shootout (sub0h264 vs libavc)",
                       ok=(p.returncode == 0),
                       summary=f"{len(rows)} streams",
                       rows=rows, headers=headers,
                       detail=p.stdout[-2000:] if p.returncode else "")


# ── Stage 4: PSNR validation vs ffmpeg (ground truth) ───────────────────────

# (name, h264 path, decoded width, decoded height, ref height) — ref height
# differs when ffmpeg honours frame_cropping (Tapo: 368 decoded, 360 ref).
PSNR_FIXTURES = [
    ("Tapo C110",                "tests/fixtures/tapo_c110_stream2_high.h264",                 640, 368, 360),
    ("wstress baseline",         "tests/fixtures/wstress_tapo_size_baseline_640x368.h264",     640, 368, 368),
    ("wstress complex_flat",     "tests/fixtures/wstress_tapo_size_complex_flat_640x368.h264", 640, 368, 368),
    ("wstress gradient",         "tests/fixtures/wstress_tapo_size_gradient_640x368.h264",     640, 368, 368),
    ("wstress i16x16",           "tests/fixtures/wstress_tapo_size_i16x16_640x368.h264",       640, 368, 368),
    ("wstress i4x4",             "tests/fixtures/wstress_tapo_size_i4x4_640x368.h264",         640, 368, 368),
    ("wstress i8x8",             "tests/fixtures/wstress_tapo_size_i8x8_640x368.h264",         640, 368, 368),
]
QUICK_PSNR_FIXTURES = PSNR_FIXTURES[:2]

def stage_psnr(quick: bool) -> StageResult:
    if not TRACE_BIN.exists():
        return StageResult("PSNR validation", ok=False,
                           summary=f"trace binary missing: {TRACE_BIN.relative_to(ROOT)}",
                           rows=[])
    fixtures = QUICK_PSNR_FIXTURES if quick else PSNR_FIXTURES
    rows: list[dict] = []
    overall_ok = True
    for name, h264, w, h, hr in fixtures:
        h264p = ROOT / h264
        if not h264p.exists():
            rows.append({"Fixture": name, "Avg PSNR (dB)": "—",
                         "Min PSNR (dB)": "—", "Frames": "missing"})
            continue
        out = ROOT / f"tests/fixtures/{Path(h264).stem}_run_all_sub0.yuv"
        ref = ROOT / f"tests/fixtures/{Path(h264).stem}_run_all_ref.yuv"
        run([str(TRACE_BIN), str(h264p), "--dump", str(out)])
        ff = run(["ffmpeg", "-y", "-i", str(h264p), "-pix_fmt", "yuv420p", str(ref)])
        if not out.exists() or not ref.exists():
            rows.append({"Fixture": name, "Avg PSNR (dB)": "—",
                         "Min PSNR (dB)": "—", "Frames": "decode failed"})
            overall_ok = False
            continue
        avg, mn, n = _psnr(out, ref, w, h, hr)
        rows.append({
            "Fixture": name,
            "Frames": n,
            "Avg PSNR (dB)": f"{avg:.2f}",
            "Min PSNR (dB)": f"{mn:.2f}",
            "Status": "✓ bit-exact" if mn >= 95 else ("✓" if mn >= 30 else "FAIL"),
        })
    headers = ["Fixture", "Frames", "Avg PSNR (dB)", "Min PSNR (dB)", "Status"]
    return StageResult("PSNR validation vs ffmpeg",
                       ok=overall_ok,
                       summary=f"{len(rows)} fixtures",
                       rows=rows, headers=headers)


def _psnr(our_path: Path, ref_path: Path, w: int, h: int, h_ref: int) -> tuple[float, float, int]:
    """PSNR over the visible Y plane, accounting for ours and ref having
    potentially different heights (ffmpeg honours frame_cropping)."""
    import numpy as np
    our = np.fromfile(our_path, dtype=np.uint8)
    ref = np.fromfile(ref_path, dtype=np.uint8)
    fs_our = w * h + 2 * (w // 2) * (h // 2)
    fs_ref = w * h_ref + 2 * (w // 2) * (h_ref // 2)
    n = min(len(our) // fs_our, len(ref) // fs_ref)
    h_compare = min(h, h_ref)
    psnrs = []
    for fr in range(n):
        oy = our[fr * fs_our:fr * fs_our + w * h_compare].reshape(h_compare, w).astype(np.float64)
        ry = ref[fr * fs_ref:fr * fs_ref + w * h_compare].reshape(h_compare, w).astype(np.float64)
        mse = np.mean((oy - ry) ** 2)
        psnrs.append(99.0 if mse < 1e-10 else 10.0 * float(np.log10(255.0 ** 2 / mse)))
    if not psnrs:
        return 0.0, 0.0, 0
    return float(sum(psnrs) / len(psnrs)), float(min(psnrs)), n


# ── Stage 5: ESP32-P4 ──────────────────────────────────────────────────────

def stage_esp(port: str, timeout: int) -> StageResult:
    nomsys = ROOT / "scripts" / "nomsys.ps1"
    cwd = ROOT / "test_apps" / "esp_shootout"
    if not nomsys.exists():
        return StageResult("ESP32-P4 shootout", ok=False,
                           summary="scripts/nomsys.ps1 missing", rows=[])

    # Configure (if needed) + build + flash.
    rows: list[dict] = []
    cfg = run(["pwsh", "-NoProfile", "-File", str(nomsys),
               "cmake", "--preset", "esp32p4"], cwd=cwd, timeout=timeout)
    if cfg.returncode:
        return StageResult("ESP32-P4 shootout", ok=False,
                           summary=f"configure failed: exit {cfg.returncode}",
                           rows=[], detail=cfg.stderr[-2000:])
    bld = run(["pwsh", "-NoProfile", "-File", str(nomsys),
               "cmake", "--build", "--preset", "esp32p4"], cwd=cwd, timeout=timeout)
    if bld.returncode:
        return StageResult("ESP32-P4 shootout", ok=False,
                           summary=f"build failed: exit {bld.returncode}",
                           rows=[], detail=bld.stderr[-2000:])
    flash = run(["pwsh", "-NoProfile", "-File", str(nomsys),
                 "cmake", "--build", "--preset", "esp32p4", "--target", "flash"],
                cwd=cwd, timeout=timeout, env_extra={"ESPPORT": port})
    if flash.returncode:
        return StageResult("ESP32-P4 shootout", ok=False,
                           summary=f"flash failed on {port}: exit {flash.returncode}",
                           rows=[], detail=flash.stderr[-2000:])

    # Capture results via the existing helper.
    cap = cwd / "cmake" / "esp32p4_serial_capture.py"
    if cap.exists():
        log = cwd / "shootout.log"
        run(["python", str(cap), "--port", port, "--test-timeout", str(timeout),
             "--log-file", str(log), "--success-pattern", "Shootout complete"],
            cwd=cwd, timeout=timeout + 60)
        text = log.read_text(errors="replace") if log.exists() else ""
        for m in re.finditer(r"SHOOTOUT_JSON\s*(\{.*?\})", text):
            try:
                obj = json.loads(m.group(1))
                rows.append({
                    "Stream": obj.get("stream", "?"),
                    "Decoder": obj.get("decoder", "?"),
                    "FPS": f"{obj.get('fps', 0):.2f}",
                    "Frames": obj.get("frames", "?"),
                })
            except Exception:
                pass
    return StageResult("ESP32-P4 shootout (on-device)",
                       ok=bool(rows), summary=f"{len(rows)} results",
                       rows=rows, headers=["Stream", "Decoder", "FPS", "Frames"])


# ── History / delta / trend ─────────────────────────────────────────────────

# Per-stage: (key column used to identify a row, list of numeric columns to track).
TRACKED_METRICS: dict[str, tuple[str, list[str]]] = {
    "Unit tests (ctest --preset default)":     ("Test",    ["Time (s)"]),
    "Bench suite (ctest --preset bench)":      ("Bench",   ["FPS", "Median (ms)"]),
    "Desktop shootout (sub0h264 vs libavc)":   ("Stream",  ["sub0h264", "libavc", "Ratio"]),
    "PSNR validation vs ffmpeg":               ("Fixture", ["Avg PSNR (dB)", "Min PSNR (dB)"]),
    "ESP32-P4 shootout (on-device)":           ("Stream",  ["FPS"]),
}

# Headline KPIs shown in the top "Performance Summary" panel — one per stage.
# Row-key values must match the actual key column emitted by the stage runner.
HEADLINE_KPIS: list[tuple[str, str, str, str]] = [
    # (stage name, row key value, metric column, label)
    ("Bench suite (ctest --preset bench)",   "Tapo C110 stream2",      "FPS",          "Tapo bench fps"),
    ("Bench suite (ctest --preset bench)",   "Ball High 640x480",      "FPS",          "Ball-High fps (P0 crux)"),
    ("Desktop shootout (sub0h264 vs libavc)","Tapo-C110",              "Ratio",        "Desktop sub0/libavc Tapo ratio"),
    ("Desktop shootout (sub0h264 vs libavc)","Ball-High-640",          "Ratio",        "Desktop sub0/libavc Ball-High ratio"),
    ("PSNR validation vs ffmpeg",            "Tapo C110",              "Min PSNR (dB)","Tapo min PSNR"),
    ("PSNR validation vs ffmpeg",            "wstress baseline",       "Min PSNR (dB)","wstress baseline min PSNR"),
    ("ESP32-P4 shootout (on-device)",        "Tapo-C110",              "FPS",          "ESP32 Tapo fps"),
]

TREND_WINDOW = 8  # how many prior data points the sparkline uses


def _load_history(path: Path) -> dict[tuple[str, str, str], list[tuple[str, float]]]:
    """Return {(stage, key, metric): [(ts, value), ...]} sorted oldest→newest."""
    out: dict[tuple[str, str, str], list[tuple[str, float]]] = {}
    if not path.exists():
        return out
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        line = line.strip()
        if not line:
            continue
        try:
            obj = json.loads(line)
        except Exception:
            continue
        stage = obj.get("stage", "")
        if stage not in TRACKED_METRICS:
            continue
        key_col, metrics = TRACKED_METRICS[stage]
        key = str(obj.get(key_col, ""))
        if not key:
            continue
        ts = obj.get("ts", "")
        for m in metrics:
            v = _coerce(obj.get(m))
            if v is None:
                continue
            out.setdefault((stage, key, m), []).append((ts, v))
    for v in out.values():
        v.sort(key=lambda t: t[0])
    return out


def _coerce(v) -> float | None:
    """Pull a float out of strings like '12.3', '12.3x', '1.05x', '—'."""
    if v is None:
        return None
    if isinstance(v, (int, float)):
        return float(v)
    s = str(v).strip().rstrip("x").rstrip("X")
    if not s or s in {"—", "?", ""}:
        return None
    try:
        return float(s)
    except ValueError:
        return None


def _fmt_delta(curr: float | None, prev: float | None, *,
               higher_is_better: bool = True, unit: str = "") -> str:
    """Format a numeric delta. Arrow shows direction of change (▲=up, ▼=down).
    A trailing ✓/✗ marks whether the change is good or bad given the metric's
    polarity. '·' means no change; '—' means missing data."""
    if curr is None or prev is None:
        return "—"
    diff = curr - prev
    if abs(diff) < 1e-9:
        return "·"
    arrow = "▲" if diff > 0 else "▼"
    is_good = (diff > 0) if higher_is_better else (diff < 0)
    mark = " ✓" if is_good else " ✗"
    sign = "+" if diff > 0 else ""
    return f"{arrow}{sign}{diff:.2f}{unit}{mark}"


def _sparkline(values: list[float]) -> str:
    if len(values) < 2:
        return ""
    bars = "▁▂▃▄▅▆▇█"
    lo, hi = min(values), max(values)
    if hi - lo < 1e-9:
        return bars[0] * len(values)
    return "".join(bars[min(7, int((v - lo) / (hi - lo) * 7))] for v in values)


# Metrics where smaller is better (latency, time).
_LOWER_IS_BETTER = {"Time (s)", "Median (ms)"}


def _enrich_stage(stage: StageResult,
                  prior: dict[tuple[str, str, str], list[tuple[str, float]]]) -> StageResult:
    """Insert Δ and Trend columns next to each tracked metric."""
    if stage.name not in TRACKED_METRICS or not stage.rows:
        return stage
    key_col, metrics = TRACKED_METRICS[stage.name]
    new_headers: list[str] = []
    for h in stage.headers or list(stage.rows[0].keys()):
        new_headers.append(h)
        if h in metrics:
            new_headers.append(f"Δ {h}")
            new_headers.append(f"Trend ({h})")

    new_rows: list[dict] = []
    for r in stage.rows:
        row = dict(r)
        key = str(r.get(key_col, ""))
        for m in metrics:
            curr = _coerce(r.get(m))
            history = prior.get((stage.name, key, m), [])
            prev = history[-1][1] if history else None
            row[f"Δ {m}"] = _fmt_delta(
                curr, prev, higher_is_better=(m not in _LOWER_IS_BETTER)
            )
            recent = [v for _, v in history[-TREND_WINDOW:]]
            if curr is not None:
                recent.append(curr)
            row[f"Trend ({m})"] = _sparkline(recent) or "—"
        new_rows.append(row)
    return dataclasses.replace(stage, rows=new_rows, headers=new_headers)


def _headline_summary(stages: list[StageResult],
                      prior: dict[tuple[str, str, str], list[tuple[str, float]]]) -> str:
    by_stage = {s.name: s for s in stages}
    rows: list[dict] = []
    for stage_name, key, metric, label in HEADLINE_KPIS:
        s = by_stage.get(stage_name)
        if s is None:
            continue
        key_col, _ = TRACKED_METRICS[stage_name]
        cur_val = None
        for r in s.rows:
            if str(r.get(key_col, "")) == key:
                cur_val = _coerce(r.get(metric))
                break
        history = prior.get((stage_name, key, metric), [])
        prev = history[-1][1] if history else None
        recent = [v for _, v in history[-TREND_WINDOW:]]
        if cur_val is not None:
            recent.append(cur_val)
        rows.append({
            "KPI": label,
            "Current": "—" if cur_val is None else f"{cur_val:.2f}",
            "Previous": "—" if prev is None else f"{prev:.2f}",
            "Δ": _fmt_delta(cur_val, prev,
                            higher_is_better=(metric not in _LOWER_IS_BETTER)),
            f"Trend (last {len(recent)})": _sparkline(recent) or "—",
        })
    if not rows:
        return ""
    return md_table(rows, headers=list(rows[0].keys()))


def _build_enriched_report(stages: list[StageResult],
                           prior: dict[tuple[str, str, str], list[tuple[str, float]]],
                           now: str) -> str:
    enriched_stages = [_enrich_stage(s, prior) for s in stages]
    lines = [f"# Sub0h264 — Full Suite Report",
             f"_{now}_", ""]

    summary = _headline_summary(enriched_stages, prior)
    if summary:
        lines += ["## Performance Summary",
                  f"_Δ vs previous run · trend over last {TREND_WINDOW + 1} points_",
                  "", summary, ""]

    for s in enriched_stages:
        marker = "✓" if s.ok else "✗"
        lines += [f"## {marker} {s.name}", f"_{s.summary}_", "",
                  md_table(s.rows, s.headers), ""]
        if s.detail and not s.ok:
            lines += ["<details><summary>Failure log (tail)</summary>\n", "```",
                      s.detail, "```", "</details>\n"]
    return "\n".join(lines)


# ── Driver ──────────────────────────────────────────────────────────────────

def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--skip-tests", action="store_true")
    ap.add_argument("--skip-bench", action="store_true")
    ap.add_argument("--skip-shootout", action="store_true")
    ap.add_argument("--skip-psnr", action="store_true")
    ap.add_argument("--esp-port", default=None)
    ap.add_argument("--esp-timeout", type=int, default=300)
    ap.add_argument("--quick", action="store_true")
    ap.add_argument("--outdir", default="docs")
    args = ap.parse_args()

    stages: list[StageResult] = []
    if not args.skip_tests:
        stages.append(stage_ctest_default())
    if not args.skip_bench:
        stages.append(stage_ctest_bench())
    if not args.skip_shootout:
        stages.append(stage_shootout())
    if not args.skip_psnr:
        stages.append(stage_psnr(args.quick))
    if args.esp_port:
        stages.append(stage_esp(args.esp_port, args.esp_timeout))

    now = _dt.datetime.now(tz=_dt.timezone.utc).strftime("%Y-%m-%d %H:%M:%SZ")
    outdir = ROOT / args.outdir
    outdir.mkdir(parents=True, exist_ok=True)

    # Load prior history BEFORE appending current run, so deltas compare
    # against the previous run rather than this one.
    history = outdir / "run_all_history.jsonl"
    prior = _load_history(history)
    with history.open("a", encoding="utf-8") as fp:
        for s in stages:
            for r in s.rows:
                fp.write(json.dumps({
                    "ts": now, "stage": s.name, "ok": s.ok, **r,
                }) + "\n")

    report = _build_enriched_report(stages, prior, now)
    print(report)
    (outdir / "run_all_report.md").write_text(report, encoding="utf-8")

    return 0 if all(s.ok for s in stages) else 1


if __name__ == "__main__":
    sys.exit(main())
