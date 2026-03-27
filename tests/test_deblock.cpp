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
