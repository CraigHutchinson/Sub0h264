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
        x = std::max(0, std::min(static_cast<int32_t>(ref.width()) - 1, x));
        y = std::max(0, std::min(static_cast<int32_t>(ref.height()) - 1, y));
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

    // Quarter-pel: average between two half-pel positions
    // General case: first interpolate to the two bracketing half-pel positions,
    // then average. For simplicity, use a two-pass approach.
    if (dy == 0U)
    {
        // Horizontal quarter-pel: average full and half
        int32_t halfX = (dx == 1U) ? refX : refX + 1;
        for (uint32_t row = 0U; row < height; ++row)
            for (uint32_t col = 0U; col < width; ++col)
            {
                int32_t full = getSample(refX + col + (dx == 3U ? 1 : 0), refY + row);
                int32_t sum = 0;
                for (int32_t k = -2; k <= 3; ++k)
                    sum += cLumaFilter6Tap[k + 2] * getSample(halfX + col + k - (dx == 3U ? 1 : 0), refY + row);
                int32_t half = clipU8((sum + 16) >> 5);
                dst[row * dstStride + col] = static_cast<uint8_t>(clipU8((full + half + 1) >> 1));
            }
        return;
    }

    if (dx == 0U)
    {
        // Vertical quarter-pel: average full and half
        for (uint32_t row = 0U; row < height; ++row)
            for (uint32_t col = 0U; col < width; ++col)
            {
                int32_t full = getSample(refX + col, refY + row + (dy == 3U ? 1 : 0));
                int32_t sum = 0;
                for (int32_t k = -2; k <= 3; ++k)
                    sum += cLumaFilter6Tap[k + 2] * getSample(refX + col, refY + row + k);
                int32_t half = clipU8((sum + 16) >> 5);
                dst[row * dstStride + col] = static_cast<uint8_t>(clipU8((full + half + 1) >> 1));
            }
        return;
    }

    // Diagonal fractional position: interpolate in both dimensions
    // Use horizontal half-pel first, then vertical filter on that
    for (uint32_t row = 0U; row < height; ++row)
        for (uint32_t col = 0U; col < width; ++col)
        {
            // Horizontal half-pel at (refX + col, refY + row)
            int32_t sumH = 0;
            for (int32_t k = -2; k <= 3; ++k)
                sumH += cLumaFilter6Tap[k + 2] * getSample(refX + col + k, refY + row);
            int32_t halfH = clipU8((sumH + 16) >> 5);

            // Vertical half-pel at (refX + col, refY + row)
            int32_t sumV = 0;
            for (int32_t k = -2; k <= 3; ++k)
                sumV += cLumaFilter6Tap[k + 2] * getSample(refX + col, refY + row + k);
            int32_t halfV = clipU8((sumV + 16) >> 5);

            // Average for diagonal position
            dst[row * dstStride + col] = static_cast<uint8_t>(clipU8((halfH + halfV + 1) >> 1));
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
        x = std::max(0, std::min(static_cast<int32_t>(chromaW) - 1, x));
        y = std::max(0, std::min(static_cast<int32_t>(chromaH) - 1, y));
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
