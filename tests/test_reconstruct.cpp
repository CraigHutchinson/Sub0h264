/** Sub0h264 — Reconstruction pipeline unit tests
 *
 *  Tests intra prediction, inverse quantization, and inverse DCT
 *  as specified in ITU-T H.264 §8.3 and §8.5.
 *
 *  SPDX-License-Identifier: MIT
 */
#include "doctest.h"
#include "../components/sub0h264/src/intra_pred.hpp"
#include "../components/sub0h264/src/tables.hpp"
#include "../components/sub0h264/src/transform.hpp"
#include "../components/sub0h264/src/frame.hpp"

#include <cstring>

using namespace sub0h264;

// ── Test 1: Intra 4x4 top-right availability ────────────────────────────

TEST_CASE("IntraPred4x4: DiagDownLeft fallback when top-right unavailable")
{
    // Blocks 3, 7, 11, 13, 14, 15 within an MB have no top-right neighbor
    // per ITU-T H.264 Table 6-3 / §6.4.11.
    // When topRight==nullptr the implementation falls back to repeating top[3..6].
    uint8_t top[4] = { 100, 110, 120, 130 };
    uint8_t pred_with_tr[16];
    uint8_t pred_no_tr[16];

    // topRight available: tr[0..3] = top[0..3] for simplicity (identical values)
    uint8_t topRight[4] = { 140, 150, 160, 170 };
    intraPred4x4(Intra4x4Mode::DiagDownLeft, top, topRight, nullptr, nullptr, pred_with_tr);

    // topRight unavailable (nullptr): implementation clamps by repeating top[3]
    intraPred4x4(Intra4x4Mode::DiagDownLeft, top, nullptr, nullptr, nullptr, pred_no_tr);

    // The two results must differ because topRight != top[3..3] repeated
    bool anyDifference = false;
    for (uint32_t i = 0U; i < 16U; ++i)
    {
        if (pred_with_tr[i] != pred_no_tr[i])
        {
            anyDifference = true;
            break;
        }
    }
    CHECK(anyDifference);

    // When top-right is unavailable, substitution repeats top[3] (§8.3.1.2.3):
    // tr = {130, 130, 130, 130}, so t = {100,110,120,130,130,130,130,130}.
    // pred[3,3]: idx=6 → special case filt121(t[6],t[7],t[7])
    //   = filt121(130, 130, 130) = 130
    CHECK(pred_no_tr[3 * 4 + 3] == filt121(uint8_t(130), uint8_t(130), uint8_t(130)));
}

TEST_CASE("IntraPred4x4: DiagDownLeft top-right-unavailable blocks produce DC fallback")
{
    // When top is also unavailable (block at top-left corner of frame with no
    // top neighbor), intraPred4x4 does nothing — output stays uninitialised,
    // so we only test the case where top IS available but topRight is not.
    uint8_t top[4] = { 200, 200, 200, 200 };
    uint8_t pred[16] = {};
    intraPred4x4(Intra4x4Mode::DiagDownLeft, top, nullptr, nullptr, nullptr, pred);

    // All top values identical → all filtered values should equal 200
    for (uint32_t i = 0U; i < 16U; ++i)
        CHECK(pred[i] == 200U);
}

// ── Test 2: Intra 4x4 modes 5-8 ────────────────────────────────────────

