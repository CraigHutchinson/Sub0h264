# H.264 Spec Tables — C++23 Refactor Plan

## Context

The Sub0h264 codebase contains ~30 numeric tables derived from the ITU-T H.264 specification (Tables 7-11 through 9-23, plus §8.x tables). Most are already `inline constexpr std::array`, but the project uses C++17 and lacks:
- Compile-time validation (`static_assert`) that table data matches the spec
- `consteval` generators for derived/packed tables (e.g. CABAC combined table)
- Consistent spec references on all tables
- Comments flagging any divergence from spec values
- The CABAC tables cite "libavc" as source rather than the spec directly

The user has confirmed upgrading from C++17 to C++23 is acceptable.

---

## Phase 0: Build System — C++17 → C++23

**Task 0.1** — Update `components/sub0h264/CMakeLists.txt:36`: change `cxx_std_17` → `cxx_std_23`.
- Verify desktop build: `cmake --preset default && cmake --build --preset default`
- Run tests: `ctest --preset default`

**Files:** `components/sub0h264/CMakeLists.txt`

---

## Phase 1: Static Validation Test Infrastructure

**Task 1.1** — Create `tests/test_spec_tables.cpp` for compile-time table validation.
- Add to `tests/test_sources.cmake`
- Include `static_assert` checks and `TEST_CASE` wrappers for runtime diagnostics
- Start with a skeleton that includes all table headers

**Task 1.2** — Add `static_assert` validations for simple tables in `tables.hpp`:
- `cZigzag4x4`: size == 16, all values in [0,15], is a valid permutation (constexpr helper)
- `cZigzag8x8`: size == 64, all values in [0,63], is a valid permutation
- `cChromaQpTable`: size == 52, identity for [0,29], monotonically non-decreasing, max == 39
- `cCbpTable`: size == 48, intra CBP values cover all valid patterns
- `cLevelSuffixThreshold`: size == 7, strictly increasing (except sentinel)
- `cNumMbPartP`, `cMbPartWidthP`, `cMbPartHeightP`, `cNumSubMbPartP`: size checks

**Task 1.3** — Add `static_assert` validations for `cavlc_tables.hpp`:
- `cCoeffTokenCode`/`cCoeffTokenSize`: dimension checks [3][4][17]
- Zero-code entries have zero size (invalid combinations where trailingOnes > totalCoeff)
- `cCoeffTokenCodeChroma`/`cCoeffTokenSizeChroma`: dimension checks [4][5]
- `cTotalZerosIndex`: monotonically increasing, last index + entries == 135
- `cTotalZerosSize`/`cTotalZerosCode`: size == 135
- `cRunBeforeIndex`: monotonically increasing
- `cRunBeforeSize`/`cRunBeforeCode`: size == 42

**Task 1.4** — Add `static_assert` validations for `deblock.hpp`:
- `cAlphaTable`: size == 52, monotonically non-decreasing, max == 255
- `cBetaTable`: size == 52, monotonically non-decreasing, max == 18
- `cTc0Table`: size [52][4], column 0 always 0, monotonically non-decreasing per row

**Task 1.5** — Add `static_assert` validations for `transform.hpp`:
- `cDequantScale`: size [6][3], all values > 0
- `cDequantPosClass`: size == 16, all values in {0,1,2}, correct symmetry pattern

**Task 1.6** — Add `static_assert` validations for CABAC tables:
- `cCabacTable`: size [128][4]
- `cCabacInitMN`: size [4][460][2], all m values in [-128,127], all n values in [-128,127]
- `cNumCabacCtx == 460`

**Task 1.7** — Add `static_assert` for scalar constants:
- `cMaxSpsCount == 32`, `cMaxRefFrames == 16`, `cMaxQp == 51`
- `cProfileBaseline == 66`, `cProfileMain == 77`, `cProfileHigh == 100`
- `cMbSize == 16`, `cChromaBlockSize == 8`, `cDefaultPicInitQp == 26`
- `cMaxCoeff4x4 == 16`, `cMaxTrailingOnes == 3`, `cMaxSuffixLength == 6`

**Run tests after each sub-task.** All static_asserts fire at compile time, so build failure = test failure.

**Files:** `tests/test_spec_tables.cpp` (new), `tests/test_sources.cmake`

---

## Phase 2: C-style Arrays → `std::array`

**Task 2.1** — Convert `cCabacInitMN[4][460][2]` (C-style `int8_t[][]`) to `std::array<std::array<std::array<int8_t, 2>, 460>, 4>`.
- File: `components/sub0h264/src/cabac_init_mn.hpp`
- Update usage in `cabac.hpp:initCabacContexts()` — indexing syntax is identical for `std::array`
- Run tests

**Task 2.2** — Convert `cCabacTable[128][4]` (C-style `uint32_t[][]`) to `std::array<std::array<uint32_t, 4>, 128>`.
- File: `components/sub0h264/src/cabac.hpp`
- Update usage in `CabacEngine::decodeBin()` — indexing identical
- Run tests

