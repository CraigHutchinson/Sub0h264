#include "doctest.h"
#include "../components/sub0h264/src/deblock.hpp"

using namespace sub0h264;

TEST_CASE("Deblock: alpha/beta tables valid range")
{
    CHECK(cAlphaTable[0] == 0U);
    CHECK(cAlphaTable[51] == 255U);
    CHECK(cBetaTable[0] == 0U);
    CHECK(cBetaTable[51] == 18U);
}

TEST_CASE("Deblock: tc0 table structure")
{
    // BS=0 column always 0
    for (uint32_t i = 0U; i < 52U; ++i)
        CHECK(cTc0Table[i][0] == 0U);

    // High QP has larger tc0
    CHECK(cTc0Table[51][3] == 25U);
}

TEST_CASE("Deblock: computeBs returns 4 for intra at MB boundary")
{
    CHECK(computeBs(true, false, true, false, false, 0, 0, 0, 0, -1, 0) == 4U);
    CHECK(computeBs(false, true, true, false, false, 0, 0, 0, 0, 0, -1) == 4U);
}

TEST_CASE("Deblock: computeBs returns 3 for intra at internal edge")
{
    CHECK(computeBs(true, false, false, false, false, 0, 0, 0, 0, -1, 0) == 3U);
}

TEST_CASE("Deblock: computeBs returns 2 for non-zero coefficients")
{
    CHECK(computeBs(false, false, true, true, false, 0, 0, 0, 0, 0, 0) == 2U);
    CHECK(computeBs(false, false, false, false, true, 0, 0, 0, 0, 0, 0) == 2U);
}

TEST_CASE("Deblock: computeBs returns 1 for MV difference")
{
    // MV diff >= 4 quarter-pel (1 full pel)
    CHECK(computeBs(false, false, true, false, false, 0, 0, 4, 0, 0, 0) == 1U);
    CHECK(computeBs(false, false, true, false, false, 0, 0, 0, 4, 0, 0) == 1U);
}

TEST_CASE("Deblock: computeBs returns 0 for same inter conditions")
{
    CHECK(computeBs(false, false, true, false, false, 5, 3, 5, 3, 0, 0) == 0U);
}

TEST_CASE("Deblock: weak luma filter no-op below threshold")
{
    // When |p0-q0| >= alpha, no filtering occurs
    uint8_t p0 = 100U, p1 = 100U, p2 = 100U;
    uint8_t q0 = 200U, q1 = 200U, q2 = 200U;
    uint8_t origP0 = p0, origQ0 = q0;

    filterLumaWeak(p0, p1, p2, q0, q1, q2, 10, 10, 5);

    // |p0-q0| = 100 >= alpha=10, so no change
    CHECK(p0 == origP0);
    CHECK(q0 == origQ0);
}

TEST_CASE("Deblock: weak luma filter modifies edge pixels")
{
    // Smooth edge with small difference
    uint8_t p0 = 128U, p1 = 128U, p2 = 128U;
    uint8_t q0 = 132U, q1 = 132U, q2 = 132U;

    filterLumaWeak(p0, p1, p2, q0, q1, q2, 100, 100, 5);

    // Filter should smooth the 128→132 transition
    CHECK(p0 > 128U); // p0 moved towards q0
    CHECK(q0 < 132U); // q0 moved towards p0
}

TEST_CASE("Deblock: strong luma filter smooths intra boundary")
{
    uint8_t p0 = 128U, p1 = 128U, p2 = 128U, p3 = 128U;
    uint8_t q0 = 130U, q1 = 130U, q2 = 130U, q3 = 130U;

    filterLumaStrong(p0, p1, p2, q0, q1, q2, p3, q3, 255, 18);

    // Should smooth transition
    CHECK(p0 >= 128U);
    CHECK(q0 <= 130U);
}

TEST_CASE("Deblock: chroma weak filter")
{
    uint8_t p0 = 128U, p1 = 128U;
    uint8_t q0 = 132U, q1 = 132U;

    filterChromaWeak(p0, p1, q0, q1, 100, 100, 5);

    CHECK(p0 > 128U);
    CHECK(q0 < 132U);
}

// ── Per-MB QP and boundary averaging tests — ITU-T H.264 §8.7.2.2 ──────