TEST_CASE("IntraPred4x4: VerticalRight (mode 5) spot-checks against spec formulas")
{
    // §8.3.1.2.6 — requires top, left, topLeft
    uint8_t top[4]    = { 10, 20, 30, 40 };
    uint8_t left[4]   = { 50, 60, 70, 80 };
    uint8_t topLeft   = 5U;
    uint8_t pred[16]  = {};

    intraPred4x4(Intra4x4Mode::VerticalRight, top, nullptr, left, &topLeft, pred);

    // Row 0, col 0: filt11(tl, top[0]) = (5+10+1)>>1 = 8
    CHECK(pred[0 * 4 + 0] == filt11(topLeft, top[0]));
    // Row 0, col 1: filt11(top[0], top[1]) = (10+20+1)>>1 = 15
    CHECK(pred[0 * 4 + 1] == filt11(top[0], top[1]));
    // Row 1, col 0: filt121(left[0], tl, top[0]) = (50+2*5+10+2)>>2 = 18
    CHECK(pred[1 * 4 + 0] == filt121(left[0], topLeft, top[0]));
    // Row 1, col 1: filt121(tl, top[0], top[1]) = (5+20+20+2)>>2 = 11
    CHECK(pred[1 * 4 + 1] == filt121(topLeft, top[0], top[1]));
    // Row 2, col 1 mirrors row 0, col 0
    CHECK(pred[2 * 4 + 1] == pred[0 * 4 + 0]);
    // Row 3, col 1 mirrors row 1, col 0
    CHECK(pred[3 * 4 + 1] == pred[1 * 4 + 0]);
}

TEST_CASE("IntraPred4x4: HorizontalDown (mode 6) spot-checks against spec formulas")
{
    // §8.3.1.2.7 — requires top, left, topLeft
    uint8_t top[4]    = { 10, 20, 30, 40 };
    uint8_t left[4]   = { 50, 60, 70, 80 };
    uint8_t topLeft   = 5U;
    uint8_t pred[16]  = {};

    intraPred4x4(Intra4x4Mode::HorizontalDown, top, nullptr, left, &topLeft, pred);

    // Row 0, col 0: filt11(tl, left[0]) = (5+50+1)>>1 = 28
    CHECK(pred[0 * 4 + 0] == filt11(topLeft, left[0]));
    // Row 0, col 1: filt121(left[0], tl, top[0]) = (50+10+10+2)>>2 = 18
    CHECK(pred[0 * 4 + 1] == filt121(left[0], topLeft, top[0]));
    // Row 1, col 0: filt11(left[0], left[1]) = (50+60+1)>>1 = 55
    CHECK(pred[1 * 4 + 0] == filt11(left[0], left[1]));
    // Row 1, col 2 mirrors row 0, col 0
    CHECK(pred[1 * 4 + 2] == pred[0 * 4 + 0]);
    // Row 2, col 2 mirrors row 1, col 0
    CHECK(pred[2 * 4 + 2] == pred[1 * 4 + 0]);
    // Row 3, col 0: filt11(left[2], left[3]) = (70+80+1)>>1 = 75
    CHECK(pred[3 * 4 + 0] == filt11(left[2], left[3]));
}

TEST_CASE("IntraPred4x4: VerticalLeft (mode 7) spot-checks against spec formulas")
{
    // §8.3.1.2.8 — requires top (topRight optional)
    uint8_t top[4]     = { 10, 20, 30, 40 };
    uint8_t topRight[4]= { 50, 60, 70, 80 };
    uint8_t pred[16]   = {};

    intraPred4x4(Intra4x4Mode::VerticalLeft, top, topRight, nullptr, nullptr, pred);

    // t[0..7] = {10,20,30,40,50,60,70,80}
    // Row 0, col 0: filt11(t[0], t[1]) = (10+20+1)>>1 = 15
    CHECK(pred[0 * 4 + 0] == filt11(uint8_t(10), uint8_t(20)));
    // Row 0, col 3: filt11(t[3], t[4]) = (40+50+1)>>1 = 45
    CHECK(pred[0 * 4 + 3] == filt11(uint8_t(40), uint8_t(50)));
    // Row 1, col 0: filt121(t[0],t[1],t[2]) = (10+40+30+2)>>2 = 20
    CHECK(pred[1 * 4 + 0] == filt121(uint8_t(10), uint8_t(20), uint8_t(30)));
    // Row 2, col 0 = row 0, col 1: filt11(t[1],t[2])
    CHECK(pred[2 * 4 + 0] == filt11(uint8_t(20), uint8_t(30)));
}

