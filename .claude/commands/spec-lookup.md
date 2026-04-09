Look up a specific section, table, figure, or topic from the ITU-T H.264 (202408, V15) specification and return its verbatim text, tables, equations, or algorithm pseudocode.

**Query**: $ARGUMENTS

## Instructions

Spawn a subagent using the Agent tool to perform the lookup. This keeps the PDF page images out of the caller's context window.

Pass the following as the subagent prompt, substituting the query:

---

**Subagent prompt:**

Look up "$ARGUMENTS" in the ITU-T H.264 (202408, V15) specification stored locally. Return the verbatim spec text — section headings, normative text, syntax tables, equations, pseudocode — as plain text. Do NOT return images; transcribe the content.

### Step 1 — Classify and normalize the query

- Section ref: `§9.3.3.1.3`, `8.5.12.1`, `Annex B`, `A.2.4` → strip §, use as label
- Table/Figure ref: `Table 9-45`, `Figure 9-3` → use chapter heuristic (see Step 2c)
- Keyword: `rangeTabLPS`, `condTermFlag` → search titles

### Step 2 — Find the page range

Read `docs/reference/itu/h264/202408/normalized/page-index.json`.

Search order:
1. **Exact label match**: `entry.label == normalized_query`
2. **Title keyword search** (case-insensitive): `entry.title` contains the query

**For Table/Figure references** (e.g. `Table 9-45`):
- Chapter prefix = first digit(s) before dash: `Table 9-45` → chapter 9
- Read the section in that chapter most likely to contain the table (use common locations below)
- V15 renumbering note: V15 removed Annex F; table/figure numbers from old code comments may be off by 1.

If page-index.json is missing, stop and return:
`page-index.json missing — run: pip install pymupdf && python scripts/extract_spec_page_index.py`

### Step 3 — Read the PDF pages

Use the Read tool on `docs/reference/itu/h264/202408/raw/spec.pdf` with `pages: "<page>-<endPage>"`.
- Read at most 5 pages at a time. If the section spans more, read the first 5 pages then read the next batch if the content is incomplete.

### Step 4 — Return transcribed text only

Transcribe all visible text from the PDF pages relevant to the query:
- Section heading and number
- Normative text, definitions, constraints
- Tables: reproduce as markdown tables or structured text
- Equations/pseudocode: reproduce verbatim using ASCII math

Cite at the top: **ITU-T H.264 §\<label\>, page \<page\> (V15, 08/2024)**

Do not return images or describe page layout — only the spec content.

### Common section locations

| Topic | Section | ~Pages |
|-------|---------|--------|
| CABAC init (m, n tables) | 9.3.1.1 | 253–275 |
| CABAC engine init | 9.3.1.2 | 276 |
| Binarization table (Table 9-34) | 9.3.2 | 280–284 |
| ctxIdx derivation | 9.3.3.1 | 285–304 |
| Arithmetic decode flowchart | 9.3.3.2.1 | 299 |
| rangeTabLPS (Table 9-44) | 9.3.3.2.1.1 | 300 |
| State transitions (Table 9-45) | 9.3.3.2.1.1 | 301–302 |
| CAVLC coeff_token | 9.2.1 | 241 |
| Intra prediction | 8.3 | 159 |
| Inter prediction / MC | 8.4 | 178 |
| Dequant / IDCT | 8.5 | 203 |
| Deblocking | 8.7 | 228 |
| SPS syntax | 7.3.2.1 | 69 |
| Slice header syntax | 7.3.3 | 79 |
| NAL unit semantics | 7.4.1 | 89–99 |
| Annex B byte stream | Annex B | 335 |

---

Return the subagent's response directly to the user.
