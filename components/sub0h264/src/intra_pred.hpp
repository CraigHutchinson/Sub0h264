/** Sub0h264 — Intra prediction for I-frame reconstruction
 *
 *  Implements H.264 intra prediction modes for luma (4x4, 16x16)
 *  and chroma (8x8) blocks.
 *
 *  Reference: ITU-T H.264 §8.3.1 (intra prediction)
 *
 *  SPDX-License-Identifier: MIT
 */
#ifndef CROG_SUB0H264_INTRA_PRED_HPP
#define CROG_SUB0H264_INTRA_PRED_HPP

#include "frame.hpp"

#include <cstdint>
#include <cstring>

namespace sub0h264 {

/// Intra 4x4 prediction modes — ITU-T H.264 Table 8-2.
enum class Intra4x4Mode : uint8_t
{
    Vertical        = 0U,
    Horizontal      = 1U,
    Dc              = 2U,
    DiagDownLeft    = 3U,
    DiagDownRight   = 4U,
    VerticalRight   = 5U,
    HorizontalDown  = 6U,
    VerticalLeft    = 7U,
    HorizontalUp    = 8U,
};

/// Intra 16x16 prediction modes — ITU-T H.264 Table 8-3.
enum class Intra16x16Mode : uint8_t
{
    Vertical   = 0U,
    Horizontal = 1U,
    Dc         = 2U,
    Plane      = 3U,
};

/// Intra chroma prediction modes — ITU-T H.264 Table 8-4.
enum class IntraChromaMode : uint8_t
{
    Dc         = 0U,
    Horizontal = 1U,
    Vertical   = 2U,
    Plane      = 3U,
};

/// Default prediction value when no neighbors are available.
inline constexpr uint8_t cDefaultPredValue = 128U;

// ── Filter helpers ──────────────────────────────────────────────────────

/// 2-tap average: (a + b + 1) >> 1
inline constexpr uint8_t filt11(uint8_t a, uint8_t b) noexcept
{
    return static_cast<uint8_t>((a + b + 1U) >> 1U);
}

/// 3-tap weighted: (a + 2*b + c + 2) >> 2
inline constexpr uint8_t filt121(uint8_t a, uint8_t b, uint8_t c) noexcept
{
    return static_cast<uint8_t>((a + 2U * b + c + 2U) >> 2U);
}

// ── Intra 4x4 prediction — ITU-T H.264 §8.3.1.2 ───────────────────────

/** Generate a 4x4 intra prediction block.
 *
 *  @param mode      Prediction mode [0-8]
 *  @param top       4 pixels above the block (nullptr if unavailable)
 *  @param topRight  4 pixels to the top-right (nullptr if unavailable)
 *  @param left      4 pixels to the left (nullptr if unavailable)
 *  @param topLeft   Top-left pixel (nullptr if unavailable)
 *  @param[out] pred Output 4x4 prediction block (stride=4)
 */
inline void intraPred4x4(Intra4x4Mode mode,
                          const uint8_t* top, const uint8_t* topRight,
                          const uint8_t* left, const uint8_t* topLeft,
                          uint8_t* pred) noexcept
{
    switch (mode)
    {
    case Intra4x4Mode::Vertical:
        if (top)
        {
            for (uint32_t row = 0U; row < 4U; ++row)
                std::memcpy(pred + row * 4U, top, 4U);
        }
        break;

    case Intra4x4Mode::Horizontal:
        if (left)
        {
            for (uint32_t row = 0U; row < 4U; ++row)
                std::memset(pred + row * 4U, left[row], 4U);
        }
        break;

    case Intra4x4Mode::Dc:
    {
        uint32_t sum = 0U;
        uint32_t count = 0U;
        if (top)  { for (uint32_t i = 0U; i < 4U; ++i) sum += top[i];  count += 4U; }
        if (left) { for (uint32_t i = 0U; i < 4U; ++i) sum += left[i]; count += 4U; }

        uint8_t val = cDefaultPredValue;
        if (count == 8U) val = static_cast<uint8_t>((sum + 4U) >> 3U);
        else if (count == 4U) val = static_cast<uint8_t>((sum + 2U) >> 2U);

        std::memset(pred, val, 16U);
        break;
    }

    case Intra4x4Mode::DiagDownLeft:
        if (top)
        {
            const uint8_t* tr = topRight ? topRight : top; // Fallback
            uint8_t t[8] = { top[0], top[1], top[2], top[3],
                             tr[0], tr[1], tr[2], tr[3] };
            for (uint32_t row = 0U; row < 4U; ++row)
                for (uint32_t col = 0U; col < 4U; ++col)
                {
                    uint32_t idx = row + col;
                    pred[row * 4U + col] = (idx == 6U)
                        ? filt121(t[6], t[7], t[7])
                        : filt121(t[idx], t[idx + 1U], t[idx + 2U]);
                }
        }
        break;

    case Intra4x4Mode::DiagDownRight:
        if (top && left && topLeft)
        {
            uint8_t tl = *topLeft;
            for (uint32_t row = 0U; row < 4U; ++row)
                for (uint32_t col = 0U; col < 4U; ++col)
                {
                    if (col > row)
                        pred[row * 4U + col] = filt121(top[col - row - 2U < 4U ? col - row - 2U : 0U],
                                                        top[col - row - 1U],
                                                        col - row < 4U ? top[col - row] : top[3]);
                    else if (col == row)
                        pred[row * 4U + col] = filt121(left[0], tl, top[0]);
                    else
                        pred[row * 4U + col] = filt121(
                            row - col - 2U < 4U ? left[row - col - 2U] : left[0],
                            left[row - col - 1U],
                            row - col == 1U ? tl : left[row - col - 2U < 4U ? row - col - 2U : 0U]);
                }
        }
        break;

    default:
        // Modes 5-8 (VR, HD, VL, HU) — implement using DC as fallback for now
        // These are less common and will be added as needed for test vector compliance
        {
            uint32_t sum = 0U;
            uint32_t count = 0U;
            if (top)  { for (uint32_t i = 0U; i < 4U; ++i) sum += top[i];  count += 4U; }
            if (left) { for (uint32_t i = 0U; i < 4U; ++i) sum += left[i]; count += 4U; }
            uint8_t val = cDefaultPredValue;
            if (count == 8U) val = static_cast<uint8_t>((sum + 4U) >> 3U);
            else if (count == 4U) val = static_cast<uint8_t>((sum + 2U) >> 2U);
            std::memset(pred, val, 16U);
        }
        break;
    }
}

// ── Intra 16x16 prediction — ITU-T H.264 §8.3.3 ───────────────────────

/** Generate a 16x16 intra prediction block.
 *
 *  @param mode       Prediction mode [0-3]
 *  @param frame      Frame containing reconstructed neighbors
 *  @param mbX, mbY   Macroblock position
 *  @param[out] pred  Output 16x16 prediction block (stride=16)
 */
inline void intraPred16x16(Intra16x16Mode mode,
                            const Frame& frame, uint32_t mbX, uint32_t mbY,
                            uint8_t* pred) noexcept
{
    uint32_t pixX = mbX * cMbSize;
    uint32_t pixY = mbY * cMbSize;
    bool hasTop  = (mbY > 0U);
    bool hasLeft = (mbX > 0U);

    switch (mode)
    {
    case Intra16x16Mode::Vertical:
        if (hasTop)
        {
            const uint8_t* topRow = frame.yRow(pixY - 1U) + pixX;
            for (uint32_t row = 0U; row < 16U; ++row)
                std::memcpy(pred + row * 16U, topRow, 16U);
        }
        break;

    case Intra16x16Mode::Horizontal:
        if (hasLeft)
        {
            for (uint32_t row = 0U; row < 16U; ++row)
            {
                uint8_t leftPix = frame.y(pixX - 1U, pixY + row);
                std::memset(pred + row * 16U, leftPix, 16U);
            }
        }
        break;

    case Intra16x16Mode::Dc:
    {
        uint32_t sum = 0U;
        uint32_t count = 0U;
        if (hasTop)
        {
            const uint8_t* topRow = frame.yRow(pixY - 1U) + pixX;
            for (uint32_t i = 0U; i < 16U; ++i) sum += topRow[i];
            count += 16U;
        }
        if (hasLeft)
        {
            for (uint32_t i = 0U; i < 16U; ++i) sum += frame.y(pixX - 1U, pixY + i);
            count += 16U;
        }

        uint8_t val = cDefaultPredValue;
        if (count == 32U) val = static_cast<uint8_t>((sum + 16U) >> 5U);
        else if (count == 16U) val = static_cast<uint8_t>((sum + 8U) >> 4U);

        std::memset(pred, val, 256U);
        break;
    }

    case Intra16x16Mode::Plane:
        if (hasTop && hasLeft)
        {
            uint8_t topLeft = frame.y(pixX - 1U, pixY - 1U);
            const uint8_t* topRow = frame.yRow(pixY - 1U) + pixX;

            int32_t h = 0, v = 0;
            for (int32_t i = 0; i < 8; ++i)
            {
                h += (i + 1) * (topRow[8 + i] - topRow[6 - i]);
                v += (i + 1) * (static_cast<int32_t>(frame.y(pixX - 1U, pixY + 8U + i))
                              - static_cast<int32_t>(frame.y(pixX - 1U, pixY + 6U - i)));
            }

            int32_t a = 16 * (static_cast<int32_t>(frame.y(pixX - 1U, pixY + 15U)) + topRow[15]);
            int32_t b = (5 * h + 32) >> 6;
            int32_t c = (5 * v + 32) >> 6;

            for (uint32_t row = 0U; row < 16U; ++row)
            {
                for (uint32_t col = 0U; col < 16U; ++col)
                {
                    int32_t val = (a + b * (static_cast<int32_t>(col) - 7)
                                     + c * (static_cast<int32_t>(row) - 7) + 16) >> 5;
                    pred[row * 16U + col] = static_cast<uint8_t>(clipU8(val));
                }
            }
        }
        break;
    }
}

// ── Intra chroma prediction (8x8) — ITU-T H.264 §8.3.4 ────────────────

/** Generate an 8x8 intra chroma prediction block.
 *
 *  @param mode       Prediction mode [0-3]
 *  @param frame      Frame containing reconstructed chroma neighbors
 *  @param mbX, mbY   Macroblock position
 *  @param isU        True for U (Cb) plane, false for V (Cr)
 *  @param[out] pred  Output 8x8 prediction block (stride=8)
 */
inline void intraPredChroma8x8(IntraChromaMode mode,
                                const Frame& frame, uint32_t mbX, uint32_t mbY,
                                bool isU, uint8_t* pred) noexcept
{
    uint32_t pixX = mbX * cChromaBlockSize;
    uint32_t pixY = mbY * cChromaBlockSize;
    bool hasTop  = (mbY > 0U);
    bool hasLeft = (mbX > 0U);

    auto getPixel = [&](uint32_t x, uint32_t y) -> uint8_t {
        return isU ? frame.u(x, y) : frame.v(x, y);
    };
    auto getRow = [&](uint32_t y) -> const uint8_t* {
        return isU ? frame.uRow(y) : frame.vRow(y);
    };

    switch (mode)
    {
    case IntraChromaMode::Dc:
    {
        // 4 quadrants, each with independent DC prediction
        for (uint32_t qy = 0U; qy < 2U; ++qy)
        {
            for (uint32_t qx = 0U; qx < 2U; ++qx)
            {
                uint32_t sum = 0U;
                uint32_t count = 0U;

                if (hasTop)
                {
                    const uint8_t* topRow = getRow(pixY - 1U);
                    for (uint32_t i = 0U; i < 4U; ++i)
                        sum += topRow[pixX + qx * 4U + i];
                    count += 4U;
                }
                if (hasLeft)
                {
                    for (uint32_t i = 0U; i < 4U; ++i)
                        sum += getPixel(pixX - 1U, pixY + qy * 4U + i);
                    count += 4U;
                }

                uint8_t val = cDefaultPredValue;
                if (count == 8U) val = static_cast<uint8_t>((sum + 4U) >> 3U);
                else if (count == 4U) val = static_cast<uint8_t>((sum + 2U) >> 2U);

                for (uint32_t row = 0U; row < 4U; ++row)
                    std::memset(pred + (qy * 4U + row) * 8U + qx * 4U, val, 4U);
            }
        }
        break;
    }

    case IntraChromaMode::Horizontal:
        if (hasLeft)
        {
            for (uint32_t row = 0U; row < 8U; ++row)
            {
                uint8_t leftPix = getPixel(pixX - 1U, pixY + row);
                std::memset(pred + row * 8U, leftPix, 8U);
            }
        }
        break;

    case IntraChromaMode::Vertical:
        if (hasTop)
        {
            const uint8_t* topRow = getRow(pixY - 1U) + pixX;
            for (uint32_t row = 0U; row < 8U; ++row)
                std::memcpy(pred + row * 8U, topRow, 8U);
        }
        break;

    case IntraChromaMode::Plane:
        if (hasTop && hasLeft)
        {
            const uint8_t* topRow = getRow(pixY - 1U) + pixX;
            int32_t h = 0, v = 0;
            for (int32_t i = 0; i < 4; ++i)
            {
                h += (i + 1) * (topRow[4 + i] - topRow[2 - i]);
                v += (i + 1) * (static_cast<int32_t>(getPixel(pixX - 1U, pixY + 4U + i))
                              - static_cast<int32_t>(getPixel(pixX - 1U, pixY + 2U - i)));
            }

            int32_t a = 16 * (static_cast<int32_t>(getPixel(pixX - 1U, pixY + 7U)) + topRow[7]);
            int32_t b = (17 * h + 16) >> 5;
            int32_t c = (17 * v + 16) >> 5;

            for (uint32_t row = 0U; row < 8U; ++row)
            {
                for (uint32_t col = 0U; col < 8U; ++col)
                {
                    int32_t val = (a + b * (static_cast<int32_t>(col) - 3)
                                     + c * (static_cast<int32_t>(row) - 3) + 16) >> 5;
                    pred[row * 8U + col] = static_cast<uint8_t>(clipU8(val));
                }
            }
        }
        break;
    }
}

} // namespace sub0h264

#endif // CROG_SUB0H264_INTRA_PRED_HPP