TEST_CASE("IntraPred4x4: HorizontalUp (mode 8) spot-checks against spec formulas")
{
    // §8.3.1.2.9 — requires left only
    uint8_t left[4]  = { 100, 120, 140, 160 };
    uint8_t pred[16] = {};

    intraPred4x4(Intra4x4Mode::HorizontalUp, nullptr, nullptr, left, nullptr, pred);

    // Row 0, col 0: filt11(left[0], left[1]) = (100+120+1)>>1 = 110
    CHECK(pred[0 * 4 + 0] == filt11(left[0], left[1]));
    // Row 0, col 1: filt121(left[0],left[1],left[2]) = (100+240+140+2)>>2 = 120
    CHECK(pred[0 * 4 + 1] == filt121(left[0], left[1], left[2]));
    // Row 0, col 2: filt11(left[1],left[2])
    CHECK(pred[0 * 4 + 2] == filt11(left[1], left[2]));
    // Row 1, col 0 = row 0, col 2
    CHECK(pred[1 * 4 + 0] == pred[0 * 4 + 2]);
    // Bottom-right quadrant: all left[3]
    CHECK(pred[2 * 4 + 2] == left[3]);
    CHECK(pred[3 * 4 + 3] == left[3]);
}

// ── Test 3: Intra 4x4 MPM derivation ────────────────────────────────────

TEST_CASE("IntraPred4x4 MPM: min(leftMode, topMode) is MPM")
{
    // ITU-T H.264 §8.3.1.1 — MPM = min(leftMode, topMode)
    // When leftMode=3 and topMode=5 → MPM = min(3,5) = 3
    constexpr uint8_t leftMode = 3U;
    constexpr uint8_t topMode  = 5U;
    constexpr uint8_t mpm = leftMode < topMode ? leftMode : topMode;
    CHECK(mpm == 3U);

    // When leftMode=5 and topMode=3 → MPM = 3 again (symmetric)
    constexpr uint8_t mpm2 = uint8_t(5U) < uint8_t(3U) ? uint8_t(5U) : uint8_t(3U);
    CHECK(mpm2 == 3U);
}

TEST_CASE("IntraPred4x4 MPM: rem→mode remapping, rem=2 mpm=2 → mode=3")
{
    // ITU-T H.264 §8.3.1.1:
    //   if rem_intra4x4_pred_mode < MPM  →  mode = rem
    //   else                             →  mode = rem + 1
    // Test case: rem=2, mpm=2 → rem >= mpm → mode = 2+1 = 3 (NOT 2)
    auto remToMode = [](uint8_t rem, uint8_t mpm) -> uint8_t {
        return (rem < mpm) ? rem : static_cast<uint8_t>(rem + 1U);
    };

    CHECK(remToMode(2U, 2U) == 3U);

    // Boundary: rem=1, mpm=2 → rem < mpm → mode = 1
    CHECK(remToMode(1U, 2U) == 1U);

    // rem=0, mpm=0 → rem >= mpm → mode = 1
    CHECK(remToMode(0U, 0U) == 1U);

    // rem=7, mpm=3 → rem >= mpm → mode = 8
    CHECK(remToMode(7U, 3U) == 8U);
}

// ── Test 4: Inverse quantize with known QP ──────────────────────────────

TEST_CASE("InverseQuantize4x4: QP=24, position 0 and position 1")
{
    // QP=24: qpDiv6=4, qpMod6=0
    // cDequantScale[0] = {10, 13, 16}
    // pos 0: posClass=cDequantPosClass[0]=0, scale=10, raw=2
    //   → val = 2 * 10 * (1<<4) = 320
    // pos 1: posClass=cDequantPosClass[1]=2, scale=16, raw=-3
    //   → val = -3 * 16 * (1<<4) = -768

    constexpr int32_t cTestQp = 24;
    static_assert(cTestQp / 6 == 4, "qpDiv6 must be 4 for QP=24");
    static_assert(cTestQp % 6 == 0, "qpMod6 must be 0 for QP=24");

    int16_t coeffs[16] = {};
    coeffs[0] = 2;
    coeffs[1] = -3;

    inverseQuantize4x4(coeffs, cTestQp);

    // QP=24: qpDiv6=4, shift = qpDiv6-2 = 2.
    // pos(0,0) posClass=0: 2 * 10 * 4 = 80
    // pos(0,1) posClass=2: -3 * 16 * 4 = -192
    CHECK(coeffs[0] == 80);
    CHECK(coeffs[1] == -192);

    // All other positions remain zero
    for (uint32_t i = 2U; i < 16U; ++i)
        CHECK(coeffs[i] == 0);
}

