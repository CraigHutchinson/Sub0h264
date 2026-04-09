#!/usr/bin/env python3
"""Build docs/reference/itu/h264/{revision}/normalized/page-index.json.

Also regenerates toc.json from raw/toc.html using the corrected line-by-line
parser, fixing title artifacts left by the old regex-based parser.

Extracts the PDF bookmark outline (section label -> page number) and merges
it with the freshly parsed TOC to produce a searchable page index that the
/spec-lookup skill uses to read only the relevant PDF pages.

Prerequisite (one-time install):
    pip install pymupdf

Usage:
    python scripts/extract_spec_page_index.py [--revision YYYYMM]
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from html import unescape
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

# ── TOC line patterns ─────────────────────────────────────────────────────────
# Top-level Annex line: "A  Annex A  Profiles and levels"
_ANNEX_TOP_RE = re.compile(r"^\s*[A-Z]\s+(Annex\s+[A-Z])\s+(.+)")

# Numeric subsection:   "     6.4.11.2 Derivation process ..."
# Annex subsection:     "     A.1 Requirements on ..."
_SECTION_RE = re.compile(
    r"^\s*((?:[A-Z]\.\d+(?:\.\d+){0,4}|\d+(?:\.\d+){0,5}))\s+(.+)"
)

# ── PDF bookmark patterns ─────────────────────────────────────────────────────
# Annex bookmark:    "A  Annex A  Profiles and levels"  -> "Annex A"
_BM_ANNEX_RE = re.compile(r"^[A-Z]\s+(Annex\s+[A-Z])\b")

# Numeric/subsection: "9.3.3.1.3 Derivation..." or "A.1 Requirements..."
_BM_LABEL_RE = re.compile(
    r"^((?:[A-Z]\.\d+(?:\.\d+){0,4}|\d+(?:\.\d+){0,5}))\b"
)


def parse_toc_sections(toc_html: str) -> list[dict[str, str]]:
    """Parse the ITU TOC HTML into clean {label, title} entries.

    The TOC is a <pre> block with one section per line.  Two formats appear:
      - Top-level Annex:  "A  Annex A  Profiles and levels"
      - All others:       "[indent] <label> <title>"
    Parsing line-by-line avoids adjacent labels bleeding into titles (the
    bug present in the old regex-based parser).
    """
    text = re.sub(r"<[^>]+>", "", toc_html)
    found: set[str] = set()
    sections: list[dict[str, str]] = []

    for raw_line in text.splitlines():
        # Try top-level Annex first: "A  Annex A  Profiles and levels"
        m = _ANNEX_TOP_RE.match(raw_line)
        if m:
            label = m.group(1).strip()  # "Annex A"
            title = unescape(m.group(2)).strip(" -\t")
            if label not in found:
                found.add(label)
                sections.append({"label": label, "title": title})
            continue

        # Numeric sections and Annex subsections: "6.4.11.2 ..." / "A.1 ..."
        m = _SECTION_RE.match(raw_line)
        if not m:
            continue
        label = m.group(1).strip()
        title = unescape(m.group(2)).strip(" -\t")
        if label not in found:
            found.add(label)
            sections.append({"label": label, "title": title})

    return sections[:5000]


def extract_page_map(pdf_path: Path) -> dict[str, int]:
    """Return {section_label: first_page_number} from PDF bookmark outline.

    PDF bookmarks use 1-based page numbers matching the Read tool's
    ``pages`` parameter.
    """
    try:
        import fitz  # PyMuPDF
    except ImportError:
        print(
            "error: PyMuPDF not found -- install it with: pip install pymupdf",
            file=sys.stderr,
        )
        sys.exit(1)

    doc = fitz.open(str(pdf_path))
    page_map: dict[str, int] = {}
    for _level, title, page in doc.get_toc():
        raw = title.strip()

        # Try top-level Annex bookmark format first
        m = _BM_ANNEX_RE.match(raw)
        if m:
            label = m.group(1).strip()  # "Annex A"
            if label not in page_map:
                page_map[label] = page
            continue

        # Try numeric / Annex subsection labels
        m = _BM_LABEL_RE.match(raw)
        if m:
            label = m.group(1).rstrip(".")
            if label not in page_map:
                page_map[label] = page

    return page_map


def get_total_pages(pdf_path: Path) -> int | None:
    try:
        import fitz
        return fitz.open(str(pdf_path)).page_count
    except Exception:
        return None


def _label_depth(label: str) -> int:
    """Return hierarchical depth of a section label.

    Examples: "9" -> 1, "9.3" -> 2, "9.3.3.1.3" -> 5,
              "Annex A" -> 1, "A.1" -> 2, "A.2.4" -> 3.
    """
    if label.startswith("Annex "):
        return 1
    return label.count(".") + 1


def build_page_index(
    toc_sections: list[dict[str, str]],
    page_map: dict[str, int],
) -> list[dict]:
    """Annotate toc entries with page and endPage from the PDF map.

    endPage is the last page of the full section *including* all its
    subsections — i.e. one page before the next sibling at the same depth
    or any ancestor.  This lets agents read a complete section without
    stopping at the first sub-heading.
    """
    result: list[dict] = []
    for i, entry in enumerate(toc_sections):
        label = entry["label"]
        page = page_map.get(label)
        if page is None:
            continue

        current_depth = _label_depth(label)

        # endPage: page before the next section at the SAME OR SHALLOWER depth
        # (i.e. the next sibling or parent), so subsections are included.
        end_page = page
        for j in range(i + 1, len(toc_sections)):
            next_label = toc_sections[j]["label"]
            if _label_depth(next_label) <= current_depth:
                nxt = page_map.get(next_label)
                if nxt is not None:
                    end_page = max(page, nxt - 1)
                break

        result.append(
            {
                "label": label,
                "title": entry["title"],
                "page": page,
                "endPage": end_page,
            }
        )
    return result


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Build H.264 spec page index from PDF bookmarks"
    )
    parser.add_argument(
        "--revision",
        default="202408",
        help="Spec revision in YYYYMM format (default: 202408)",
    )
    args = parser.parse_args()

    base_dir = ROOT / "docs" / "reference" / "itu" / "h264" / args.revision
    pdf_path = base_dir / "raw" / "spec.pdf"
    toc_html_path = base_dir / "raw" / "toc.html"
    toc_json_path = base_dir / "normalized" / "toc.json"
    out_path = base_dir / "normalized" / "page-index.json"

    if not pdf_path.exists():
        print(f"error: PDF not found at {pdf_path}", file=sys.stderr)
        print("Run: python scripts/sync_h264_spec.py", file=sys.stderr)
        return 1

    if not toc_html_path.exists():
        print(f"error: toc.html not found at {toc_html_path}", file=sys.stderr)
        return 1

    # Re-parse toc.html with the corrected line-by-line parser and update toc.json.
    print(f"Parsing TOC from {toc_html_path} ...")
    toc_html = toc_html_path.read_text(encoding="utf-8", errors="replace")
    toc_sections = parse_toc_sections(toc_html)
    print(f"  Parsed {len(toc_sections)} sections.")

    existing_revision = args.revision
    if toc_json_path.exists():
        try:
            existing_revision = json.loads(
                toc_json_path.read_text(encoding="utf-8")
            ).get("revision", args.revision)
        except Exception:
            pass

    toc_json_path.write_text(
        json.dumps(
            {"revision": existing_revision, "sections": toc_sections},
            indent=2,
            ensure_ascii=True,
        )
        + "\n",
        encoding="utf-8",
    )
    print(f"  Updated {toc_json_path}")

    # Extract page numbers from PDF bookmarks.
    print(f"Extracting PDF bookmarks from {pdf_path} ...")
    page_map = extract_page_map(pdf_path)
    print(f"  Found {len(page_map)} labelled bookmark entries.")

    page_index = build_page_index(toc_sections, page_map)
    total_pages = get_total_pages(pdf_path)

    output = {
        "revision": args.revision,
        "totalPages": total_pages,
        "generatedFrom": "pdf-bookmarks",
        "sections": page_index,
    }

    out_path.write_text(
        json.dumps(output, indent=2, ensure_ascii=True) + "\n", encoding="utf-8"
    )
    print(f"Wrote {len(page_index)} sections to {out_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