TEST_CASE("Deblock: QP averaging at MB boundary produces different thresholds")
{
    // When two adjacent MBs have different QPs, the boundary edge should
    // use (qpP + qpQ + 1) >> 1 per ITU-T H.264 §8.7.2.2.
    // QP=16: alpha=cAlphaTable[16]=4
    // QP=24: alpha=cAlphaTable[24]=13
    // QP_avg = (16+24+1)>>1 = 20: alpha=cAlphaTable[20]=7
    static constexpr int32_t cQpP = 16;
    static constexpr int32_t cQpQ = 24;
    static constexpr int32_t cQpAvg = (cQpP + cQpQ + 1) >> 1; // = 20
    static_assert(cQpAvg == 20, "average QP must be 20");
    CHECK(cAlphaTable[cQpAvg] == 7U);
    CHECK(cAlphaTable[cQpP]   == 4U);
    CHECK(cAlphaTable[cQpQ]   == 12U);  // cAlphaTable[24]=12
    // The averaged index must differ from either single-MB index.
    CHECK(cAlphaTable[cQpAvg] != cAlphaTable[cQpP]);
    CHECK(cAlphaTable[cQpAvg] != cAlphaTable[cQpQ]);
}

TEST_CASE("Deblock: weak filter delta is bounded by tc")
{
    // With BS=1, QP=24: indexA=24, tc0=cTc0Table[24][1]=1.
    // |p0-q0|=4 < alpha=13, |p1-p0|=1 < beta=4, |q1-q0|=1 < beta=4.
    // ap=1 < beta → tc += 1; aq=1 < beta → tc += 1 → tc=3.
    // delta = (((4*4) + (0) + 4) >> 3) = (20>>3) = 2, clamped to ±3 → 2.
    // p0_new = 100+2 = 102; q0_new = 104-2 = 102.
    uint8_t p0 = 100U, p1 = 101U, p2 = 101U;
    uint8_t q0 = 104U, q1 = 103U, q2 = 103U;

    static constexpr int32_t cTestQp = 24;
    static constexpr int32_t cTc0  = 1; // cTc0Table[24][1]
    CHECK(cTc0Table[cTestQp][1] == cTc0);

    filterLumaWeak(p0, p1, p2, q0, q1, q2,
                   cAlphaTable[cTestQp], cBetaTable[cTestQp], cTc0);

    CHECK(p0 == 102U);
    CHECK(q0 == 102U);
}

TEST_CASE("Deblock: strong filter BS=4 averages 4 pixels on each side")
{
    // Verify the exact output of filterLumaStrong for known inputs.
    // §8.7.2.4: with strongThresh and ap < beta:
    //   p0 = (p2 + 2*p1 + 2*p0 + 2*q0 + q1 + 4) >> 3
    //      = (80 + 160 + 160 + 160 + 80 + 4) >> 3 = 644 >> 3 = 80
    //   q0 = (p1 + 2*p0 + 2*q0 + 2*q1 + q2 + 4) >> 3
    //      = (80 + 160 + 160 + 160 + 80 + 4) >> 3 = 644 >> 3 = 80
    uint8_t p0 = 80U, p1 = 80U, p2 = 80U, p3 = 80U;
    uint8_t q0 = 80U, q1 = 80U, q2 = 80U, q3 = 80U;

    // All-same-value: filter must be idempotent
    filterLumaStrong(p0, p1, p2, q0, q1, q2, p3, q3, 255, 18);
    CHECK(p0 == 80U);
    CHECK(q0 == 80U);
}

TEST_CASE("Deblock: strong filter threshold condition §8.7.2.4")
{
    // Strong filter only applies when |p0-q0| < (alpha >> 2) + 2.
    // With alpha=28 (QP=31): threshold = (28>>2)+2 = 9.
    // |p0-q0|=10 >= 9 → strongThresh=false → use weak-strong path:
    //   p0 = (2*p1 + p0 + q1 + 2) >> 2
    //   q0 = (2*q1 + q0 + p1 + 2) >> 2
    static constexpr int32_t cAlpha31 = 28;  // cAlphaTable[31]
    static constexpr int32_t cBeta31  = 8;   // cBetaTable[31]
    CHECK(cAlphaTable[31] == cAlpha31);
    CHECK(cBetaTable[31]  == cBeta31);

    uint8_t p0 = 100U, p1 = 100U, p2 = 100U, p3 = 100U;
    uint8_t q0 = 110U, q1 = 110U, q2 = 110U, q3 = 110U;
    // |p0-q0|=10 >= (28>>2)+2=9 → not strongThresh
    // → p0 = (200 + 100 + 110 + 2) >> 2 = 412>>2 = 103
    // → q0 = (220 + 110 + 100 + 2) >> 2 = 432>>2 = 108
    filterLumaStrong(p0, p1, p2, q0, q1, q2, p3, q3, cAlpha31, cBeta31);
    CHECK(p0 == 103U);
    CHECK(q0 == 108U);
}