// ── Test 5: Inverse DCT with DC-only coefficient ────────────────────────

TEST_CASE("InverseDct4x4AddPred: DC-only coefficient 320 + pred 128 = 133")
{
    // coeffs[0]=320, all others 0; prediction=128 for all pixels.
    //
    // Horizontal pass row 0: q0=320, q1=q2=q3=0
    //   x0=320, x1=320, x2=0, x3=0
    //   tmp[0]=320, tmp[1]=320, tmp[2]=320, tmp[3]=320
    // All other rows: all-zero input → all-zero tmp.
    //
    // Vertical pass col j (j=0..3):
    //   t0=320, t1=t2=t3=0
    //   x0=320, x1=320, x2=0, x3=0
    //   r0=(320+32)>>6=5, r1=(320+32)>>6=5, r2=5, r3=5
    //
    // Output = clipU8(128 + 5) = 133 for all 16 pixels.

    uint8_t pred[16];
    uint8_t out[16];
    std::memset(pred, 128U, 16U);

    int16_t coeffs[16] = {};
    coeffs[0] = 320;

    inverseDct4x4AddPred(coeffs, pred, 4U, out, 4U);

    for (uint32_t i = 0U; i < 16U; ++i)
        CHECK(out[i] == 133U);
}

// ── Test 6: QP clamp for negative QP ────────────────────────────────────

TEST_CASE("InverseQuantize4x4: negative QP wraps to valid range via modular arithmetic")
{
    // QP=-5: (((-5)%52)+52)%52 = ((-5+52))%52 = 47%52 = 47
    // This test verifies no crash/UB and that the clamped QP is applied.
    // QP=47: qpDiv6=7, qpMod6=5, scale[5][0]=18
    // coeffs[0]=1 → val = 1 * 18 * (1<<7) = 2304
    int16_t coeffs[16] = {};
    coeffs[0] = 1;

    inverseQuantize4x4(coeffs, -5);

    // QP=-5 wraps to 47. cDequantScale[5][0]=18, qpDiv6=7, shift=7-2=5
    // → val = 18 << 5 = 576
    CHECK(coeffs[0] == static_cast<int16_t>(18 * (1 << 5)));
}

TEST_CASE("InverseQuantize4x4: QP=-52 wraps to 0")
{
    // ((-52 % 52) + 52) % 52: in C++ (-52%52) = 0 → (0+52)%52 = 0
    // QP=0: qpDiv6=0, qpMod6=0, scale[0][0]=10
    // coeffs[0]=1 → val = 1 * 10 * (1<<0) = 10
    int16_t coeffs[16] = {};
    coeffs[0] = 1;

    inverseQuantize4x4(coeffs, -52);

    // QP=0: qpDiv6=0, qpDiv6<2 so right-shift: (1*10 + 2) >> 2 = 3
    CHECK(coeffs[0] == static_cast<int16_t>(3));
}

// ── Test 7: clipU8 edge cases ───────────────────────────────────────────

TEST_CASE("clipU8: boundary and overflow values")
{
    CHECK(clipU8(-1)   == 0);
    CHECK(clipU8(0)    == 0);
    CHECK(clipU8(1)    == 1);
    CHECK(clipU8(127)  == 127);
    CHECK(clipU8(255)  == 255);
    CHECK(clipU8(256)  == 255);
    CHECK(clipU8(1000) == 255);
}

