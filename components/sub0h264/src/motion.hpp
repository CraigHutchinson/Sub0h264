/** Sub0h264 — Motion vector prediction and decoding
 *
 *  Computes motion vector predictors from spatial neighbors and
 *  reconstructs final MVs by adding decoded MVDs.
 *
 *  Reference: ITU-T H.264 §8.4.1
 *
 *  SPDX-License-Identifier: MIT
 */
#ifndef CROG_SUB0H264_MOTION_HPP
#define CROG_SUB0H264_MOTION_HPP

#include <cstdint>
#include <algorithm>

namespace sub0h264 {

/// Motion vector (quarter-pel precision) — ITU-T H.264 §8.4.1.3.
struct MotionVector
{
    int16_t x = 0;   ///< Horizontal component (quarter-pel units)
    int16_t y = 0;   ///< Vertical component (quarter-pel units)
};

/// Per-MB motion information stored for neighbor context.
struct MbMotionInfo
{
    MotionVector mv;          ///< Motion vector (L0)
    int8_t refIdx = -1;       ///< Reference frame index (-1 = intra/unavailable)
    bool available = false;   ///< True if this MB/partition is available
};

/** Compute the median of three signed values.
 *  Used for MV predictor computation — ITU-T H.264 §8.4.1.3.1.
 */
inline int16_t median3(int16_t a, int16_t b, int16_t c) noexcept
{
    int16_t minAB = std::min(a, b);
    int16_t maxAB = std::max(a, b);
    return std::max(minAB, std::min(maxAB, c));
}

/** Compute the MV predictor from three spatial neighbors.
 *
 *  @param left     Left neighbor motion info (partition A)
 *  @param top      Top neighbor motion info (partition B)
 *  @param topRight Top-right neighbor motion info (partition C, or top-left D)
 *  @param refIdx   Reference index of the current partition
 *  @return Predicted motion vector
 *
 *  Reference: ITU-T H.264 §8.4.1.3.1
 */
inline MotionVector computeMvPredictor(const MbMotionInfo& left,
                                        const MbMotionInfo& top,
                                        const MbMotionInfo& topRight,
                                        int8_t refIdx) noexcept
{
    // Count available neighbors
    uint32_t availCount = 0U;
    uint32_t matchCount = 0U;
    int32_t lastMatchIdx = -1;

    if (left.available)      { ++availCount; if (left.refIdx == refIdx) { ++matchCount; lastMatchIdx = 0; } }
    if (top.available)       { ++availCount; if (top.refIdx == refIdx) { ++matchCount; lastMatchIdx = 1; } }
    if (topRight.available)  { ++availCount; if (topRight.refIdx == refIdx) { ++matchCount; lastMatchIdx = 2; } }

    // §8.4.1.3: If B is not available, set B = A (both MV and refIdx).
    // If C is not available, set C = A. This happens BEFORE ref_idx matching.
    MbMotionInfo effA = left;
    MbMotionInfo effB = top.available ? top : left;
    MbMotionInfo effC = topRight.available ? topRight : left;

    // Recount matches with effective neighbors
    matchCount = 0U;
    lastMatchIdx = -1;
    if (effA.available && effA.refIdx == refIdx) { ++matchCount; lastMatchIdx = 0; }
    if (effB.available && effB.refIdx == refIdx) { ++matchCount; lastMatchIdx = 1; }
    if (effC.available && effC.refIdx == refIdx) { ++matchCount; lastMatchIdx = 2; }

    const MbMotionInfo* effNeighbors[3] = { &effA, &effB, &effC };

    // §8.4.1.3: If exactly one has matching ref_idx → use that MV.
    if (matchCount == 1U)
        return effNeighbors[lastMatchIdx]->mv;

    // Default: median of all three effective MVs.
    int16_t mvA_x = effA.available ? effA.mv.x : 0;
    int16_t mvA_y = effA.available ? effA.mv.y : 0;
    int16_t mvB_x = effB.available ? effB.mv.x : 0;
    int16_t mvB_y = effB.available ? effB.mv.y : 0;
    int16_t mvC_x = effC.available ? effC.mv.x : 0;
    int16_t mvC_y = effC.available ? effC.mv.y : 0;

    return { median3(mvA_x, mvB_x, mvC_x), median3(mvA_y, mvB_y, mvC_y) };
}

} // namespace sub0h264

#endif // CROG_SUB0H264_MOTION_HPP
