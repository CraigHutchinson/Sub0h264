#!/usr/bin/env python3
"""Sync ITU-T H.264 specification artifacts into docs/reference.

This script discovers the latest in-force H.264 revision from the parent
recommendation page unless a revision override is provided.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass
from datetime import datetime, timezone
from html import unescape
from pathlib import Path
from typing import Any
from urllib.parse import urljoin
from urllib.request import urlopen

PARENT_URL = "https://www.itu.int/rec/T-REC-H.264"
TOC_URL_TMPL = "https://www.itu.int/dms_pubrec/itu-t/rec/h/T-REC-H.264-{revision}-S!!TOC-HTM-E.htm"
SUMMARY_URL_TMPL = "https://www.itu.int/dms_pubrec/itu-t/rec/h/T-REC-H.264-{revision}-S!!SUM-HTM-E.htm"
LANDING_URL_TMPL = "https://www.itu.int/rec/T-REC-H.264-{revision}-S"


@dataclass
class FetchResult:
    url: str
    final_url: str
    status: int
    content_type: str
    body: bytes


def fetch_url(url: str) -> FetchResult:
    with urlopen(url) as response:  # nosec B310
        body = response.read()
        return FetchResult(
            url=url,
            final_url=response.geturl(),
            status=getattr(response, "status", 200),
            content_type=response.headers.get("Content-Type", ""),
            body=body,
        )


def detect_latest_revision(parent_html: str) -> str:
    match = re.search(r"H\.264\s*\((\d{2})/(\d{2})\).*?In force", parent_html, flags=re.IGNORECASE | re.DOTALL)
    if not match:
        raise RuntimeError("Could not find in-force H.264 revision in parent page.")
    month, year = match.group(1), match.group(2)
    return f"20{year}{month}"


def parse_in_force_landing_url(parent_html: str, parent_url: str) -> str | None:
    match = re.search(
        r"<tr[^>]*>\s*<td[^>]*>\s*<a\s+href=\"([^\"]*parent=T-REC-H\.264-[^\"]+)\"[^>]*>\s*<strong>\s*H\.264\s*\([^\)]+\)\s*</strong>",
        parent_html,
        flags=re.IGNORECASE | re.DOTALL,
    )
    if not match:
        return None
    return urljoin(parent_url, unescape(match.group(1)))


def extract_revision_and_suffix(url_or_html: str) -> tuple[str, str] | None:
    match = re.search(r"T-REC-H\.264-(20\d{2}(0[1-9]|1[0-2]))-([A-Z])", url_or_html)
    if not match:
        return None
    return match.group(1), match.group(3)


def parse_link_from_landing(landing_html: str, landing_url: str, marker: str) -> str | None:
    pattern = rf'href\s*=\s*"([^"]*{re.escape(marker)}[^"]*)"'
    match = re.search(pattern, landing_html, flags=re.IGNORECASE)
    if match:
        return urljoin(landing_url, unescape(match.group(1)))


def parse_pdf_url(landing_html: str, landing_url: str, revision: str, suffix: str) -> str:
    parsed = parse_link_from_landing(landing_html, landing_url, "PDF-E")
    if parsed:
        return parsed
    return (
        "https://www.itu.int/rec/dologin_pub.asp?lang=e"
        f"&id=T-REC-H.264-{revision}-{suffix}!!PDF-E&type=items"
    )


def parse_toc_sections(toc_html: str) -> list[dict[str, str]]:
    # The TOC is a <pre> block with one entry per line: indentation + label + space + title.
    # Parse line-by-line to avoid adjacent labels bleeding into each other.
    #
    # Two line formats exist:
    #   Top-level Annex:  "A  Annex A  Profiles and levels"
    #   All others:       "[indent] <label> <title>"
    #     where label is e.g. "9.3.3.1.3", "A.1", "A.2.4", or bare "0"
    annex_top_re = re.compile(r"^\s*[A-Z]\s+(Annex\s+[A-Z])\s+(.+)")
    section_re = re.compile(r"^\s*((?:[A-Z]\.\d+(?:\.\d+){0,4}|\d+(?:\.\d+){0,5}))\s+(.+)")
    text = re.sub(r"<[^>]+>", "", toc_html)
    found: set[str] = set()
    sections: list[dict[str, str]] = []
    for raw_line in text.splitlines():
        # Top-level Annex line: "A  Annex A  Profiles and levels"
        m = annex_top_re.match(raw_line)
        if m:
            label = m.group(1).strip()
            title = unescape(m.group(2)).strip(" -\t")
            if label not in found:
                found.add(label)
                sections.append({"label": label, "title": title})
            continue
        # Numeric and Annex subsection lines: "6.4.11.2 ..." / "A.1 ..."
        m = section_re.match(raw_line)
        if not m:
            continue
        label = m.group(1).strip()
        title = unescape(m.group(2)).strip(" -\t")
        if label not in found:
            found.add(label)
            sections.append({"label": label, "title": title})
    return sections[:5000]


def write_bytes(path: Path, content: bytes, force: bool) -> None:
    if path.exists() and not force:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(content)


def write_json(path: Path, payload: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, ensure_ascii=True) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description="Sync ITU-T H.264 reference documents")
    parser.add_argument("--revision", help="Revision override in YYYYMM format, e.g. 202408")
    parser.add_argument("--force", action="store_true", help="Overwrite previously fetched files")
    parser.add_argument("--dry-run", action="store_true", help="Print resolved URLs and exit")
    parser.add_argument(
        "--root",
        default=str(Path(__file__).resolve().parents[1]),
        help="Repository root path",
    )
    args = parser.parse_args()

    root = Path(args.root).resolve()

    parent = fetch_url(PARENT_URL)
    parent_html = parent.body.decode("utf-8", errors="replace")
    revision = args.revision or detect_latest_revision(parent_html)
    suffix = "S"

    discovered_landing = parse_in_force_landing_url(parent_html, parent.final_url)
    if discovered_landing and not args.revision:
        landing_url = discovered_landing
        parsed = extract_revision_and_suffix(discovered_landing)
        if parsed:
            revision, suffix = parsed
    else:
        landing_url = LANDING_URL_TMPL.format(revision=revision)

    if args.revision:
        landing_url = LANDING_URL_TMPL.format(revision=revision)

    if not re.fullmatch(r"20\d{2}(0[1-9]|1[0-2])", revision):
        raise RuntimeError(f"Invalid revision format: {revision}")

    landing = fetch_url(landing_url)
    landing_html = landing.body.decode("utf-8", errors="replace")
    toc_url = parse_link_from_landing(landing_html, landing.final_url, "TOC-HTM")
    summary_url = parse_link_from_landing(landing_html, landing.final_url, "SUM-HTM")
    parsed_from_landing = extract_revision_and_suffix(landing_html)
    if parsed_from_landing and not args.revision:
        revision, suffix = parsed_from_landing

    if not toc_url:
        toc_url = TOC_URL_TMPL.format(revision=revision).replace("-S!!", f"-{suffix}!!")
    if not summary_url:
        summary_url = SUMMARY_URL_TMPL.format(revision=revision).replace("-S!!", f"-{suffix}!!")
    pdf_url = parse_pdf_url(landing_html, landing.final_url, revision, suffix)
    toc = fetch_url(toc_url)
    summary = fetch_url(summary_url)
    pdf = fetch_url(pdf_url)

    base_dir = root / "docs" / "reference" / "itu" / "h264" / revision
    raw_dir = base_dir / "raw"
    normalized_dir = base_dir / "normalized"

    manifest = {
        "spec": "ITU-T H.264",
        "revision": revision,
        "generatedAtUtc": datetime.now(timezone.utc).isoformat(),
        "canonical": {
            "parent": PARENT_URL,
            "landing": landing_url,
            "toc": toc_url,
            "summary": summary_url,
            "pdf": pdf_url,
        },
        "sources": {
            "parent": {
                "url": parent.url,
                "finalUrl": parent.final_url,
                "status": parent.status,
                "contentType": parent.content_type,
            },
            "landing": {
                "url": landing.url,
                "finalUrl": landing.final_url,
                "status": landing.status,
                "contentType": landing.content_type,
            },
            "toc": {
                "url": toc.url,
                "finalUrl": toc.final_url,
                "status": toc.status,
                "contentType": toc.content_type,
            },
            "summary": {
                "url": summary.url,
                "finalUrl": summary.final_url,
                "status": summary.status,
                "contentType": summary.content_type,
            },
            "pdf": {
                "url": pdf.url,
                "finalUrl": pdf.final_url,
                "status": pdf.status,
                "contentType": pdf.content_type,
            },
        },
        "local": {
            "rawDir": str(raw_dir.relative_to(root)).replace("\\", "/"),
            "normalizedDir": str(normalized_dir.relative_to(root)).replace("\\", "/"),
        },
    }

    if args.dry_run:
        print(json.dumps(manifest, indent=2, ensure_ascii=True))
        return 0

    write_bytes(raw_dir / "parent.html", parent.body, args.force)
    write_bytes(raw_dir / "landing.html", landing.body, args.force)
    write_bytes(raw_dir / "toc.html", toc.body, args.force)
    write_bytes(raw_dir / "summary.html", summary.body, args.force)
    write_bytes(raw_dir / "spec.pdf", pdf.body, args.force)

    toc_sections = parse_toc_sections(toc.body.decode("utf-8", errors="replace"))
    write_json(normalized_dir / "manifest.json", manifest)
    write_json(normalized_dir / "toc.json", {"revision": revision, "sections": toc_sections})
    write_json(
        normalized_dir / "search-index.json",
        {
            "revision": revision,
            "aliases": [
                "H.264",
                "ITU-T H.264",
                "CABAC",
                "CAVLC",
                "DPB",
                "MMCO",
                "Annex B",
                "Annex C",
                "Table 9-45",
                "8.5.12.1",
            ],
            "entries": toc_sections,
        },
    )
    write_json(
        normalized_dir / "aliases.json",
        {
            "dpb": "Decoded picture buffer",
            "mmco": "Memory management control operation",
            "cabac": "Context-adaptive binary arithmetic coding",
            "cavlc": "Context-adaptive variable length coding",
        },
    )

    print(f"Synced ITU-T H.264 revision {revision} into {base_dir}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        raise SystemExit(1)
