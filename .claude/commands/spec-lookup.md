Look up a specific section, table, figure, or topic from the ITU-T H.264 (202408, V15) specification and return its verbatim text, tables, equations, or algorithm pseudocode.

**Query**: $ARGUMENTS

## Instructions

### 1. Understand what is being requested

Classify the query:
- **Section reference**: `§9.3.3.1.3`, `8.5.12.1`, `Annex B`, `A.2.4` → look up by label
- **Table/Figure reference**: `Table 9-45`, `Figure 9-3` → no direct label match; use chapter heuristic
- **Keyword**: `rangeTabLPS`, `condTermFlag`, `CAVLC` → search titles

Normalize the reference: strip leading §, trailing punctuation.

### 2. Read the page index

Read `docs/reference/itu/h264/202408/normalized/page-index.json`.

Search for a matching entry using this priority order:
1. **Exact label match**: `entry.label == normalized_reference`
2. **Prefix match**: entry.label starts with the query (e.g. `9.3.3` → finds `9.3.3.1.3`)
3. **Title keyword search** (case-insensitive): `entry.title` contains the query text

**For Table/Figure references** (e.g. `Table 9-45`):
- Table numbers embed the chapter: `Table 9-45` → chapter 9 → read §9.3.3.2.x pages, which contain the CABAC tables
- `Table 7-x` → chapter 7 syntax, `Table 8-x` → chapter 8 reconstruction, etc.
- Read the most likely containing section (usually the section whose label starts with the table's chapter number and covers the relevant algorithm)
- **V15 renumbering note**: V15 (08/2024) removed Annex F and renumbered subsequent content. Table and figure numbers in code comments citing older revisions may be off by 1 or more. Cross-check against the actual table content, not just the number.

**For keyword searches** (e.g. `rangeTabLPS`, `condTermFlag`):
- Find sections whose titles contain the keyword
- Also check nearby sections (often the table is inside a section rather than named in its title)

### 3. Read the PDF pages

If page-index.json is missing, tell the user:
```
page-index.json is missing. Generate it with:
  pip install pymupdf
  python scripts/extract_spec_page_index.py
```

Use the Read tool on `docs/reference/itu/h264/202408/raw/spec.pdf` with the `pages` parameter.
- Set `pages` to `"<page>-<endPage>"` from the matched entry
- Cap the range: read at most 5 pages at a time (`page` to `min(endPage, page+4)`)
- If the content spans more than 5 pages, read the first 5 and offer to continue

### 4. Return the spec text

Extract and present verbatim:
- The section heading and number
- All normative text, syntax tables, equations, and pseudocode
- Any cross-references to related sections

Always cite: **ITU-T H.264 §\<label\>, Table/Figure \<N\>, page \<page\> (V15, 08/2024)**

### 5. Common section locations (for fast lookup)

| Topic | Section | ~Pages |
|-------|---------|--------|
| CABAC init (m, n tables) | 9.3.1.1 | 253–275 |
| CABAC engine init | 9.3.1.2 | 276 |
| Binarization (Table 9-34) | 9.3.2 | 280–284 |
| ctxIdx derivation | 9.3.3.1 | 285–304 |
| Arithmetic decode | 9.3.3.2.1 | 299 |
| rangeTabLPS (Table 9-44) | 9.3.3.2.1.1 | 300 |
| State transitions (Table 9-45) | 9.3.3.2.1.1 | 301–302 |
| CAVLC coeff_token | 9.2.1 | 241 |
| Intra prediction | 8.3 | 159 |
| Inter prediction / MC | 8.4 | 178 |
| Dequant / IDCT | 8.5 | 203 |
| Deblocking | 8.7 | 228 |
| SPS syntax | 7.3.2.1 | 69 |
| Slice header syntax | 7.3.3 | 79 |
| Annex B byte stream | Annex B | 335 |
