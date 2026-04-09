#!/usr/bin/env python3
"""Look up a section of the ITU-T H.264 specification by label or keyword.

Extracts text directly from the PDF using PyMuPDF — no model transcription,
no content-filter exposure.

Requires:
    pip install pymupdf
    python scripts/extract_spec_page_index.py   # generates page-index.json

Usage:
    python scripts/lookup_spec.py "9.3.3.1.3"
    python scripts/lookup_spec.py "Table 9-44"
    python scripts/lookup_spec.py "condTermFlag"
    python scripts/lookup_spec.py "Annex B"
    python scripts/lookup_spec.py --max-pages 10 "7.4.1"
    python scripts/lookup_spec.py --revision 202408 "8.5.12.1"
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

# Force UTF-8 output — PDF text contains Unicode characters that cp1252 can't encode.
if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
if hasattr(sys.stderr, "reconfigure"):
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")


def normalize_query(raw: str) -> str:
    """Strip leading § / whitespace / trailing punctuation."""
    return raw.strip().lstrip("\u00a7").strip()


_TABLE_FIG_RE = re.compile(r"^(?:Table|Figure)\s+(\d+)-\d+", re.IGNORECASE)


def find_entry(query: str, sections: list[dict]) -> dict | None:
    """Return the best-matching page-index entry for *query*.

    Priority:
      1. Exact label match          "9.3.3.1.3"
      2. Title keyword match        "rangeTabLPS"
      3. Label prefix match         "9.3" → first section whose label starts with "9.3"
      4. Table/Figure chapter hint  "Table 9-44" → first section in chapter 9
         (caller should then use search_pdf_for_table to find the exact page)
    """
    q = normalize_query(query)
    q_lower = q.lower()

    # 1. Exact label
    for entry in sections:
        if entry["label"] == q:
            return entry

    # 2. Title keyword (case-insensitive)
    for entry in sections:
        if q_lower in entry["title"].lower():
            return entry

    # 3. Label prefix
    prefix = q.rstrip(".")
    for entry in sections:
        if entry["label"].startswith(prefix + ".") or entry["label"] == prefix:
            return entry

    # 4. Table/Figure: use chapter number to find the containing chapter entry
    m = _TABLE_FIG_RE.match(q)
    if m:
        chapter = m.group(1)
        for entry in sections:
            if entry["label"] == chapter:
                return entry

    return None


def search_pdf_for_table(pdf_path: Path, query: str, page_start: int, page_end: int) -> int | None:
    """Return the first PDF page number (1-based) where *query* text appears."""
    try:
        import fitz
    except ImportError:
        return None

    doc = fitz.open(str(pdf_path))
    for idx in range(page_start - 1, min(page_end, doc.page_count)):
        if doc[idx].search_for(query):
            return idx + 1  # return 1-based page number
    return None


def extract_text(pdf_path: Path, page_start: int, page_end: int) -> str:
    """Return plain text extracted from PDF pages (1-based page numbers)."""
    try:
        import fitz  # PyMuPDF
    except ImportError:
        print("error: PyMuPDF not installed — run: pip install pymupdf", file=sys.stderr)
        sys.exit(1)

    doc = fitz.open(str(pdf_path))
    parts: list[str] = []
    for idx in range(page_start - 1, page_end):  # fitz uses 0-based indexing
        if idx >= doc.page_count:
            break
        page = doc[idx]
        # "text" mode preserves reading order; tables may lose column alignment
        # but are still readable and complete.
        text = page.get_text("text").strip()
        parts.append(f"── Page {idx + 1} ──\n{text}")

    return "\n\n".join(parts)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Extract H.264 spec text for a section, table, or keyword"
    )
    parser.add_argument("query", help="Section label, table ref, or keyword")
    parser.add_argument(
        "--revision", default="202408", help="Spec revision YYYYMM (default: 202408)"
    )
    parser.add_argument(
        "--max-pages", type=int, default=5, metavar="N",
        help="Maximum pages to extract (default: 5)"
    )
    args = parser.parse_args()

    base_dir = ROOT / "docs" / "reference" / "itu" / "h264" / args.revision
    index_path = base_dir / "normalized" / "page-index.json"
    pdf_path = base_dir / "raw" / "spec.pdf"

    if not index_path.exists():
        print(f"error: page-index.json not found at {index_path}", file=sys.stderr)
        print("Run: python scripts/extract_spec_page_index.py", file=sys.stderr)
        return 1

    if not pdf_path.exists():
        print(f"error: spec.pdf not found at {pdf_path}", file=sys.stderr)
        return 1

    data = json.loads(index_path.read_text(encoding="utf-8"))
    entry = find_entry(args.query, data["sections"])

    if entry is None:
        print(f"No match for '{args.query}'.", file=sys.stderr)
        print("Try a broader label (e.g. '9.3.3' instead of '9.3.3.1.3') or a title keyword.", file=sys.stderr)
        return 1

    q_norm = normalize_query(args.query)
    page_start = entry["page"]
    page_end_full = entry["endPage"]

    # For Table/Figure refs: search the chapter's page range for the exact text
    # to jump directly to the right page rather than dumping the whole chapter.
    if _TABLE_FIG_RE.match(q_norm) and entry["label"].isdigit():
        found = search_pdf_for_table(pdf_path, q_norm, page_start, page_end_full)
        if found:
            page_start = found
            page_end_full = min(found + 2, entry["endPage"])  # table fits in ~3 pages

    page_end = min(page_end_full, page_start + args.max_pages - 1)
    total_pages = page_end_full - page_start + 1

    # Header
    print(f"ITU-T H.264 (V15, {args.revision}) §{entry['label']} — {entry['title']}")
    print(f"Spec pages {page_start}–{entry['endPage']} ({total_pages} total)", end="")
    if page_end < entry["endPage"]:
        print(f"; showing {page_start}–{page_end} (use --max-pages {total_pages} for full section)", end="")
    print("\n")

    print(extract_text(pdf_path, page_start, page_end))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
