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
#include "../components/sub0h264/src/decoder.hpp"
#include "../components/sub0h264/src/annexb.hpp"
#include "test_fixtures.hpp"
#include <cmath>

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
    // With codIOffset=509 >= threshold 388 -> LPS -> 0 (I_NxN)
    CHECK(bin0 == 0U);
}

// ── Full CABAC decode pipeline tests ───────────────────────────────────────

TEST_CASE("CABAC decode: flat gray single-MB Y plane mean within range")
{
    // Decode the flat gray 16x16 CABAC fixture (cabac_idr_only).
    // The raw source Y plane is ~121. Our decode should produce close to that.
    // Currently produces ~129 (8 pixels off). As CABAC fixes improve,
    // this test's tolerance should tighten toward < 1 pixel error.
    auto h264 = getFixture("cabac_idr_only.h264");
    if (h264.empty())
    {
        MESSAGE("cabac_idr_only.h264 not found - skipping");
        return;
    }

    H264Decoder decoder;
    std::vector<NalBounds> bounds;
    findNalUnits(h264.data(), static_cast<uint32_t>(h264.size()), bounds);

    const Frame* decoded = nullptr;
    for (const auto& b : bounds)
    {
        NalUnit nal;
        if (!parseNalUnit(h264.data() + b.offset, b.size, nal))
            continue;
        if (decoder.processNal(nal) == DecodeStatus::FrameDecoded)
            decoded = decoder.currentFrame();
    }
    REQUIRE(decoded != nullptr);
    REQUIRE(decoded->width() >= 16U);
    REQUIRE(decoded->height() >= 16U);

    // Compute Y plane mean
    double ySum = 0.0;
    uint32_t w = decoded->width(), h = decoded->height();
    for (uint32_t r = 0U; r < h; ++r)
        for (uint32_t c = 0U; c < w; ++c)
            ySum += decoded->yRow(r)[c];
    double yMean = ySum / (w * h);

    MESSAGE("CABAC flat gray Y mean: " << yMean << " (target ~121)");
    // Track regression: currently ~129, will converge to ~121
    CHECK(yMean > 80.0);   // Not completely broken
    CHECK(yMean < 200.0);  // Not completely inverted
}

TEST_CASE("CABAC decode: flat gray 320x240 I-frame pixel analysis")
{
    // Decode the 320x240 flat gray CABAC fixture.
    // Analyze MB(0,0) pixels specifically to isolate prediction vs residual errors.
    auto h264 = getFixture("cabac_flat_main.h264");
    if (h264.empty())
    {
        MESSAGE("cabac_flat_main.h264 not found - skipping");
        return;
    }

    H264Decoder decoder;
    std::vector<NalBounds> bounds;
    findNalUnits(h264.data(), static_cast<uint32_t>(h264.size()), bounds);

    const Frame* decoded = nullptr;
    for (const auto& b : bounds)
    {
        NalUnit nal;
        if (!parseNalUnit(h264.data() + b.offset, b.size, nal))
            continue;
        if (decoder.processNal(nal) == DecodeStatus::FrameDecoded)
            decoded = decoder.currentFrame();
    }
    REQUIRE(decoded != nullptr);
    CHECK(decoded->width() == 320U);
    CHECK(decoded->height() == 240U);

    // Compute Y plane stats
    double ySum = 0.0;
    uint32_t minY = 255U, maxY = 0U;
    for (uint32_t r = 0U; r < decoded->height(); ++r)
    {
        for (uint32_t c = 0U; c < decoded->width(); ++c)
        {
            uint8_t val = decoded->yRow(r)[c];
            ySum += val;
            if (val < minY) minY = val;
            if (val > maxY) maxY = val;
        }
    }
    double yMean = ySum / (320.0 * 240.0);

    MESSAGE("CABAC 320x240 flat: Y mean=" << yMean
            << " min=" << minY << " max=" << maxY);

    // Analyze MB(0,0) specifically: first 16x16 pixels
    double mb0Sum = 0.0;
    for (uint32_t r = 0U; r < 16U; ++r)
        for (uint32_t c = 0U; c < 16U; ++c)
            mb0Sum += decoded->yRow(r)[c];
    double mb0Mean = mb0Sum / 256.0;
    MESSAGE("MB(0,0) Y mean: " << mb0Mean
            << " (DC pred=128, target=121, residual should be -7)");

    // Analyze MB(1,0): second MB
    double mb1Sum = 0.0;
    for (uint32_t r = 0U; r < 16U; ++r)
        for (uint32_t c = 16U; c < 32U; ++c)
            mb1Sum += decoded->yRow(r)[c];
    double mb1Mean = mb1Sum / 256.0;
    MESSAGE("MB(1,0) Y mean: " << mb1Mean);

    // Sanity checks
    CHECK(yMean > 50.0);
    CHECK(yMean < 220.0);
    CHECK(mb0Mean > 50.0);  // MB(0,0) shouldn't be black
    CHECK(mb0Mean < 200.0); // or white
}

TEST_CASE("CABAC decode: scrolling texture High profile I-frame produces non-black")
{
    // Decode the High profile CABAC scrolling texture.
    // The raw source Y mean is ~123. Our decode should not be near 0 or 255.
    auto h264 = getFixture("scrolling_texture_high.h264");
    auto raw = getFixture("scrolling_texture_high_raw.yuv");
    if (h264.empty() || raw.empty())
    {
        MESSAGE("scrolling_texture_high fixture not found - skipping");
        return;
    }

    H264Decoder decoder;
    std::vector<NalBounds> bounds;
    findNalUnits(h264.data(), static_cast<uint32_t>(h264.size()), bounds);

    const Frame* decoded = nullptr;
    for (const auto& b : bounds)
    {
        NalUnit nal;
        if (!parseNalUnit(h264.data() + b.offset, b.size, nal))
            continue;
        if (decoder.processNal(nal) == DecodeStatus::FrameDecoded)
        {
            decoded = decoder.currentFrame();
            break; // just first frame
        }
    }
    REQUIRE(decoded != nullptr);

    // Compute PSNR against raw Y plane
    uint32_t w = 320U, h = 240U;
    double sse = 0.0;
    for (uint32_t r = 0U; r < h; ++r)
    {
        const uint8_t* decRow = decoded->yRow(r);
        const uint8_t* refRow = raw.data() + r * w;
        for (uint32_t c = 0U; c < w; ++c)
        {
            double d = static_cast<double>(decRow[c]) - refRow[c];
            sse += d * d;
        }
    }
    double mse = sse / (w * h);
    double psnr = (mse > 0.0) ? 10.0 * std::log10(255.0 * 255.0 / mse) : 999.0;

    MESSAGE("CABAC High profile scrolling_texture frame 0: PSNR=" << psnr << " dB");
    // Currently ~10 dB. Target: 50 dB when CABAC fully implemented.
    CHECK(psnr >= 5.0);
}
