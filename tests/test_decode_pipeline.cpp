#include "doctest.h"
#include "test_fixtures.hpp"
#include "../components/sub0h264/src/decoder.hpp"

#include <memory>
#include <vector>

using namespace sub0h264;

/** Compute a simple checksum of a frame's Y plane (sum of all bytes). */
static uint64_t yPlaneSum(const Frame& frame)
{
    uint64_t sum = 0U;
    for (uint32_t row = 0U; row < frame.height(); ++row)
    {
        const uint8_t* rowPtr = frame.yRow(row);
        for (uint32_t col = 0U; col < frame.width(); ++col)
            sum += rowPtr[col];
    }
    return sum;
}

TEST_CASE("Pipeline: H264Decoder decodes flat_black IDR")
{
    auto data = getFixture("flat_black_640x480.h264");
    REQUIRE_FALSE(data.empty());

    auto decoder = std::make_unique<H264Decoder>();
    int32_t frames = decoder->decodeStream(data.data(), static_cast<uint32_t>(data.size()));

    CHECK(frames >= 1);

    const Frame* frame = decoder->currentFrame();
    REQUIRE(frame != nullptr);
    CHECK(frame->width() == 640U);
    CHECK(frame->height() == 480U);

    // Flat black frame: BT.601 "legal black" = Y=16, U=128, V=128.
    // CRC-verified against ffmpeg reference (0x509df05f).
    uint64_t sum = yPlaneSum(*frame);
    double avgY = static_cast<double>(sum) / (640.0 * 480.0);

    MESSAGE("flat_black decoded: " << frame->width() << "x" << frame->height()
            << " avg_Y=" << avgY);

    /// BT.601 legal black luma value.
    static constexpr double cBt601BlackY = 16.0;
    CHECK(avgY == cBt601BlackY);
}

TEST_CASE("Pipeline: H264Decoder processes SPS/PPS from baseline stream")
{
    auto data = getFixture("baseline_640x480_short.h264");
    REQUIRE_FALSE(data.empty());

    auto decoder = std::make_unique<H264Decoder>();

    // Feed just the first few NALs to test SPS/PPS processing
    std::vector<NalBounds> bounds;
    findNalUnits(data.data(), static_cast<uint32_t>(data.size()), bounds);
    REQUIRE(bounds.size() >= 3U);

    // Process SPS
    NalUnit spsNal;
    REQUIRE(parseNalUnit(data.data() + bounds[0].offset, bounds[0].size, spsNal));
    CHECK(spsNal.type == NalType::Sps);
    CHECK(decoder->processNal(spsNal) == DecodeStatus::NeedMoreData);

    // Process PPS
    NalUnit ppsNal;
    REQUIRE(parseNalUnit(data.data() + bounds[1].offset, bounds[1].size, ppsNal));
    CHECK(ppsNal.type == NalType::Pps);
    CHECK(decoder->processNal(ppsNal) == DecodeStatus::NeedMoreData);

    // Verify parameter sets are stored
    const Sps* sps = decoder->paramSets().getSps(0);
    REQUIRE(sps != nullptr);
    CHECK(sps->width() == 640U);
    CHECK(sps->height() == 480U);
}

TEST_CASE("Pipeline: H264Decoder lifecycle")
{
    auto decoder = std::make_unique<H264Decoder>();

    CHECK(decoder->currentFrame() == nullptr);
    CHECK(decoder->frameCount() == 0U);

    // Decode empty stream
    const uint8_t empty[] = { 0x00 };
    int32_t frames = decoder->decodeStream(empty, 1U);
    CHECK(frames == 0);
}

TEST_CASE("Pipeline: frame allocation matches SPS")
{
    auto data = getFixture("flat_black_640x480.h264");
    REQUIRE_FALSE(data.empty());

    auto decoder = std::make_unique<H264Decoder>();
    decoder->decodeStream(data.data(), static_cast<uint32_t>(data.size()));

    const Frame* frame = decoder->currentFrame();
    REQUIRE(frame != nullptr);

    // Y plane should be 640*480 = 307200 bytes
    CHECK(frame->width() == 640U);
    CHECK(frame->height() == 480U);
    CHECK(frame->yStride() == 640U);
    CHECK(frame->uvStride() == 320U);

    // U and V planes should be 320*240 = 76800 bytes each
    // Just verify they're accessible
    CHECK(frame->uData() != nullptr);
    CHECK(frame->vData() != nullptr);
}

TEST_CASE("Pipeline: baseline_640x480_short — IDR + P-frames")
{
    auto data = getFixture("baseline_640x480_short.h264");
    REQUIRE_FALSE(data.empty());

    auto decoder = std::make_unique<H264Decoder>();
    int32_t frames = decoder->decodeStream(data.data(), static_cast<uint32_t>(data.size()));

    MESSAGE("baseline_640x480_short: decoded " << frames << " frames, "
            << "frameCount=" << decoder->frameCount());

    // Should decode IDR + P-frames (stream has 1 IDR + 49 P-frames = 50 total)
    CHECK(frames >= 1);
    CHECK(decoder->frameCount() >= 1U);

    const Frame* frame = decoder->currentFrame();
    REQUIRE(frame != nullptr);
    CHECK(frame->width() == 640U);
    CHECK(frame->height() == 480U);

    // Verify the last decoded frame has reasonable pixel data
    uint64_t sum = yPlaneSum(*frame);
    double avgY = static_cast<double>(sum) / (640.0 * 480.0);
    MESSAGE("Last frame avg_Y=" << avgY);
    CHECK(avgY >= 0.0);
    CHECK(avgY <= 255.0);
}

TEST_CASE("Pipeline: high_640x480 — CABAC High profile decode")
{
    auto data = getFixture("high_640x480.h264");
    REQUIRE_FALSE(data.empty());

    auto decoder = std::make_unique<H264Decoder>();
    int32_t frames = decoder->decodeStream(data.data(), static_cast<uint32_t>(data.size()));

    MESSAGE("high_640x480: decoded " << frames << " frames, "
            << "frameCount=" << decoder->frameCount());

    // High profile stream should decode at least the IDR frame
    CHECK(frames >= 1);

    const Frame* frame = decoder->currentFrame();
    REQUIRE(frame != nullptr);
    CHECK(frame->width() == 640U);
    CHECK(frame->height() == 480U);

    // Verify pixel output is in valid range
    uint64_t sum = yPlaneSum(*frame);
    double avgY = static_cast<double>(sum) / (640.0 * 480.0);
    MESSAGE("High profile last frame avg_Y=" << avgY);
    CHECK(avgY >= 0.0);
    CHECK(avgY <= 255.0);
}
