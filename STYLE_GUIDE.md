# Sub0h264 Code Style Guide

Style conventions derived from the Sub0Pub sister project. Follow these for consistency.

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

## Formatting

- **Indentation:** 4 spaces (no tabs)
- **Braces:** Opening brace on same line for control flow, next line for class/function definitions
- **Line width:** ~120 characters soft limit
- **Pointer/reference alignment:** `Type* name` (pointer with type), `const Type& name` (reference with type)

```cpp
// Class definition
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

## Templates

- Use angle brackets with space inside for readability: `template< typename T >`
- Use `using` aliases over `typedef` for new code

## Documentation

- Doxygen-style comments with `/** */` for public API
- `@tparam`, `@param[in]`, `@return`, `@remark`, `@note`, `@warning` tags
- Inline `///<` for member variable documentation
- No documentation needed for obvious getters/setters

## Preprocessor

- Feature flags use `#ifndef` / `#define` / `#endif` pattern with default values
- Guard conditions: `#if SUB0H264_FLAG` (not `#ifdef`)
- Include guard: `#ifndef CROG_SUB0H264_xxx_HPP`

## Error Handling

- Use `enum class Result` return codes for decoder operations
- `noexcept` on all hot-path functions
- No `<iostream>` (adds ~200KB on ESP32)

## Integer Types

- Use `<cstdint>` fixed-width types: `uint32_t`, `uint8_t`, `uint_fast16_t`
- Unsigned literals with `U` suffix: `0U`, `8U`
- Cast explicitly when narrowing: `static_cast<uint_fast16_t>(value)`

## Includes

- Standard library includes sorted alphabetically
- Project includes use quotes: `#include "sub0h264/sub0h264.hpp"`
- System includes use angle brackets: `#include <cstdint>`
