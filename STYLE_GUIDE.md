# Sub0h264 Code Style Guide

**Read this when:** writing any C++ code, reviewing changes, touching an existing function, or deciding where to place new code.

Style conventions derived from the Sub0Pub sister project and NestNinja coding guide. Follow these for consistency.

---

## Naming

| Element | Convention | Example |
|---------|-----------|---------|
| Namespaces | lowercase | `sub0h264`, `detail` |
| Classes/Structs | PascalCase | `Decoder`, `Version`, `FrameBuffer` |
| Template parameters | PascalCase with `_t` suffix for type params | `Platform_t`, `Pixel_t` |
| Member variables | camelCase with `_` suffix | `width_`, `refCount_`, `nalType_` |
| Local variables | camelCase | `sliceIdx`, `mbCount` |
| Constants | `c` prefix + PascalCase | `cMaxWidth`, `cMaxRefFrames` |
| Macros/Defines | UPPER_SNAKE_CASE with `SUB0H264_` prefix | `SUB0H264_TRACE`, `SUB0H264_MAX_WIDTH` |
| Free functions | camelCase | `getVersion()`, `platformName()` |
| Type aliases | PascalCase | `PixelRow`, `NalUnit` |

### Prefer Full Words Over Abbreviations

Names are read far more often than they are written. Prefer full, descriptive names.

```cpp
// Correct — self-documenting
uint32_t frameCount = 0;
bool isConnected = false;

// Wrong — abbreviated, ambiguous
uint32_t frmCnt = 0;
bool isCon = false;
```

**Permitted abbreviations** (widely understood, no expansion needed):

| Abbrev | Meaning | | Abbrev | Meaning |
|--------|---------|---|--------|---------|
| `idx` | index | | `ptr` | pointer |
| `buf` | buffer | | `len` | length |
| `cfg` | configuration | | `ctx` | context |
| `err` | error | | `msg` | message |
| `nal` | NAL unit | | `mb` | macroblock |
| `sps` | sequence parameter set | | `pps` | picture parameter set |
| `fps` | frames per second | | `qp` | quantization parameter |

---

## Formatting

- **Indentation:** 4 spaces (no tabs)
- **Braces:** Opening brace on same line for control flow, next line for class/function definitions
- **Line width:** ~120 characters soft limit
- **Pointer/reference alignment:** `Type* name` (pointer with type), `const Type& name` (reference with type)

```cpp
class Decoder
{
public:
    Result decode(const uint8_t* data, uint32_t size) noexcept
    {
        for (uint32_t i = 0U; i < size; ++i)
        {
            if (data[i] == 0U)
                continue;
        }
        return Result::Ok;
    }
};
```

---

## No Magic Numbers

Every numeric literal with non-obvious meaning must be named. Use `constexpr` with a doc comment explaining the value's origin.

```cpp
// Correct — named, documented, traceable
/// Maximum macroblock width for 640px — ITU-T H.264 §7.4.2.1.
inline constexpr uint16_t cMaxWidthInMbs = 40U;

/// H.264 Baseline profile IDC — ITU-T H.264 Table A-1.
inline constexpr uint8_t cProfileBaseline = 66U;

// Wrong — reader has no idea what 40 or 66 mean
if (widthMbs > 40) return Error;
if (profile == 66) { ... }
```

---

## Prefer C++ Over Preprocessor

| Instead of... | Prefer... |
|---|---|
| `#define CONSTANT 42` | `inline constexpr T cConstant = 42;` |
| `#define MAX(a,b) ...` | `std::max` or `constexpr` function |
| `#ifdef PLATFORM_X` | `if constexpr (cPlatform == Platform::X)` or source-level dispatch |
| `#define ASSERT_SIZE(T,N)` | `static_assert(sizeof(T) == N)` |

Preprocessor is still required for include guards (`#ifndef CROG_SUB0H264_xxx_HPP`) and platform boundaries where `constexpr` is not possible.

---

## Templates

- Use angle brackets with space inside for readability: `template< typename T >`
- Use `using` aliases over `typedef` for new code
- Single-letter params (`T`, `N`) acceptable for generic templates; descriptive names for domain-specific

---

## Documentation

- Doxygen-style comments with `/** */` for public API
- `@tparam`, `@param[in]`, `@return`, `@remark`, `@note`, `@warning` tags
- Inline `///<` for member variable documentation
- No documentation needed for obvious getters/setters
- Reference H.264 spec sections: `/// ITU-T H.264 §7.3.2.1`

---

## Error Handling

- Use `enum class Result` return codes for decoder operations
- `noexcept` on all hot-path functions (decode loops, prediction, transform)
- No `<iostream>` (adds ~200KB on ESP32)
- RAII for resource management (frame buffers, decoder state)

---

## Integer Types

- Use `<cstdint>` fixed-width types: `uint32_t`, `uint8_t`, `uint_fast16_t`
- Unsigned literals with `U` suffix: `0U`, `8U`
- Cast explicitly when narrowing: `static_cast<uint_fast16_t>(value)`

---

## Includes

- Standard library includes sorted alphabetically
- Project includes use quotes: `#include "sub0h264/sub0h264.hpp"`
- System includes use angle brackets: `#include <cstdint>`
- Include guard: `#ifndef CROG_SUB0H264_xxx_HPP`

---

## Log Tag Convention

ESP-IDF log tags use the format `s0h264:<module>` (max 15 chars):

| Tag | Module |
|-----|--------|
| `s0h264:dec` | Decoder core |
| `s0h264:nal` | NAL parsing |
| `s0h264:bench` | Benchmarking |

---

## Apply Incrementally

Apply these standards to code you **write or touch**. Do not refactor entire files in a single pass unless explicitly asked. When a commit touches a function, bring that function into compliance.