**Task 2.3** — Convert `cCoeffTokenCode[3][4][17]` and `cCoeffTokenSize[3][4][17]` to nested `std::array`.
- File: `components/sub0h264/src/cavlc_tables.hpp`
- Run tests

**Task 2.4** — Convert `cCoeffTokenCodeChroma[4][5]` and `cCoeffTokenSizeChroma[4][5]` to `std::array`.
- File: `components/sub0h264/src/cavlc_tables.hpp`
- Run tests

**Task 2.5** — Convert `cLumaFilter6Tap[6]` to `std::array<int32_t, 6>`.
- File: `components/sub0h264/src/inter_pred.hpp`
- Run tests

**Task 2.6** — Convert `cTc0Table[52][4]` to `std::array<std::array<uint8_t, 4>, 52>`.
- File: `components/sub0h264/src/deblock.hpp`
- Run tests

---

## Phase 3: Consteval Generators for Derived Tables

### 3.1 — CABAC Combined Table (`cCabacTable`)

The current `cCabacTable[128][4]` is pre-computed from libavc, packing `rangeTabLPS`, `nextStateMPS`, and `nextStateLPS` into uint32_t. This should be generated at compile time from the raw spec tables.

**Task 3.1.1** — Create `components/sub0h264/src/cabac_tables_gen.hpp`:
- Define raw spec tables as `constexpr`:
  - `cRangeTabLPS[64][4]` — ITU-T H.264 Table 9-48
  - `cTransIdxLPS[64]` — ITU-T H.264 Table 9-45 (next state after LPS)
  - `cTransIdxMPS[64]` — ITU-T H.264 Table 9-45 (next state after MPS)
- Write a `consteval` function that packs these into the combined format matching `cCabacTable`
- Add `static_assert` verifying the generated table matches the existing pre-computed values exactly
- If values diverge: keep both, add `// DIVERGENCE:` comment explaining which is used and why

**Task 3.1.2** — Once validated, replace the hand-written `cCabacTable` with the `consteval`-generated version.
- Keep the raw spec tables as the source of truth
- Run tests

### 3.2 — CABAC Init (m,n) Verification

**Task 3.2.1** — Cross-reference `cCabacInitMN[4][460][2]` against ITU-T H.264 Tables 9-12 through 9-23.
- The current table says "extracted from libavc" — verify against spec
- Add precise spec table references for each context range (e.g., contexts 0-10 = Table 9-12)
- Add comments grouping contexts by their spec table source
- Flag any divergences with `// DIVERGENCE: spec says X, libavc has Y`
- Run tests

### 3.3 — Dequant Position Class Generator

**Task 3.3.1** — Replace hand-written `cDequantPosClass[16]` with a `consteval` function.
- The position class follows a pattern: class depends on `(row%2, col%2)` in 4x4 grid
- Generate from formula: class 0 when both even, class 1 when both odd, class 2 otherwise
- Verify with `static_assert` against current values
- File: `components/sub0h264/src/transform.hpp`
- Run tests

---

## Phase 4: Flattened Tables → Structured Tables

The total_zeros and run_before tables in `cavlc_tables.hpp` use flattened arrays with separate index arrays. This is fragile — restructure into nested arrays.

**Task 4.1** — Restructure total_zeros tables:
- Replace `cTotalZerosSize[135]` + `cTotalZerosCode[135]` + `cTotalZerosIndex[15]` with a struct-of-arrays or array-of-arrays approach
- Option: `std::array<std::span<const uint8_t>, 15>` referencing sub-ranges of a backing array, or nested `std::array` with padding
- Update `decodeTotalZeros()` in `cavlc.hpp` to use new structure
- Run tests

**Task 4.2** — Restructure run_before tables:
- Replace `cRunBeforeSize[42]` + `cRunBeforeCode[42]` + `cRunBeforeIndex[7]` similarly
- Update `decodeRunBefore()` in `cavlc.hpp`
- Run tests

**Task 4.3** — Restructure chroma DC total_zeros tables:
- Replace `cTotalZerosSizeChroma[9]` + `cTotalZerosCodeChroma[9]` similarly
- Update `decodeTotalZerosChromaDC()` in `cavlc.hpp`
- Run tests

---

## Phase 5: Magic Numbers in Algorithmic Code

### 5.1 — Intra Prediction (`intra_pred.hpp`)

**Task 5.1.1** — Name magic numbers in plane prediction formulas:
- `intraPred16x16` Plane mode: `5`, `32`, `7` → spec-derived constants with comments
  - `5` = (5 * H + 32) >> 6 — ITU-T H.264 §8.3.3.4 Eq. 8-133/8-134
  - `7` = offset for 16x16 center
- `intraPredChroma8x8` Plane mode: `17`, `16`, `3` → spec-derived constants
  - `17` = (17 * H + 16) >> 5 — ITU-T H.264 §8.3.4.4
  - `3` = offset for 8x8 center
- Run tests

### 5.2 — Deblocking Filter (`deblock.hpp`)

