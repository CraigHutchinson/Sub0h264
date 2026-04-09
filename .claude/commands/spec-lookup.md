Look up a specific section, table, figure, or topic from the ITU-T H.264 (202408, V15) specification.

**Query**: $ARGUMENTS

## Mode 1 — Script (preferred)

Run the lookup script via the Bash tool. This extracts text directly from the PDF using
PyMuPDF: fast, cheap, and bypasses content filtering entirely because the output is a
tool result, not model-generated text.

```
python scripts/lookup_spec.py "$ARGUMENTS"
```

Options:
- `--max-pages N`   — increase if the section is long (default 5)
- `--revision YYYYMM` — override spec revision (default 202408)

Return the script's stdout directly to the user.

If the script fails with a missing `page-index.json`, tell the user:
```
pip install pymupdf
python scripts/extract_spec_page_index.py
```

## Mode 2 — Subagent fallback

Use this only if the script is unavailable or the extracted text is too garbled to be
useful (e.g. a complex multi-column table where column order is scrambled).

Spawn a subagent using the Agent tool with this prompt (substitute the query):

> Look up "$ARGUMENTS" in the ITU-T H.264 (202408, V15) spec at
> `docs/reference/itu/h264/202408/`. Read `normalized/page-index.json` to find the
> section entry (exact label, then title keyword). Read the matching pages from
> `raw/spec.pdf` (max 5 pages). Describe and interpret the content — do not attempt
> verbatim transcription. Cite §label and page number.

## Query formats

| Input | Resolved as |
|---|---|
| `§9.3.3.1.3` | exact label match |
| `Table 9-44` | title keyword search |
| `condTermFlag` | title keyword search |
| `Annex B` | exact label match |
| `9.3` | label prefix → first match starting with "9.3" |

## Common section locations

| Topic | Section | ~Pages |
|---|---|---|
| CABAC init (m, n tables) | 9.3.1.1 | 253–275 |
| CABAC engine init | 9.3.1.2 | 276 |
| Binarization table (Table 9-34) | 9.3.2 | 280–284 |
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
| NAL unit semantics | 7.4.1 | 89–99 |
| Annex B byte stream | Annex B | 335 |