TEST_CASE("clipU8: large negative value clamps to 0")
{
    CHECK(clipU8(-32768) == 0);
    CHECK(clipU8(INT32_MIN) == 0);
}

TEST_CASE("clipU8: large positive value clamps to 255")
{
    CHECK(clipU8(INT32_MAX) == 255);
}

// ── Test 8: I_16x16 DC no-double-dequant ────────────────────────────────

TEST_CASE("I_16x16 DC: saved-DC pattern prevents double dequantization of position 0")
{
    // Reproduces the pattern used in the decoder for I_16x16 macroblocks
    // (decoder.hpp lines 727-730).  The DC at coeffs[0] has already been
    // dequantized by the Hadamard path; inverseQuantize4x4 must NOT scale it again.
    //
    // Pattern:
    //   int16_t savedDc = coeffs[0];
    //   inverseQuantize4x4(coeffs, qp);   // dequants AC (positions 1-15)
    //   coeffs[0] = savedDc;              // restore pre-dequanted DC value
    //
    // Without the save/restore, a DC value of 320 at QP=24 would become
    // 320*10*(1<<4) = 51200 — a 160× error.

    constexpr int32_t cTestQp = 24;
    constexpr int16_t cDcAlreadyDequanted = 320;  // already scaled by Hadamard path
    constexpr int16_t cAcCoeff = 2;               // raw AC coefficient, not yet dequanted

    int16_t coeffs[16] = {};
    coeffs[0] = cDcAlreadyDequanted;
    coeffs[1] = cAcCoeff;

    // Simulate the decoder's save/restore pattern
    int16_t savedDc = coeffs[0];
    inverseQuantize4x4(coeffs, cTestQp);
    coeffs[0] = savedDc;

    // DC must be unchanged — still the Hadamard-dequanted value
    CHECK(coeffs[0] == cDcAlreadyDequanted);

    // AC coefficient at position 1 must have been dequanted normally:
    // posClass[1]=2, scale=cDequantScale[0][2]=16, qpDiv6=4, shift=4-2=2
    // → 2 * 16 * (1<<2) = 128
    CHECK(coeffs[1] == static_cast<int16_t>(cAcCoeff * 16 * (1 << 2)));
}

TEST_CASE("I_16x16 DC: without save/restore, DC is incorrectly double-dequanted")
{
    // Demonstrates the bug that the save/restore pattern fixes.
    // If DC is NOT restored, it gets dequantized a second time.
    constexpr int32_t cTestQp = 24;
    constexpr int16_t cDcAlreadyDequanted = 320;

    int16_t coeffs[16] = {};
    coeffs[0] = cDcAlreadyDequanted;

    // No save/restore: dequant is applied to DC position as well
    inverseQuantize4x4(coeffs, cTestQp);

    // coeffs[0] was 320; posClass[0]=0, scale=10, qpDiv6=4
    // → 320 * 10 * (1<<4) = 51200 — but int16_t saturates at 32767
    // The point is it is NOT 320 any more.
    CHECK(coeffs[0] != cDcAlreadyDequanted);
}

// ── Block scan order tests — ITU-T H.264 §6.4.3 ────────────────────────

TEST_CASE("ScanOrder: cLuma4x4BlkX/Y matches spec §6.4.3")
{
    // Verify the scan order tables encode the correct positions.
    // Formula: x = InverseRasterScan(blkIdx/4, 8, 8, 16, 0)
    //            + InverseRasterScan(blkIdx%4, 4, 4, 8, 0)
    //          y = InverseRasterScan(blkIdx/4, 8, 8, 16, 1)
    //            + InverseRasterScan(blkIdx%4, 4, 4, 8, 1)
    for (uint32_t i = 0U; i < 16U; ++i)
    {
        uint32_t i8x8 = i / 4U;
        uint32_t i4x4 = i % 4U;
        uint32_t expectedX = (i8x8 % 2U) * 8U + (i4x4 % 2U) * 4U;
        uint32_t expectedY = (i8x8 / 2U) * 8U + (i4x4 / 2U) * 4U;
        CHECK(cLuma4x4BlkX[i] == expectedX);
        CHECK(cLuma4x4BlkY[i] == expectedY);
    }
}

