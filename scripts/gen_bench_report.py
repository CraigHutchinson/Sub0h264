#!/usr/bin/env python3
"""Generate benchmark report from BENCH_JSON and SHOOTOUT_JSON output.

Parses structured JSON lines from:
  - ctest --preset bench (BENCH_JSON on stderr)
  - sub0h264_shootout    (SHOOTOUT_JSON on stderr)

Produces:
  - docs/benchmark_report.md  — human-readable table
  - docs/benchmark_history.csv — append-only historical record

Usage:
    # Capture bench output:
    ctest --preset bench 2> bench.log
    sub0h264_shootout --fixtures-dir tests/fixtures 2> shootout.log

    # Generate report:
    python scripts/gen_bench_report.py --bench bench.log --shootout shootout.log

    # Or pipe directly:
    ctest --preset bench 2>&1 | python scripts/gen_bench_report.py --bench -

SPDX-License-Identifier: MIT
"""
import argparse
import csv
import json
import os
import re
import sys
from datetime import datetime
from pathlib import Path


def parse_json_lines(text: str, prefix: str) -> list[dict]:
    """Extract JSON objects following a prefix tag."""
    results = []
    pattern = re.compile(rf'{prefix}\s*(\{{.*?\}})')
    for match in pattern.finditer(text):
        try:
            results.append(json.loads(match.group(1)))
        except json.JSONDecodeError:
            pass
    return results


def generate_report(bench_data: list[dict], shootout_data: list[dict],
                    output_dir: str) -> None:
    """Generate markdown report and CSV history entry."""
    os.makedirs(output_dir, exist_ok=True)
    timestamp = datetime.now().strftime("%Y-%m-%d %H:%M")

    # Build bench table
    lines = [
        f"# Sub0h264 Benchmark Report",
        f"",
        f"Generated: {timestamp}",
        f"",
    ]

    if bench_data:
        lines.extend([
            "## Internal Benchmarks (ctest --preset bench)",
            "",
            "| ID | Frames | Median (ms) | FPS | Version |",
            "|---|---|---|---|---|",
        ])
        for entry in bench_data:
            lines.append(
                f"| {entry.get('id', '?')} "
                f"| {entry.get('frames', '?')} "
                f"| {entry.get('median_ms', 0):.1f} "
                f"| {entry.get('fps', 0):.1f} "
                f"| {entry.get('version', '?')} |"
            )
        lines.append("")

    if shootout_data:
        # Group by stream
        streams: dict[str, dict] = {}
        for entry in shootout_data:
            stream = entry.get("stream", "?")
            decoder = entry.get("decoder", "?")
            fps = entry.get("fps", 0.0)
            if stream not in streams:
                streams[stream] = {"frames": entry.get("frames", 0)}
            streams[stream][decoder] = fps

        lines.extend([
            "## Shootout: Sub0h264 vs libavc",
            "",
            "| Stream | Frames | Sub0h264 (fps) | libavc (fps) | Ratio |",
            "|---|---|---|---|---|",
        ])
        for stream, data in streams.items():
            sub0 = data.get("sub0h264", 0.0)
            libavc = data.get("libavc", 0.0)
            ratio = sub0 / libavc if libavc > 0 else 0.0
            lines.append(
                f"| {stream} "
                f"| {data.get('frames', '?')} "
                f"| {sub0:.1f} "
                f"| {libavc:.1f} "
                f"| {ratio:.2f}x |"
            )
        lines.append("")

    report_path = os.path.join(output_dir, "benchmark_report.md")
    with open(report_path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))
    print(f"Report: {report_path}")

    # Append to CSV history
    csv_path = os.path.join(output_dir, "benchmark_history.csv")
    write_header = not os.path.exists(csv_path)

    with open(csv_path, "a", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        if write_header:
            writer.writerow(["timestamp", "source", "id", "frames",
                             "median_ms", "fps", "version"])
        for entry in bench_data:
            writer.writerow([
                timestamp, "bench",
                entry.get("id", ""),
                entry.get("frames", ""),
                entry.get("median_ms", ""),
                entry.get("fps", ""),
                entry.get("version", ""),
            ])
        for entry in shootout_data:
            writer.writerow([
                timestamp, "shootout",
                f"{entry.get('stream', '')}_{entry.get('decoder', '')}",
                entry.get("frames", ""),
                "",
                entry.get("fps", ""),
                "",
            ])
    print(f"History: {csv_path}")


def read_input(path: str) -> str:
    """Read from file or stdin."""
    if path == "-":
        return sys.stdin.read()
    with open(path, encoding="utf-8", errors="replace") as f:
        return f.read()


def main():
    parser = argparse.ArgumentParser(
        description="Generate benchmark report from BENCH_JSON/SHOOTOUT_JSON")
    parser.add_argument("--bench", default=None,
                        help="File with BENCH_JSON lines (- for stdin)")
    parser.add_argument("--shootout", default=None,
                        help="File with SHOOTOUT_JSON lines (- for stdin)")
    parser.add_argument("--outdir", default="docs",
                        help="Output directory for report files")
    args = parser.parse_args()

    bench_data = []
    shootout_data = []

    if args.bench:
        text = read_input(args.bench)
        bench_data = parse_json_lines(text, "BENCH_JSON")
        print(f"Parsed {len(bench_data)} BENCH_JSON entries")

    if args.shootout:
        text = read_input(args.shootout)
        shootout_data = parse_json_lines(text, "SHOOTOUT_JSON")
        print(f"Parsed {len(shootout_data)} SHOOTOUT_JSON entries")

    if not bench_data and not shootout_data:
        print("No data found. Provide --bench and/or --shootout files.")
        sys.exit(1)

    generate_report(bench_data, shootout_data, args.outdir)


if __name__ == "__main__":
    main()