**Task 5.2.1** — Name magic numbers in filter formulas:
- `computeBs()`: `4` (quarter-pel threshold for 1 full pixel) → named constant
- `filterLumaStrong()`: `(alpha >> 2) + 2` — spec formula, add comment ref
- Rounding constants in filter math: document with spec equation references
- Run tests

### 5.3 — Transform (`transform.hpp`)

**Task 5.3.1** — Name magic numbers:
- `32` (rounding for 6-bit shift), `>> 6` → named constants with spec refs
- `clipU8`: `0`, `255` → `cMinPixelValue`, `cMaxPixelValue` (or just spec ref comment)
- `clampQpIdx`: `0`, `51` → use `cMaxQp` from cabac.hpp (avoid duplication)
- Run tests

### 5.4 — CABAC Engine (`cabac.hpp`)

**Task 5.4.1** — Name magic numbers:
- `510U` (initial codIRange) → `cCabacInitRange = 510U` with §9.3.1.2 ref
- `9U` (initial offset bits) → `cCabacInitOffsetBits = 9U`
- `256U` (renormalization threshold) → `cCabacRenormThreshold = 256U`
- Bit-packing masks `0x7FU`, `0x3FU`, `0xFFU` → named or documented
- Run tests

### 5.5 — Inter Prediction (`inter_pred.hpp`)

**Task 5.5.1** — Name magic numbers in motion compensation:
- `16` and `>> 5` in luma filter rounding → spec ref comment
- `8U` in chroma bilinear weights → `cChromaFilterDenom = 8U`
- `32U` and `>> 6` in chroma rounding → spec ref
- Run tests

### 5.6 — CAVLC Decoder (`cavlc.hpp`)

**Task 5.6.1** — Name remaining magic numbers:
- `10U` threshold for initial suffixLen=1 → named constant with spec ref
- `4096U` in level decode escape → named with spec ref
- `cChromaDcMaxCoeff = 4U` is already a local constexpr — promote to tables.hpp
- Run tests

### 5.7 — SPS/Slice/PPS Parsers

**Task 5.7.1** — Name magic numbers in parsers:
- `65535U` (max idr_pic_id) → named constant
- `256` in scaling list: `(lastScale + deltaScale + 256) % 256` → spec ref comment
- Bit widths (8, 6, 16) used in readBits() → spec ref comments where non-obvious
- Run tests

---

## Phase 6: CABAC Context Offset Table

**Task 6.1** — Create a structured context offset mapping table:
- Instead of 18 separate `inline constexpr` values, create a single structured table or enum
- Map syntax element names to context ranges with size information
- Add spec table reference for each entry (Table 9-11)
- Verify all offsets + sizes don't overlap and cover [0, 460)
- `static_assert` validation of non-overlap
- File: `components/sub0h264/src/cabac_parse.hpp`
- Run tests

---

## Phase 7: Documentation Pass

**Task 7.1** — Review all table headers for consistent spec reference format:
- Standard format: `/// <description> — ITU-T H.264 Table X-Y` or `§X.Y.Z`
- Ensure every table has a reference
- Ensure `cabac_init_mn.hpp` header references spec tables, not just "libavc"

---

## Verification Strategy

After **every** task:
1. `cmake --build --preset default` — must compile (static_asserts checked here)
2. `ctest --preset default` — all existing tests must pass
3. No test expectations modified unless a table value was genuinely wrong (flagged with `// DIVERGENCE:` comment)

**Key files to modify:**
- `components/sub0h264/CMakeLists.txt` — C++ standard
- `components/sub0h264/src/tables.hpp` — general tables
- `components/sub0h264/src/cavlc_tables.hpp` — CAVLC VLC tables
- `components/sub0h264/src/cabac.hpp` — CABAC engine + combined table
- `components/sub0h264/src/cabac_init_mn.hpp` — CABAC init (m,n)
- `components/sub0h264/src/cabac_parse.hpp` — CABAC context offsets
- `components/sub0h264/src/deblock.hpp` — deblocking tables
- `components/sub0h264/src/transform.hpp` — dequant tables
- `components/sub0h264/src/inter_pred.hpp` — filter coefficients
- `components/sub0h264/src/intra_pred.hpp` — prediction magic numbers
- `components/sub0h264/src/sps.hpp` — parser constants
- `components/sub0h264/src/cavlc.hpp` — CAVLC decoder magic numbers
- `components/sub0h264/src/frame.hpp` — frame constants
- `tests/test_spec_tables.cpp` — **new** static validation test file
- `tests/test_sources.cmake` — add new test file

**Existing functions/utilities to reuse:**
- `clipU8()` in `transform.hpp:20`
- `clampQpIdx()` in `transform.hpp:26`
- `computeCabacInitState()` in `cabac.hpp:371`
- All existing `std::array` table patterns in `tables.hpp`

**Risks:**
- ESP32 IDF toolchain may not support all C++23 features — test after Phase 0
- Restructuring flattened tables (Phase 4) changes code paths in hot decoder loops — thorough testing needed
- CABAC combined table regeneration (Phase 3.1) requires precise spec data for rangeTabLPS and state transitions — any error breaks decoding