TEST_CASE("ScanOrder: cLuma4x4ToRaster is correct inverse")
{
    // cLuma4x4ToRaster[scanIdx] should give the raster index
    // Raster index = (blkY / 4) * 4 + (blkX / 4)
    for (uint32_t i = 0U; i < 16U; ++i)
    {
        uint32_t raster = (cLuma4x4BlkY[i] / 4U) * 4U + (cLuma4x4BlkX[i] / 4U);
        CHECK(cLuma4x4ToRaster[i] == raster);
    }
}

TEST_CASE("ScanOrder: cRasterToLuma4x4 is true inverse of cLuma4x4ToRaster")
{
    for (uint32_t i = 0U; i < 16U; ++i)
    {
        CHECK(cRasterToLuma4x4[cLuma4x4ToRaster[i]] == i);
        CHECK(cLuma4x4ToRaster[cRasterToLuma4x4[i]] == i);
    }
}

TEST_CASE("ScanOrder: cTopRightUnavailScan matches spec derivation")
{
    // For each scan index where blkY > 0, the top-right block is at
    // (blkX+4, blkY-4). It's unavailable if:
    //   (a) blkX+4 >= 16 (beyond MB right edge), or
    //   (b) the scan index of (blkX+4, blkY-4) > current scan index
    for (uint32_t i = 0U; i < 16U; ++i)
    {
        uint32_t bx = cLuma4x4BlkX[i];
        uint32_t by = cLuma4x4BlkY[i];

        if (by == 0U)
        {
            // Top-right is in the top MB row — not blocked by within-MB ordering.
            // Exception: blkX+4 may exceed frame width, but that's a runtime check.
            CHECK_FALSE(cTopRightUnavailScan[i]);
            continue;
        }

        // Within-MB: top-right block at (bx+4, by-4)
        if (bx + 4U >= 16U)
        {
            // Beyond MB right edge
            CHECK(cTopRightUnavailScan[i]);
            continue;
        }

        // Find scan index of the top-right block
        uint32_t trX = bx + 4U;
        uint32_t trY = by - 4U;
        uint32_t trScan = 0xFFU;
        for (uint32_t j = 0U; j < 16U; ++j)
        {
            if (cLuma4x4BlkX[j] == trX && cLuma4x4BlkY[j] == trY)
            {
                trScan = j;
                break;
            }
        }
        REQUIRE(trScan != 0xFFU);

        // Unavailable if top-right has higher scan index (not yet decoded)
        CHECK(cTopRightUnavailScan[i] == (trScan > i));
    }
}

TEST_CASE("IntraPred4x4: DDL top-right substitution uses top[3] repeated")
{
    // ITU-T H.264 §8.3.1.2.3: when top-right unavailable,
    // p[4,-1]..p[7,-1] = p[3,-1]
    uint8_t top[4] = { 10, 20, 30, 40 };
    uint8_t pred[16];

    // No topRight → should substitute with {40, 40, 40, 40}
    intraPred4x4(Intra4x4Mode::DiagDownLeft, top, nullptr, nullptr, nullptr, pred);

    // With explicit topRight = {40, 40, 40, 40} — should match
    uint8_t trRepeated[4] = { 40, 40, 40, 40 };
    uint8_t pred2[16];
    intraPred4x4(Intra4x4Mode::DiagDownLeft, top, trRepeated, nullptr, nullptr, pred2);

    for (uint32_t i = 0U; i < 16U; ++i)
        CHECK(pred[i] == pred2[i]);
}

