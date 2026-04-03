/** Sub0h264 — Inter prediction (motion compensation)
 *
 *  Implements H.264 luma and chroma interpolation filters for
 *  fractional-pel motion compensation in P-frames.
 *
 *  Reference: ITU-T H.264 §8.4.2
 *
 *  SPDX-License-Identifier: MIT
 */
#ifndef CROG_SUB0H264_INTER_PRED_HPP
#define CROG_SUB0H264_INTER_PRED_HPP

#include "frame.hpp"
#include "motion.hpp"
#include "transform.hpp" // clipU8

#include <cstdint>
#include <algorithm>

namespace sub0h264 {

/// 6-tap FIR filter coefficients for luma half-pel — ITU-T H.264 §8.4.2.2.1.
/// Filter: {1, -5, 20, 20, -5, 1} / 32
inline constexpr int32_t cLumaFilter6Tap[6] = { 1, -5, 20, 20, -5, 1 };

/** Apply 6-tap luma horizontal filter for half-pel position.
 *  Output = (src[-2] - 5*src[-1] + 20*src[0] + 20*src[1] - 5*src[2] + src[3] + 16) >> 5
 */
inline int32_t lumaHFilter(const uint8_t* src) noexcept
{
    return src[-2] - 5 * src[-1] + 20 * src[0] + 20 * src[1] - 5 * src[2] + src[3];
}

/** Apply 6-tap luma vertical filter for half-pel position. */
inline int32_t lumaVFilter(const uint8_t* src, uint32_t stride) noexcept
{
    return src[-2 * static_cast<int32_t>(stride)]
         - 5 * src[-1 * static_cast<int32_t>(stride)]
         + 20 * src[0]
         + 20 * src[1 * stride]
         - 5 * src[2 * stride]
         + src[3 * stride];
}

/** Perform luma motion compensation for a rectangular block.
 *
 *  Handles all 16 fractional-pel positions (dx,dy ∈ {0,1,2,3}).
 *  dx,dy represent quarter-pel offsets.
 *
 *  @param ref       Reference frame
 *  @param refX      Integer-pel X position in reference
 *  @param refY      Integer-pel Y position in reference
 *  @param dx        Horizontal fractional offset (0-3, quarter-pel)
 *  @param dy        Vertical fractional offset (0-3, quarter-pel)
 *  @param width     Block width in pixels
 *  @param height    Block height in pixels
 *  @param[out] dst  Destination buffer
 *  @param dstStride Destination stride
 */
inline void lumaMotionComp(const Frame& ref,
                            int32_t refX, int32_t refY,
                            uint32_t dx, uint32_t dy,
                            uint32_t width, uint32_t height,
                            uint8_t* dst, uint32_t dstStride) noexcept
{
    // Clamp reference position to frame bounds with margin for filter taps
    auto getSample = [&](int32_t x, int32_t y) -> uint8_t {
        int32_t maxX = static_cast<int32_t>(ref.width()) - 1;
        int32_t maxY = static_cast<int32_t>(ref.height()) - 1;
        x = (x < 0) ? 0 : (x > maxX ? maxX : x);
        y = (y < 0) ? 0 : (y > maxY ? maxY : y);
        return ref.y(static_cast<uint32_t>(x), static_cast<uint32_t>(y));
    };

    if (dx == 0U && dy == 0U)
    {
        // Full-pel copy
        for (uint32_t row = 0U; row < height; ++row)
            for (uint32_t col = 0U; col < width; ++col)
                dst[row * dstStride + col] = getSample(refX + col, refY + row);
        return;
    }

    if (dx == 0U && dy == 2U)
    {
        // Vertical half-pel
        for (uint32_t row = 0U; row < height; ++row)
            for (uint32_t col = 0U; col < width; ++col)
            {
                int32_t sum = 0;
                for (int32_t k = -2; k <= 3; ++k)
                    sum += cLumaFilter6Tap[k + 2] * getSample(refX + col, refY + row + k);
                dst[row * dstStride + col] = static_cast<uint8_t>(clipU8((sum + 16) >> 5));
            }
        return;
    }

    if (dx == 2U && dy == 0U)
    {
        // Horizontal half-pel
        for (uint32_t row = 0U; row < height; ++row)
            for (uint32_t col = 0U; col < width; ++col)
            {
                int32_t sum = 0;
                for (int32_t k = -2; k <= 3; ++k)
                    sum += cLumaFilter6Tap[k + 2] * getSample(refX + col + k, refY + row);
                dst[row * dstStride + col] = static_cast<uint8_t>(clipU8((sum + 16) >> 5));
            }
        return;
    }

    // Quarter-pel horizontal (dx=1 or 3, dy=0):
    // a=(G+b+1)>>1 for dx=1, c=(H+b+1)>>1 for dx=3. §8.4.2.2.1
    if (dy == 0U)
    {
        for (uint32_t row = 0U; row < height; ++row)
            for (uint32_t col = 0U; col < width; ++col)
            {
                // b = horizontal half-pel between col and col+1
                int32_t sum = 0;
                for (int32_t k = -2; k <= 3; ++k)
                    sum += cLumaFilter6Tap[k + 2] * getSample(refX + col + k, refY + row);
                int32_t b = clipU8((sum + 16) >> 5);
                // Integer sample: G for dx=1, H (col+1) for dx=3
                int32_t full = getSample(refX + col + (dx == 3U ? 1 : 0), refY + row);
                dst[row * dstStride + col] = static_cast<uint8_t>((full + b + 1) >> 1);
            }
        return;
    }

    // Quarter-pel vertical (dx=0, dy=1 or 3):
    // d=(G+h+1)>>1 for dy=1, n=(J+h+1)>>1 for dy=3. §8.4.2.2.1
    if (dx == 0U)
    {
        for (uint32_t row = 0U; row < height; ++row)
            for (uint32_t col = 0U; col < width; ++col)
            {
                // h = vertical half-pel between row and row+1
                int32_t sum = 0;
                for (int32_t k = -2; k <= 3; ++k)
                    sum += cLumaFilter6Tap[k + 2] * getSample(refX + col, refY + row + k);
                int32_t h = clipU8((sum + 16) >> 5);
                // Integer sample: G for dy=1, J (row+1) for dy=3
                int32_t full = getSample(refX + col, refY + row + (dy == 3U ? 1 : 0));
                dst[row * dstStride + col] = static_cast<uint8_t>((full + h + 1) >> 1);
            }
        return;
    }

    // ── Diagonal fractional positions (dx!=0, dy!=0) ────────────────
    // ITU-T H.264 §8.4.2.2.1: requires computing intermediate half-pel
    // values b, h, j, m, s and then averaging per Table 8-12.

    // Helper: horizontal 6-tap producing UNCLIPPED intermediate (for j computation).
    auto hFilter = [&](int32_t x, int32_t y) -> int32_t {
        int32_t sum = 0;
        for (int32_t k = -2; k <= 3; ++k)
            sum += cLumaFilter6Tap[k + 2] * getSample(x + k, y);
        return sum; // NOT clipped — intermediate for 2D filter
    };

    // Helper: horizontal 6-tap, clipped (for b, s values).
    auto hFilterClip = [&](int32_t x, int32_t y) -> int32_t {
        return clipU8((hFilter(x, y) + 16) >> 5);
    };

    // Helper: vertical 6-tap, clipped (for h, m values).
    auto vFilterClip = [&](int32_t x, int32_t y) -> int32_t {
        int32_t sum = 0;
        for (int32_t k = -2; k <= 3; ++k)
            sum += cLumaFilter6Tap[k + 2] * getSample(x, y + k);
        return clipU8((sum + 16) >> 5);
    };

    // Helper: j = 2D 6-tap at (x, y). Vertical 6-tap on horizontal intermediates.
    // §8.4.2.2.1: j = clip((vFilter(hFilter_unclipped) + 512) >> 10)
    auto j2D = [&](int32_t x, int32_t y) -> int32_t {
        int32_t sum = 0;
        for (int32_t k = -2; k <= 3; ++k)
            sum += cLumaFilter6Tap[k + 2] * hFilter(x, y + k);
        return clipU8((sum + 512) >> 10);
    };

    // Compute per-pixel based on fractional position — Table 8-12:
    //   (1,1)=e: (b+h+1)>>1     (2,1)=f: (b+j+1)>>1     (3,1)=g: (b+m+1)>>1
    //   (1,2)=i: (h+j+1)>>1     (2,2)=j: 2D filter       (3,2)=k: (j+m+1)>>1
    //   (1,3)=p: (h+s+1)>>1     (2,3)=q: (j+s+1)>>1     (3,3)=r: (m+s+1)>>1
    // Where: b=hHalf@row, h=vHalf@col, j=2D@(col,row),
    //        m=vHalf@(col+1), s=hHalf@(row+1)
    for (uint32_t row = 0U; row < height; ++row)
    {
        for (uint32_t col = 0U; col < width; ++col)
        {
            int32_t x = refX + static_cast<int32_t>(col);
            int32_t y = refY + static_cast<int32_t>(row);
            int32_t val;

            if (dx == 2U && dy == 2U)
            {
                // j = 2D 6-tap — §8.4.2.2.1 Eq. 8-239/8-240
                val = j2D(x, y);
            }
            else if (dx == 1U && dy == 1U)
            {
                // e = (b + h + 1) >> 1
                val = (hFilterClip(x, y) + vFilterClip(x, y) + 1) >> 1;
            }
            else if (dx == 3U && dy == 1U)
            {
                // g = (b + m + 1) >> 1 — m is vertical half at col+1
                val = (hFilterClip(x, y) + vFilterClip(x + 1, y) + 1) >> 1;
            }
            else if (dx == 1U && dy == 3U)
            {
                // p = (h + s + 1) >> 1 — s is horizontal half at row+1
                val = (vFilterClip(x, y) + hFilterClip(x, y + 1) + 1) >> 1;
            }
            else if (dx == 3U && dy == 3U)
            {
                // r = (m + s + 1) >> 1
                val = (vFilterClip(x + 1, y) + hFilterClip(x, y + 1) + 1) >> 1;
            }
            else if (dx == 2U && dy == 1U)
            {
                // f = (b + j + 1) >> 1
                val = (hFilterClip(x, y) + j2D(x, y) + 1) >> 1;
            }
            else if (dx == 2U && dy == 3U)
            {
                // q = (j + s + 1) >> 1
                val = (j2D(x, y) + hFilterClip(x, y + 1) + 1) >> 1;
            }
            else if (dx == 1U && dy == 2U)
            {
                // i = (h + j + 1) >> 1
                val = (vFilterClip(x, y) + j2D(x, y) + 1) >> 1;
            }
            else // dx == 3U && dy == 2U
            {
                // k = (j + m + 1) >> 1
                val = (j2D(x, y) + vFilterClip(x + 1, y) + 1) >> 1;
            }

            dst[row * dstStride + col] = static_cast<uint8_t>(val);
        }
    }
}

/** Perform chroma bilinear motion compensation.
 *
 *  @param ref       Reference frame
 *  @param refX      Integer-pel X position in chroma plane
 *  @param refY      Integer-pel Y position in chroma plane
 *  @param dx        Horizontal fraction (0-7, eighth-pel)
 *  @param dy        Vertical fraction (0-7, eighth-pel)
 *  @param width     Block width in chroma pixels
 *  @param height    Block height in chroma pixels
 *  @param isU       True for U plane, false for V
 *  @param[out] dst  Destination buffer
 *  @param dstStride Destination stride
 *
 *  Reference: ITU-T H.264 §8.4.2.2.2
 */
inline void chromaMotionComp(const Frame& ref,
                              int32_t refX, int32_t refY,
                              uint32_t dx, uint32_t dy,
                              uint32_t width, uint32_t height,
                              bool isU, uint8_t* dst, uint32_t dstStride) noexcept
{
    uint32_t chromaW = ref.width() / 2U;
    uint32_t chromaH = ref.height() / 2U;

    auto getSample = [&](int32_t x, int32_t y) -> uint8_t {
        int32_t maxCx = static_cast<int32_t>(chromaW) - 1;
        int32_t maxCy = static_cast<int32_t>(chromaH) - 1;
        x = (x < 0) ? 0 : (x > maxCx ? maxCx : x);
        y = (y < 0) ? 0 : (y > maxCy ? maxCy : y);
        return isU ? ref.u(static_cast<uint32_t>(x), static_cast<uint32_t>(y))
                   : ref.v(static_cast<uint32_t>(x), static_cast<uint32_t>(y));
    };

    /// Chroma filter weights — ITU-T H.264 §8.4.2.2.2 Eq. 8-258.
    uint32_t w00 = (8U - dx) * (8U - dy);
    uint32_t w10 = dx * (8U - dy);
    uint32_t w01 = (8U - dx) * dy;
    uint32_t w11 = dx * dy;

    for (uint32_t row = 0U; row < height; ++row)
    {
        for (uint32_t col = 0U; col < width; ++col)
        {
            uint32_t a = getSample(refX + col, refY + row);
            uint32_t b = getSample(refX + col + 1, refY + row);
            uint32_t c = getSample(refX + col, refY + row + 1);
            uint32_t d = getSample(refX + col + 1, refY + row + 1);

            uint32_t val = (w00 * a + w10 * b + w01 * c + w11 * d + 32U) >> 6U;
            dst[row * dstStride + col] = static_cast<uint8_t>(val);
        }
    }
}

} // namespace sub0h264

#endif // CROG_SUB0H264_INTER_PRED_HPP
