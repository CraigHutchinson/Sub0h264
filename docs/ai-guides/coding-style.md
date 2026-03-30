# Coding Style Guide — Sub0h264

**Read this when:** writing any C++ code, reviewing a PR, touching an existing function, or deciding where to place a new utility.

---

## No Magic Numbers

Every numeric literal with non-obvious meaning must be named. Use `constexpr` with a one-line doc comment explaining the value's origin and a reference to its specification/datasheet section.

```cpp
// Correct — named, documented, traceable
/// Maximum entries in offsetForRefFrame — ITU-T H.264 §7.4.2.1 allows 255,
/// capped at 16 for stack safety (no real stream exceeds this).
static constexpr uint32_t cMaxRefFramesInPocCycle = 16U;

/// 6-tap FIR filter coefficients for luma half-pel — ITU-T H.264 §8.4.2.2.1.
inline constexpr int32_t cLumaFilter6Tap[6] = { 1, -5, 20, 20, -5, 1 };

// Wrong — reader has no idea what 255 or 16 mean
int32_t offsetForRefFrame_[255]{};
val <<= (qpDiv6 - 2);
```

When the origin of a value is unknown, add a `TODO:` rather than leaving a bare literal:

```cpp
static constexpr uint32_t cSomeThreshold = 4096U; // TODO: trace origin
```

---

## Naming Conventions

Follow `STYLE_GUIDE.md` for type/variable naming. Additionally:

### Prefer Full Words Over Abbreviations

Names are read far more often than they are written. Prefer full, descriptive names.

```cpp
// Correct — self-explanatory
uint32_t frameCount      = 0U;
bool     isAllocated     = false;
int32_t  chromaQpIndex   = 0;

// Wrong — abbreviated, ambiguous
uint32_t frmCnt = 0U;
bool     isAlloc = false;
int32_t  cQpIdx = 0;
```

### Permitted abbreviations

| Abbreviation | Meaning | Rationale |
|---|---|---|
| `idx` | index | Universal in systems code |
| `ptr` | pointer | Universal |
| `buf` | buffer | Universal |
| `len` | length | Universal |
| `ctx` | context | Common C API convention |
| `nal` | Network Abstraction Layer | H.264 domain term |
| `sps` / `pps` | Sequence/Picture Parameter Set | H.264 domain terms |
| `mb` | macroblock | H.264 domain term |
| `mv` | motion vector | H.264 domain term |
| `dpb` | Decoded Picture Buffer | H.264 domain term |
| `fps` | frames per second | Media domain |
| `qp` | quantization parameter | H.264 domain term |
| `cbp` | coded block pattern | H.264 domain term |

Abbreviations **not** on this list require expansion. When in doubt, write it out.

---

## Prefer C++ Over Preprocessor

Use C++ language features instead of C preprocessor macros wherever C++17 makes this viable.

| Instead of… | Prefer… |
|---|---|
| `#define CONSTANT 42` | `inline constexpr T cConstant = 42;` |
| `#define MAX(a,b) ...` | `std::max` or inline helper |
| `#ifdef PLATFORM_X ... #endif` | `if constexpr` or conditional compilation in CMake |
| `#define ASSERT_SIZE(T,N)` | `static_assert(sizeof(T) == N)` |

Preprocessor is still required for include guards (`#ifndef CROG_SUB0H264_*`), platform detection macros (`SUB0H264_*`), and `#pragma once`. Document any remaining `#define` with a comment explaining why C++ alternatives are not applicable.

---

## Bit-field Structs Over Raw Bit Manipulation

Prefer typed bit-field structs for packed protocol fields. Always include:
- A `static_assert` verifying `sizeof` and alignment.
- A reference comment linking to the spec section.

Raw shift/mask is permitted when the struct approach creates alignment issues or for isolated single-bit checks in well-commented inline expressions.

---

## C APIs → C++ Types

When wrapping C APIs (ESP-IDF), prefer C++ value types at internal boundaries:
- `std::span<const uint8_t>` instead of `uint8_t*, size_t` pairs
- Typed `enum class` instead of `#define` or raw integer error codes
- RAII wrappers for C handle types

At the actual C ABI call site, convert back with an explicit cast. Keep the conversion visible.

---

## Utility Placement — Promote Reusable Code

When writing a function that has wider applicability, move it to the appropriate shared location.

| Scope of reuse | Destination |
|---|---|
| Any platform | `components/sub0h264/src/` (decoder internals) |
| Public API | `components/sub0h264/include/sub0h264/` |
| Platform-specific SIMD | `components/sub0h264/platform/{P}/` |
| Test helpers | `tests/` or `test_apps/` |

If moving now would be disruptive, add a comment: `// TODO: promote to <destination>`.

---

## Embedded-Specific Rules

### Stack Safety
- Never allocate large structures on the stack. Use `std::make_unique` for objects > 1KB.
- The ESP32-P4 main task stack is 64KB. Assert high-water mark after heavy operations.
- Cap array sizes in structs with named constants (e.g., `cMaxRefFramesInPocCycle`).

### No Dynamic Allocation in Hot Paths
- Pre-allocate all frame buffers and DPB entries during init.
- Use `resize()` not `push_back()` for known-size vectors.
- Avoid `std::map` / `std::unordered_map` in decode loops.

### FreeRTOS Integration
- Yield every N frames in long decode loops: `vTaskDelay(pdMS_TO_TICKS(1))`.
- Set `CONFIG_ESP_TASK_WDT_PANIC=n` during testing.
- Use `heap_caps_get_free_size(MALLOC_CAP_SPIRAM)` to verify PSRAM availability.

---

## Log Tag Convention

All ESP-IDF `ESP_LOGx()` calls must use standardised log tags:

```
sub0h264:<module>
```

| Tag | Module |
|-----|--------|
| `sub0h264_test` | Unit test runner |
| `sub0h264` | Core decoder |

---

## Apply Incrementally

Apply these standards to code you **write or touch**. Do not refactor entire files in a single pass unless explicitly asked. When a PR touches a function, bring that function and its immediate context into compliance. Leave the rest for when it is next edited. Record gaps as `TODO:` comments.
