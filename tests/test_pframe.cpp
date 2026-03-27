#include "doctest.h"
#include "../components/sub0h264/src/motion.hpp"
#include "../components/sub0h264/src/inter_pred.hpp"
#include "../components/sub0h264/src/dpb.hpp"

using namespace sub0h264;

// ── Motion vector prediction tests ──────────────────────────────────────

TEST_CASE("MV: median3 computes median of three values")
{
    CHECK(median3(1, 2, 3) == 2);
    CHECK(median3(3, 1, 2) == 2);
    CHECK(median3(-5, 0, 5) == 0);
    CHECK(median3(10, 10, 10) == 10);
    CHECK(median3(-1, -2, -3) == -2);
}

TEST_CASE("MV: predictor with all neighbors available, no ref match")
{
    MbMotionInfo left  = { {4, 8}, 0, true };
    MbMotionInfo top   = { {2, 6}, 0, true };
    MbMotionInfo topR  = { {6, 2}, 0, true };

    MotionVector mvp = computeMvPredictor(left, top, topR, 0);
    // Median: x=median(4,2,6)=4, y=median(8,6,2)=6
    CHECK(mvp.x == 4);
    CHECK(mvp.y == 6);
}

TEST_CASE("MV: predictor with single neighbor matching ref")
{
    MbMotionInfo left  = { {10, 20}, 1, true };  // ref=1
    MbMotionInfo top   = { {5, 15}, 0, true };    // ref=0 ← match
    MbMotionInfo topR  = { {8, 12}, 2, true };    // ref=2

    MotionVector mvp = computeMvPredictor(left, top, topR, 0);
    // Only top matches ref=0 → use top's MV directly
    CHECK(mvp.x == 5);
    CHECK(mvp.y == 15);
}

TEST_CASE("MV: predictor with only one neighbor available")
{
    MbMotionInfo left  = { {7, 3}, 0, true };
    MbMotionInfo top   = { {0, 0}, -1, false };
    MbMotionInfo topR  = { {0, 0}, -1, false };

    MotionVector mvp = computeMvPredictor(left, top, topR, 0);
    CHECK(mvp.x == 7);
    CHECK(mvp.y == 3);
}

// ── Inter prediction tests ──────────────────────────────────────────────

TEST_CASE("InterPred: full-pel luma copy")
{
    Frame ref;
    ref.allocate(16U, 16U);
    // Fill with a gradient pattern
    for (uint32_t y = 0U; y < 16U; ++y)
        for (uint32_t x = 0U; x < 16U; ++x)
            ref.y(x, y) = static_cast<uint8_t>((x + y) * 8U);

    uint8_t dst[64];
    lumaMotionComp(ref, 4, 4, 0U, 0U, 8U, 8U, dst, 8U);

    // Should be exact copy of ref at (4,4)
    CHECK(dst[0] == ref.y(4U, 4U));
    CHECK(dst[1] == ref.y(5U, 4U));
    CHECK(dst[8] == ref.y(4U, 5U));
}

TEST_CASE("InterPred: horizontal half-pel uses 6-tap filter")
{
    Frame ref;
    ref.allocate(16U, 16U);
    ref.fill(100U, 128U, 128U);

    uint8_t dst[16];
    lumaMotionComp(ref, 4, 4, 2U, 0U, 4U, 4U, dst, 4U);

    // Constant input → 6-tap filter should output same value
    // (1-5+20+20-5+1)*100/32 = 32*100/32 = 100
    CHECK(dst[0] == 100U);
}

TEST_CASE("InterPred: chroma bilinear full-pel")
{
    Frame ref;
    ref.allocate(16U, 16U);
    ref.fill(0U, 200U, 50U);

    uint8_t dst[16];
    chromaMotionComp(ref, 2, 2, 0U, 0U, 4U, 4U, true, dst, 4U);

    // Full-pel: weights = (8*8, 0, 0, 0)/64 = 1.0 * A → exact copy
    CHECK(dst[0] == 200U);
}

// ── DPB tests ───────────────────────────────────────────────────────────

TEST_CASE("DPB: init and get decode target")
{
    Dpb dpb;
    dpb.init(640U, 480U, 3U);

    Frame* target = dpb.getDecodeTarget();
    REQUIRE(target != nullptr);
    CHECK(target->width() == 640U);
    CHECK(target->height() == 480U);
}

TEST_CASE("DPB: mark reference and retrieve")
{
    Dpb dpb;
    dpb.init(16U, 16U, 3U);

    Frame* f = dpb.getDecodeTarget();
    f->fill(42U, 0U, 0U);
    dpb.markAsReference(0U);

    const Frame* ref = dpb.getReference(0U);
    REQUIRE(ref != nullptr);
    CHECK(ref->y(0U, 0U) == 42U);
}

TEST_CASE("DPB: flush clears all references")
{
    Dpb dpb;
    dpb.init(16U, 16U, 3U);

    dpb.getDecodeTarget();
    dpb.markAsReference(0U);
    CHECK(dpb.numReferences() == 1U);

    dpb.flush();
    CHECK(dpb.numReferences() == 0U);
}

TEST_CASE("DPB: multiple references sorted by frameNum descending")
{
    Dpb dpb;
    dpb.init(16U, 16U, 4U);

    // Decode 3 frames
    Frame* f1 = dpb.getDecodeTarget();
    f1->fill(10U, 0U, 0U);
    dpb.markAsReference(1U);

    Frame* f2 = dpb.getDecodeTarget();
    f2->fill(20U, 0U, 0U);
    dpb.markAsReference(2U);

    Frame* f3 = dpb.getDecodeTarget();
    f3->fill(30U, 0U, 0U);
    dpb.markAsReference(3U);

    CHECK(dpb.numReferences() == 3U);

    // Ref index 0 should be most recent (frameNum=3)
    const Frame* ref0 = dpb.getReference(0U);
    REQUIRE(ref0 != nullptr);
    CHECK(ref0->y(0U, 0U) == 30U);

    // Ref index 2 should be oldest (frameNum=1)
    const Frame* ref2 = dpb.getReference(2U);
    REQUIRE(ref2 != nullptr);
    CHECK(ref2->y(0U, 0U) == 10U);
}
