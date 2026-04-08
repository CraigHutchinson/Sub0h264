/** Spec-only Intra Prediction
 *
 *  ITU-T H.264 Section 8.3.1.2 (Intra_4x4 prediction)
 *  ITU-T H.264 Section 8.3.3   (Intra_16x16 prediction)
 *  ITU-T H.264 Section 8.3.4   (Chroma prediction)
 *
 *  All prediction modes implemented from spec sample equations.
 *
 *  SPDX-License-Identifier: MIT
 */
#ifndef CROG_SUB0H264_SPEC_REF_INTRA_PRED_HPP
#define CROG_SUB0H264_SPEC_REF_INTRA_PRED_HPP

#include <cstdint>
#include <algorithm>

namespace sub0h264 {
namespace spec_ref {

// ============================================================================
// Intra_4x4 Prediction Modes -- ITU-T H.264 Section 8.3.1.2
//
// Reference samples:
//   p[-1,-1]  p[0,-1] p[1,-1] p[2,-1] p[3,-1] p[4,-1] p[5,-1] p[6,-1] p[7,-1]
//   p[-1, 0]
//   p[-1, 1]
//   p[-1, 2]
//   p[-1, 3]
//
// The prediction is written to pred[y*4+x] for x,y in [0,3].
// ============================================================================

/// Intra_4x4 prediction mode indices -- ITU-T H.264 Table 8-2.
inline constexpr uint32_t cIntra4x4Vertical         = 0U;
inline constexpr uint32_t cIntra4x4Horizontal        = 1U;
inline constexpr uint32_t cIntra4x4Dc                = 2U;
inline constexpr uint32_t cIntra4x4DiagDownLeft      = 3U;
inline constexpr uint32_t cIntra4x4DiagDownRight     = 4U;
inline constexpr uint32_t cIntra4x4VerticalRight     = 5U;
inline constexpr uint32_t cIntra4x4HorizontalDown    = 6U;
inline constexpr uint32_t cIntra4x4VerticalLeft      = 7U;
inline constexpr uint32_t cIntra4x4HorizontalUp      = 8U;

/** Generate Intra_4x4 prediction block.
 *
 *  @param pred     Output: 16-byte prediction in raster order (row-major 4x4)
 *  @param above    Pointer to pixels above: above[0..7]=p[0,-1]..p[7,-1]
 *  @param left     Pointer to pixels left: left[0..3] = p[-1,0]..p[-1,3]
 *  @param topLeft  The pixel p[-1,-1]
 *  @param mode     Prediction mode (0..8)
 *  @param hasAbove True if above samples are available
 *  @param hasLeft  True if left samples are available
 */
inline void intra4x4Predict(uint8_t pred[16],
                             const uint8_t* above,
                             const uint8_t* left,
                             uint8_t topLeft,
                             uint32_t mode,
                             bool hasAbove,
                             bool hasLeft) noexcept
{
    // Alias: a[x] = above[x] = p[x, -1], l[y] = left[y] = p[-1, y], tl = p[-1, -1]
    auto a = [&](int x) -> int32_t { return above[x]; };
    auto l = [&](int y) -> int32_t { return left[y]; };
    int32_t tl = topLeft;

    auto clip = [](int32_t v) -> uint8_t {
        return static_cast<uint8_t>(std::clamp(v, 0, 255));
    };

    switch (mode) {
    case cIntra4x4Vertical:
        // ITU-T H.264 Section 8.3.1.2.1
        // pred[y][x] = p[x, -1]
        for (uint32_t y = 0U; y < 4U; ++y)
            for (uint32_t x = 0U; x < 4U; ++x)
                pred[y * 4 + x] = static_cast<uint8_t>(a(x));
        break;

    case cIntra4x4Horizontal:
        // ITU-T H.264 Section 8.3.1.2.2
        // pred[y][x] = p[-1, y]
        for (uint32_t y = 0U; y < 4U; ++y)
            for (uint32_t x = 0U; x < 4U; ++x)
                pred[y * 4 + x] = static_cast<uint8_t>(l(y));
        break;

    case cIntra4x4Dc: {
        // ITU-T H.264 Section 8.3.1.2.3
        int32_t sum = 0;
        int32_t count = 0;
        if (hasAbove) {
            for (uint32_t x = 0U; x < 4U; ++x)
                sum += a(x);
            count += 4;
        }
        if (hasLeft) {
            for (uint32_t y = 0U; y < 4U; ++y)
                sum += l(y);
            count += 4;
        }
        uint8_t dc;
        if (count == 8)
            dc = static_cast<uint8_t>((sum + 4) >> 3);
        else if (count == 4)
            dc = static_cast<uint8_t>((sum + 2) >> 2);
        else
            dc = 128U;
        for (uint32_t i = 0U; i < 16U; ++i)
            pred[i] = dc;
        break;
    }

    case cIntra4x4DiagDownLeft:
        // ITU-T H.264 Section 8.3.1.2.4
        // pred[y][x] = (p[x+y, -1] + 2*p[x+y+1, -1] + p[x+y+2, -1] + 2) >> 2
        // Exception: pred[3][3] = (p[6,-1] + 3*p[7,-1] + 2) >> 2
        for (uint32_t y = 0U; y < 4U; ++y) {
            for (uint32_t x = 0U; x < 4U; ++x) {
                if (x == 3U && y == 3U) {
                    pred[y * 4 + x] = clip((a(6) + 3 * a(7) + 2) >> 2);
                } else {
                    pred[y * 4 + x] = clip((a(x + y) + 2 * a(x + y + 1) + a(x + y + 2) + 2) >> 2);
                }
            }
        }
        break;

    case cIntra4x4DiagDownRight:
        // ITU-T H.264 Section 8.3.1.2.5
        for (uint32_t y = 0U; y < 4U; ++y) {
            for (uint32_t x = 0U; x < 4U; ++x) {
                if (x > y) {
                    // Above-right diagonal
                    pred[y * 4 + x] = clip((a(x - y - 2) + 2 * a(x - y - 1) + a(x - y) + 2) >> 2);
                } else if (x < y) {
                    // Left-below diagonal
                    pred[y * 4 + x] = clip((l(y - x - 2) + 2 * l(y - x - 1) + l(y - x) + 2) >> 2);
                } else {
                    // x == y: diagonal
                    pred[y * 4 + x] = clip((a(0) + 2 * tl + l(0) + 2) >> 2);
                }
            }
        }
        break;

    case cIntra4x4VerticalRight:
        // ITU-T H.264 Section 8.3.1.2.6
        for (uint32_t y = 0U; y < 4U; ++y) {
            for (uint32_t x = 0U; x < 4U; ++x) {
                int32_t zVR = 2 * static_cast<int32_t>(x) - static_cast<int32_t>(y);
                if (zVR >= 0 && (zVR & 1) == 0) {
                    // Even zVR >= 0
                    int32_t idx = x - (y >> 1);
                    if (idx == 0)
                        pred[y * 4 + x] = clip((tl + a(0) + 1) >> 1);
                    else
                        pred[y * 4 + x] = clip((a(idx - 1) + a(idx) + 1) >> 1);
                } else if (zVR >= 0 && (zVR & 1) == 1) {
                    // Odd zVR >= 0
                    int32_t idx = x - (y >> 1) - 1;
                    if (idx == -1)
                        pred[y * 4 + x] = clip((l(0) + 2 * tl + a(0) + 2) >> 2);
                    else if (idx == 0)
                        pred[y * 4 + x] = clip((tl + 2 * a(0) + a(1) + 2) >> 2);
                    else
                        pred[y * 4 + x] = clip((a(idx - 1) + 2 * a(idx) + a(idx + 1) + 2) >> 2);
                } else if (zVR == -1) {
                    pred[y * 4 + x] = clip((l(0) + 2 * tl + a(0) + 2) >> 2);
                } else {
                    // zVR < -1
                    int32_t idx = y - 1;
                    pred[y * 4 + x] = clip((l(idx - 2) + 2 * l(idx - 1) + l(idx) + 2) >> 2);
                }
            }
        }
        break;

    case cIntra4x4HorizontalDown:
        // ITU-T H.264 Section 8.3.1.2.7
        for (uint32_t y = 0U; y < 4U; ++y) {
            for (uint32_t x = 0U; x < 4U; ++x) {
                int32_t zHD = 2 * static_cast<int32_t>(y) - static_cast<int32_t>(x);
                if (zHD >= 0 && (zHD & 1) == 0) {
                    int32_t idx = y - (x >> 1);
                    if (idx == 0)
                        pred[y * 4 + x] = clip((tl + l(0) + 1) >> 1);
                    else
                        pred[y * 4 + x] = clip((l(idx - 1) + l(idx) + 1) >> 1);
                } else if (zHD >= 0 && (zHD & 1) == 1) {
                    int32_t idx = y - (x >> 1) - 1;
                    if (idx == -1)
                        pred[y * 4 + x] = clip((a(0) + 2 * tl + l(0) + 2) >> 2);
                    else if (idx == 0)
                        pred[y * 4 + x] = clip((tl + 2 * l(0) + l(1) + 2) >> 2);
                    else
                        pred[y * 4 + x] = clip((l(idx - 1) + 2 * l(idx) + l(idx + 1) + 2) >> 2);
                } else if (zHD == -1) {
                    pred[y * 4 + x] = clip((a(0) + 2 * tl + l(0) + 2) >> 2);
                } else {
                    int32_t idx = x - 1;
                    pred[y * 4 + x] = clip((a(idx - 2) + 2 * a(idx - 1) + a(idx) + 2) >> 2);
                }
            }
        }
        break;

    case cIntra4x4VerticalLeft:
        // ITU-T H.264 Section 8.3.1.2.8
        for (uint32_t y = 0U; y < 4U; ++y) {
            for (uint32_t x = 0U; x < 4U; ++x) {
                if ((y & 1) == 0) {
                    // Even row
                    int32_t idx = x + (y >> 1);
                    pred[y * 4 + x] = clip((a(idx) + a(idx + 1) + 1) >> 1);
                } else {
                    // Odd row
                    int32_t idx = x + (y >> 1);
                    pred[y * 4 + x] = clip((a(idx) + 2 * a(idx + 1) + a(idx + 2) + 2) >> 2);
                }
            }
        }
        break;

    case cIntra4x4HorizontalUp:
        // ITU-T H.264 Section 8.3.1.2.9
        for (uint32_t y = 0U; y < 4U; ++y) {
            for (uint32_t x = 0U; x < 4U; ++x) {
                int32_t zHU = static_cast<int32_t>(x) + 2 * static_cast<int32_t>(y);
                if (zHU < 6 && (zHU & 1) == 0) {
                    int32_t idx = y + (x >> 1);
                    pred[y * 4 + x] = clip((l(idx) + l(idx + 1) + 1) >> 1);
                } else if (zHU < 6 && (zHU & 1) == 1) {
                    int32_t idx = y + (x >> 1);
                    pred[y * 4 + x] = clip((l(idx) + 2 * l(idx + 1) + l(idx + 2) + 2) >> 2);
                } else if (zHU == 6) {
                    pred[y * 4 + x] = clip((l(2) + 3 * l(3) + 2) >> 2);
                } else {
                    pred[y * 4 + x] = static_cast<uint8_t>(l(3));
                }
            }
        }
        break;

    default:
        // Unknown mode -- fill with 128
        for (uint32_t i = 0U; i < 16U; ++i)
            pred[i] = 128U;
        break;
    }
}

// ============================================================================
// Intra_16x16 Prediction Modes -- ITU-T H.264 Section 8.3.3
// ============================================================================

/// Intra_16x16 prediction mode indices -- ITU-T H.264 Table 8-4.
inline constexpr uint32_t cIntra16x16Vertical   = 0U;
inline constexpr uint32_t cIntra16x16Horizontal  = 1U;
inline constexpr uint32_t cIntra16x16Dc          = 2U;
inline constexpr uint32_t cIntra16x16Plane       = 3U;

/** Generate Intra_16x16 prediction block.
 *
 *  @param pred      Output: 256-byte prediction in raster order (row-major 16x16)
 *  @param above     Pointer to 16 pixels above the macroblock
 *  @param left      Pointer to 16 pixels to the left of the macroblock
 *  @param topLeft   The pixel at position (-1, -1)
 *  @param mode      Prediction mode (0..3)
 *  @param hasAbove  True if above samples are available
 *  @param hasLeft   True if left samples are available
 */
inline void intra16x16Predict(uint8_t pred[256],
                               const uint8_t* above,
                               const uint8_t* left,
                               uint8_t topLeft,
                               uint32_t mode,
                               bool hasAbove,
                               bool hasLeft) noexcept
{
    switch (mode) {
    case cIntra16x16Vertical:
        // ITU-T H.264 Section 8.3.3.1
        for (uint32_t y = 0U; y < 16U; ++y)
            for (uint32_t x = 0U; x < 16U; ++x)
                pred[y * 16 + x] = above[x];
        break;

    case cIntra16x16Horizontal:
        // ITU-T H.264 Section 8.3.3.2
        for (uint32_t y = 0U; y < 16U; ++y)
            for (uint32_t x = 0U; x < 16U; ++x)
                pred[y * 16 + x] = left[y];
        break;

    case cIntra16x16Dc: {
        // ITU-T H.264 Section 8.3.3.3
        int32_t sum = 0;
        int32_t count = 0;
        if (hasAbove) {
            for (uint32_t x = 0U; x < 16U; ++x)
                sum += above[x];
            count += 16;
        }
        if (hasLeft) {
            for (uint32_t y = 0U; y < 16U; ++y)
                sum += left[y];
            count += 16;
        }
        uint8_t dc;
        if (count == 32)
            dc = static_cast<uint8_t>((sum + 16) >> 5);
        else if (count == 16)
            dc = static_cast<uint8_t>((sum + 8) >> 4);
        else
            dc = 128U;
        for (uint32_t i = 0U; i < 256U; ++i)
            pred[i] = dc;
        break;
    }

    case cIntra16x16Plane: {
        // ITU-T H.264 Section 8.3.3.4
        int32_t H = 0;
        for (int32_t x = 0; x < 8; ++x)
            H += (x + 1) * (static_cast<int32_t>(above[8 + x]) - static_cast<int32_t>(above[6 - x]));

        int32_t V = 0;
        for (int32_t y = 0; y < 8; ++y)
            V += (y + 1) * (static_cast<int32_t>(left[8 + y]) - static_cast<int32_t>(left[6 - y]));

        int32_t a16 = 16 * (static_cast<int32_t>(above[15]) + static_cast<int32_t>(left[15]));
        int32_t b = (5 * H + 32) >> 6;
        int32_t c = (5 * V + 32) >> 6;

        for (uint32_t y = 0U; y < 16U; ++y) {
            for (uint32_t x = 0U; x < 16U; ++x) {
                int32_t val = (a16 + b * (static_cast<int32_t>(x) - 7) +
                               c * (static_cast<int32_t>(y) - 7) + 16) >> 5;
                pred[y * 16 + x] = static_cast<uint8_t>(std::clamp(val, 0, 255));
            }
        }
        break;
    }

    default:
        for (uint32_t i = 0U; i < 256U; ++i)
            pred[i] = 128U;
        break;
    }
}

// ============================================================================
// Chroma Prediction Modes -- ITU-T H.264 Section 8.3.4
// ============================================================================

/// Chroma prediction mode indices -- ITU-T H.264 Table 8-5.
/// Note: chroma uses a DIFFERENT mode numbering than luma 16x16!
inline constexpr uint32_t cIntraChromaDc      = 0U;
inline constexpr uint32_t cIntraChromaH       = 1U;
inline constexpr uint32_t cIntraChromaV       = 2U;
inline constexpr uint32_t cIntraChromaPlane   = 3U;

/** Generate chroma prediction block (8x8 for 4:2:0).
 *
 *  @param pred      Output: 64-byte prediction in raster order (row-major 8x8)
 *  @param above     Pointer to 8 pixels above
 *  @param left      Pointer to 8 pixels to the left
 *  @param topLeft   The pixel at position (-1, -1)
 *  @param mode      Chroma prediction mode (0..3)
 *  @param hasAbove  True if above samples are available
 *  @param hasLeft   True if left samples are available
 */
inline void intraChromaPredict(uint8_t pred[64],
                                const uint8_t* above,
                                const uint8_t* left,
                                uint8_t topLeft,
                                uint32_t mode,
                                bool hasAbove,
                                bool hasLeft) noexcept
{
    switch (mode) {
    case cIntraChromaDc: {
        // ITU-T H.264 Section 8.3.4.1
        // For 4:2:0: four 4x4 sub-blocks, each with its own DC
        for (uint32_t blkY = 0U; blkY < 2U; ++blkY) {
            for (uint32_t blkX = 0U; blkX < 2U; ++blkX) {
                int32_t sum = 0;
                int32_t count = 0;

                if (hasAbove) {
                    for (uint32_t x = blkX * 4; x < blkX * 4 + 4; ++x)
                        sum += above[x];
                    count += 4;
                }
                if (hasLeft) {
                    for (uint32_t y = blkY * 4; y < blkY * 4 + 4; ++y)
                        sum += left[y];
                    count += 4;
                }

                uint8_t dc;
                if (count == 8)
                    dc = static_cast<uint8_t>((sum + 4) >> 3);
                else if (count == 4)
                    dc = static_cast<uint8_t>((sum + 2) >> 2);
                else
                    dc = 128U;

                for (uint32_t y = blkY * 4; y < blkY * 4 + 4; ++y)
                    for (uint32_t x = blkX * 4; x < blkX * 4 + 4; ++x)
                        pred[y * 8 + x] = dc;
            }
        }
        break;
    }

    case cIntraChromaH:
        // ITU-T H.264 Section 8.3.4.2
        for (uint32_t y = 0U; y < 8U; ++y)
            for (uint32_t x = 0U; x < 8U; ++x)
                pred[y * 8 + x] = left[y];
        break;

    case cIntraChromaV:
        // ITU-T H.264 Section 8.3.4.3
        for (uint32_t y = 0U; y < 8U; ++y)
            for (uint32_t x = 0U; x < 8U; ++x)
                pred[y * 8 + x] = above[x];
        break;

    case cIntraChromaPlane: {
        // ITU-T H.264 Section 8.3.4.4
        int32_t H = 0;
        for (int32_t x = 0; x < 4; ++x)
            H += (x + 1) * (static_cast<int32_t>(above[4 + x]) - static_cast<int32_t>(above[2 - x]));

        int32_t V = 0;
        for (int32_t y = 0; y < 4; ++y)
            V += (y + 1) * (static_cast<int32_t>(left[4 + y]) - static_cast<int32_t>(left[2 - y]));

        int32_t a8 = 16 * (static_cast<int32_t>(above[7]) + static_cast<int32_t>(left[7]));
        int32_t b = (34 * H + 32) >> 6;
        int32_t c = (34 * V + 32) >> 6;

        for (uint32_t y = 0U; y < 8U; ++y) {
            for (uint32_t x = 0U; x < 8U; ++x) {
                int32_t val = (a8 + b * (static_cast<int32_t>(x) - 3) +
                               c * (static_cast<int32_t>(y) - 3) + 16) >> 5;
                pred[y * 8 + x] = static_cast<uint8_t>(std::clamp(val, 0, 255));
            }
        }
        break;
    }

    default:
        for (uint32_t i = 0U; i < 64U; ++i)
            pred[i] = 128U;
        break;
    }
}

} // namespace spec_ref
} // namespace sub0h264

#endif // CROG_SUB0H264_SPEC_REF_INTRA_PRED_HPP
