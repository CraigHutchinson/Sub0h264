#!/usr/bin/env python3
"""Enforce H.264 spec source and citation workflow checks."""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

CANONICAL_SOURCE = "https://www.itu.int/rec/T-REC-H.264"
README_LINK_MARKER = "Spec Reference Source"
CLAUDE_LINK_MARKER = "Spec Source and Local Mirror"
LOCAL_MIRROR_MARKER = "docs/reference/itu/h264"

RELEVANT_EXTENSIONS = {".hpp", ".cpp", ".h", ".c"}
RELEVANT_ROOTS = ("components/sub0h264/src/", "tests/")
SPEC_CITATION_MARKERS = ("ITU-T H.264", "§")


def get_changed_files(repo_root: Path, diff_range: str | None) -> list[str]:
    if diff_range:
        cmd = ["git", "diff", "--name-only", diff_range]
    else:
        cmd = ["git", "diff", "--name-only", "HEAD~1", "HEAD"]
    result = subprocess.run(cmd, cwd=repo_root, check=False, capture_output=True, text=True)
    if result.returncode != 0:
        return []
    return [line.strip().replace("\\", "/") for line in result.stdout.splitlines() if line.strip()]


def file_contains(path: Path, marker: str) -> bool:
    return marker in path.read_text(encoding="utf-8", errors="replace")


def check_docs(repo_root: Path) -> list[str]:
    failures: list[str] = []
    readme = repo_root / "README.md"
    claude = repo_root / "CLAUDE.md"

    if not readme.exists() or not claude.exists():
        failures.append("README.md or CLAUDE.md missing")
        return failures

    readme_text = readme.read_text(encoding="utf-8", errors="replace")
    claude_text = claude.read_text(encoding="utf-8", errors="replace")

    if README_LINK_MARKER not in readme_text:
        failures.append("README.md missing 'Spec Reference Source' section")
    if CANONICAL_SOURCE not in readme_text:
        failures.append("README.md missing canonical ITU H.264 source link")

    if CLAUDE_LINK_MARKER not in claude_text:
        failures.append("CLAUDE.md missing 'Spec Source and Local Mirror' section")
    if CANONICAL_SOURCE not in claude_text:
        failures.append("CLAUDE.md missing canonical ITU H.264 source link")
    if LOCAL_MIRROR_MARKER not in claude_text:
        failures.append("CLAUDE.md missing local mirror path docs/reference/itu/h264")

    return failures


def is_relevant_source(path: str) -> bool:
    if not path.endswith(tuple(RELEVANT_EXTENSIONS)):
        return False
    return path.startswith(RELEVANT_ROOTS)


def check_citations(repo_root: Path, changed_files: list[str]) -> list[str]:
    failures: list[str] = []

    for rel in changed_files:
        if not is_relevant_source(rel):
            continue
        file_path = repo_root / rel
        if not file_path.exists():
            continue
        text = file_path.read_text(encoding="utf-8", errors="replace")
        if not any(marker in text for marker in SPEC_CITATION_MARKERS):
            failures.append(f"{rel}: missing ITU-T H.264 citation marker (expected 'ITU-T H.264' or '§')")

    return failures


def main() -> int:
    parser = argparse.ArgumentParser(description="Check H.264 spec workflow compliance")
    parser.add_argument(
        "--diff-range",
        help="Git diff range for changed-file checks (example: origin/develop...HEAD)",
    )
    parser.add_argument(
        "--repo-root",
        default=str(Path(__file__).resolve().parents[1]),
        help="Repository root",
    )
    args = parser.parse_args()

    repo_root = Path(args.repo_root).resolve()

    failures = []
    failures.extend(check_docs(repo_root))

    changed_files = get_changed_files(repo_root, args.diff_range)
    if changed_files:
        failures.extend(check_citations(repo_root, changed_files))

    if failures:
        print("H.264 spec workflow compliance check failed:", file=sys.stderr)
        for item in failures:
            print(f"- {item}", file=sys.stderr)
        return 1

    print("H.264 spec workflow compliance check passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
