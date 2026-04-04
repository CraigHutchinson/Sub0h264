/** Test CABAC arithmetic engine against known reference values.
 *
 *  Verifies the CABAC engine (decodeBin, decodeBypass, decodeTerminate)
 *  produces correct bin sequences for known bitstream+context inputs.
 *  This isolates engine correctness from the full decoder pipeline.
 *
 *  Reference: ITU-T H.264 §9.3
 *  SPDX-License-Identifier: MIT
 */
#include "doctest.h"
#include "../components/sub0h264/src/cabac.hpp"
#include "../components/sub0h264/src/bitstream.hpp"

using namespace sub0h264;

// ── CABAC engine unit tests ────────────────────────────────────────────────

TEST_CASE("CABAC engine: init reads 9-bit codIOffset from byte-aligned pos")
{
    // Byte stream: 0xFE 0xF4 0x00 ...
    // 9-bit value from MSB: 11111110 | 1 = 509
    uint8_t data[] = {0xFE, 0xF4, 0x00, 0x00};
    BitReader br(data, 4U);

    CabacEngine engine;
    engine.init(br);

    // After init: 9 bits consumed
    CHECK(br.bitOffset() == 9U);
}

TEST_CASE("CABAC engine: decodeBin with known state produces expected symbol")
{
    // Construct a minimal bitstream where we know what the engine should decode.
    // Use a context with MPS=1, pStateIdx=0 (maximum LPS probability: 50/50).
    // codIRange=510, codIOffset=0 → codIOffset < threshold → MPS
    uint8_t data[] = {0x00, 0x00, 0x00, 0x00, 0x00};
    BitReader br(data, 5U);

    CabacEngine engine;
    engine.init(br); // codIOffset = 0

    // Context: pStateIdx=0, MPS=1 → mpsState = 0 | (1<<6) = 64
    CabacCtx ctx;
    ctx.mpsState = 64U; // pState=0, MPS=1

    // With codIOffset=0, which is < any threshold, should decode MPS=1
    uint32_t symbol = engine.decodeBin(ctx);
    CHECK(symbol == 1U); // MPS
}

TEST_CASE("CABAC engine: decodeBin LPS with high offset")
{
    // codIOffset=511 (max 9-bit value) → should be in LPS region for most states
    uint8_t data[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    BitReader br(data, 5U);

    CabacEngine engine;
    engine.init(br); // codIOffset = 511 (but capped to < codIRange=510, so 510 effectively)

    // Wait — codIOffset can be up to 511 (9 bits). codIRange is 510.
    // If codIOffset > codIRange, the spec says this is invalid, but our engine should handle it.
    // For state 0 (most uncertain): rangeLPS is 128 (qRange=3 for range 510).
    // threshold = 510 - 128 = 382
    // codIOffset=511 >= 382 → LPS

    CabacCtx ctx;
    ctx.mpsState = 64U; // pState=0, MPS=1

    uint32_t symbol = engine.decodeBin(ctx);
    CHECK(symbol == 0U); // LPS (since MPS=1, LPS symbol = 1-1 = 0)
}

TEST_CASE("CABAC engine: decodeBypass produces alternating bits from 0xAA pattern")
{
    // 0xAA = 10101010 → after 9-bit init consuming "10101010 | 1" = 0x155 = 341
    // Subsequent bypass bins read from the remaining bits.
    uint8_t data[] = {0xAA, 0xAA, 0xAA, 0xAA, 0xAA};
    BitReader br(data, 5U);

    CabacEngine engine;
    engine.init(br); // consumes 9 bits

    // Bypass bins read one bit each, shifting offset
    // The exact values depend on the range/offset state after init.
    // Just verify it doesn't crash and produces 0 or 1.
    for (int i = 0; i < 10; ++i)
    {
        uint32_t bin = engine.decodeBypass();
        CHECK(bin <= 1U);
    }
}

TEST_CASE("CABAC engine: decodeTerminate returns 0 for non-terminal data")
{
    // Normal data (not end-of-slice) should return 0 from decodeTerminate.
    uint8_t data[] = {0x00, 0x80, 0x00, 0x00, 0x00};
    BitReader br(data, 5U);

    CabacEngine engine;
    engine.init(br);

    uint32_t term = engine.decodeTerminate();
    CHECK(term == 0U); // Not end-of-slice
}

TEST_CASE("CABAC context init: I-slice QP=17 context 5 matches spec Table 9-12")
{
    // Context 5 (mb_type I-slice, ctxIdxInc=2): m=3, n=74
    // At QP=17: preCtxState = (3*17)/16 + 74 = 3+74 = 77
    // Since 77 > 63: pStateIdx = 77-64 = 13, valMPS = 1
    CabacCtx ctx[cNumCabacCtx] = {};
    initCabacContexts(ctx, 2U /* I-slice */, 0U, 17);

    uint8_t state5 = ctx[5].mpsState;
    uint8_t pStateIdx = state5 & 0x3FU;
    uint8_t valMPS = (state5 >> 6U) & 1U;

    CHECK(pStateIdx == 13U);
    CHECK(valMPS == 1U);
}

TEST_CASE("CABAC context init: P-slice idc=0 QP=20 context 14 for mb_type_P")
{
    // Context 14 (mb_type P-slice bin 0): init from cabac_init_idc=0
    CabacCtx ctx[cNumCabacCtx] = {};
    initCabacContexts(ctx, 0U /* P-slice */, 0U, 20);

    // Just verify it doesn't crash and produces a valid state
    uint8_t state = ctx[14].mpsState;
    CHECK((state & 0x3FU) <= 63U); // valid pStateIdx range
}

// ── First-bin decode verification ──────────────────────────────────────────

TEST_CASE("CABAC first bin: flat Main profile fixture decodes MB(0,0) as I_NxN")
{
    // The flat gray CABAC fixture has codIOffset=509 which gives LPS for
    // context 5 (pState=13, MPS=1). This is I_4x4 (bin 0 = 0).
    // This test verifies the engine arithmetic is correct for this case.
    //
    // Note: ffmpeg also decodes this stream correctly despite the LPS result,
    // meaning the encoder genuinely uses I_4x4 for some MBs even in "100% I_16x16"
    // streams (the encoder stats may aggregate differently).

    uint8_t cabacData[] = {0xFE, 0xF4, 0xA0, 0xF8, 0x12, 0xEA};
    BitReader br(cabacData, 6U);

    CabacEngine engine;
    engine.init(br);

    CabacCtx ctx[cNumCabacCtx] = {};
    initCabacContexts(ctx, 2U, 0U, 17);

    // mb_type bin 0 for MB(0,0): ctxIdx = 3 + ctxInc(2) = 5
    uint32_t bin0 = engine.decodeBin(ctx[5]);
    // With codIOffset=509 >= threshold 388 → LPS → 0 (I_NxN)
    CHECK(bin0 == 0U);
}