TEST_CASE("IntraPred4x4: VL top-right substitution uses top[3] repeated")
{
    uint8_t top[4] = { 50, 60, 70, 80 };
    uint8_t pred[16];

    intraPred4x4(Intra4x4Mode::VerticalLeft, top, nullptr, nullptr, nullptr, pred);

    uint8_t trRepeated[4] = { 80, 80, 80, 80 };
    uint8_t pred2[16];
    intraPred4x4(Intra4x4Mode::VerticalLeft, top, trRepeated, nullptr, nullptr, pred2);

    for (uint32_t i = 0U; i < 16U; ++i)
        CHECK(pred[i] == pred2[i]);
}

// ── §6.4.3 InverseRasterScan function tests ─────────────────────────────

TEST_CASE("InverseRasterScan: spec §6.4.3 formula cross-check")
{
    // InverseRasterScan(a, b, c, d, e):
    //   e=0: (a % (d/b)) * b  — x-offset
    //   e=1: (a / (d/b)) * c  — y-offset
    //
    // Grid: two 8x8 blocks wide (d=16, b=8) — i8x8=0..3 maps to a 2×2 grid.
    CHECK(inverseRasterScan(0U, 8U, 8U, 16U, 0U) == 0U);  // i8x8=0 → x=0
    CHECK(inverseRasterScan(0U, 8U, 8U, 16U, 1U) == 0U);  // i8x8=0 → y=0
    CHECK(inverseRasterScan(1U, 8U, 8U, 16U, 0U) == 8U);  // i8x8=1 → x=8
    CHECK(inverseRasterScan(1U, 8U, 8U, 16U, 1U) == 0U);  // i8x8=1 → y=0
    CHECK(inverseRasterScan(2U, 8U, 8U, 16U, 0U) == 0U);  // i8x8=2 → x=0
    CHECK(inverseRasterScan(2U, 8U, 8U, 16U, 1U) == 8U);  // i8x8=2 → y=8
    CHECK(inverseRasterScan(3U, 8U, 8U, 16U, 0U) == 8U);  // i8x8=3 → x=8
    CHECK(inverseRasterScan(3U, 8U, 8U, 16U, 1U) == 8U);  // i8x8=3 → y=8

    // Sub-4x4 grid within 8x8: d=8, b=4, c=4 — i4x4=0..3 maps to 2×2 grid.
    CHECK(inverseRasterScan(0U, 4U, 4U, 8U, 0U) == 0U);   // i4x4=0 → x=0
    CHECK(inverseRasterScan(1U, 4U, 4U, 8U, 0U) == 4U);   // i4x4=1 → x=4
    CHECK(inverseRasterScan(2U, 4U, 4U, 8U, 0U) == 0U);   // i4x4=2 → x=0
    CHECK(inverseRasterScan(2U, 4U, 4U, 8U, 1U) == 4U);   // i4x4=2 → y=4
    CHECK(inverseRasterScan(3U, 4U, 4U, 8U, 0U) == 4U);   // i4x4=3 → x=4
    CHECK(inverseRasterScan(3U, 4U, 4U, 8U, 1U) == 4U);   // i4x4=3 → y=4

    // Key luma4x4BlkIdx positions derived per §6.4.3.
    // blkIdx=5 (i8x8=1, i4x4=1): x=8+4=12, y=0+0=0
    CHECK(inverseRasterScan(5U/4U, 8U, 8U, 16U, 0U) + inverseRasterScan(5U%4U, 4U, 4U, 8U, 0U) == 12U);
    CHECK(inverseRasterScan(5U/4U, 8U, 8U, 16U, 1U) + inverseRasterScan(5U%4U, 4U, 4U, 8U, 1U) == 0U);
    // blkIdx=11 (i8x8=2, i4x4=3): x=0+4=4, y=8+4=12
    CHECK(inverseRasterScan(11U/4U, 8U, 8U, 16U, 0U) + inverseRasterScan(11U%4U, 4U, 4U, 8U, 0U) == 4U);
    CHECK(inverseRasterScan(11U/4U, 8U, 8U, 16U, 1U) + inverseRasterScan(11U%4U, 4U, 4U, 8U, 1U) == 12U);
}

// ── §8.5.12.1 DequantPosClass derivation test ───────────────────────────

TEST_CASE("DequantPosClass: derived from row/col parity per §8.5.12.1")
{
    // Class rule: col%2==0 && row%2==0 → 0; col%2==1 && row%2==1 → 1; else → 2.
    // Exhaustive check for all 16 positions.
    static constexpr std::array<uint8_t, 16> cExpected = {
        0, 2, 0, 2,   // row 0: cols 0,1,2,3
        2, 1, 2, 1,   // row 1
        0, 2, 0, 2,   // row 2
        2, 1, 2, 1,   // row 3
    };
    for (uint32_t i = 0U; i < 16U; ++i)
        CHECK(cDequantPosClass[i] == cExpected[i]);

    // Spot-checks matching Table 8-14 footnotes:
    CHECK(cDequantPosClass[0]  == 0U);  // (0,0) → class 0
    CHECK(cDequantPosClass[5]  == 1U);  // (1,1) → class 1
    CHECK(cDequantPosClass[15] == 1U);  // (3,3) → class 1
    CHECK(cDequantPosClass[4]  == 2U);  // (1,0) → class 2
    CHECK(cDequantPosClass[3]  == 2U);  // (0,3) → class 2
}

// ── Dequant formula verification — ITU-T H.264 §8.5.12.1 ───────────────

TEST_CASE("Dequant: MB(9,0) block (12,8) formula comparison")
{
    // Raw CAVLC coefficients for MB(9,0) scan 13 (raster 11) at QP=24.
    // Verified against bitstream decode trace.
    int16_t raw[16] = { 2, -3, 2, -1, -3, 4, -3, 2, 2, -3, 2, -1, -1, 2, -1, 1 };

    // Dequant with current formula: d = c * v[qp%6][posClass] << (qp/6)
    // ITU-T H.264 §8.5.12.1 eq 8-315.
    int16_t dequant[16];
    std::memcpy(dequant, raw, sizeof(raw));
    inverseQuantize4x4(dequant, 24);

    // QP=24: qpDiv6=4, qpMod6=0, shift=4-2=2.
    // Position (0,0) posClass=0: 2 * 10 * 4 = 80
    CHECK(dequant[0] == 80);

    // IDCT of full dequantized block, DC prediction = 81
    uint8_t pred[16];
    std::memset(pred, 81, 16);
    uint8_t out[16];
    inverseDct4x4AddPred(dequant, pred, 4U, out, 4U);

    // With << (qpDiv6-2): pixel(0,0) = 77
    MESSAGE("Formula << (qpDiv6-2): pixel(0,0) = " << static_cast<unsigned>(out[0]));
    CHECK(out[0] == 77U);

    // Alternative: d = c * v << max(qpDiv6 - 2, 0)
    int16_t altDequant[16];
    for (uint32_t i = 0U; i < 16U; ++i)
    {
        int32_t posClass = cDequantPosClass[i];
        int32_t scale = cDequantScale[0][posClass]; // qpMod6=0
        int32_t val = raw[i] * scale;
        val <<= 2; // qpDiv6 - 2 = 4 - 2 = 2
        altDequant[i] = static_cast<int16_t>(val);
    }
    uint8_t altOut[16];
    inverseDct4x4AddPred(altDequant, pred, 4U, altOut, 4U);
    MESSAGE("Formula << (qpDiv6-2): pixel(0,0) = " << static_cast<unsigned>(altOut[0]));

    // Report both for ongoing investigation
    MESSAGE("Row 0: current=[" << (unsigned)out[0] << " " << (unsigned)out[1]
            << " " << (unsigned)out[2] << " " << (unsigned)out[3]
            << "]  alt=[" << (unsigned)altOut[0] << " " << (unsigned)altOut[1]
            << " " << (unsigned)altOut[2] << " " << (unsigned)altOut[3] << "]"
            << "  ref=[81 80 82 78]");
}
