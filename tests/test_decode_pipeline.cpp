#include "doctest.h"
#include "../components/sub0h264/src/decoder.hpp"

#include <fstream>
#include <numeric>
#include <vector>

using namespace sub0h264;

static std::vector<uint8_t> loadFile(const char* path)
{
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.is_open())
        return {};
    auto size = f.tellg();
    f.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(static_cast<size_t>(size));
    f.read(reinterpret_cast<char*>(data.data()), size);
    return data;
}

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
    auto data = loadFile(SUB0H264_TEST_FIXTURES_DIR "/flat_black_640x480.h264");
    REQUIRE_FALSE(data.empty());

    H264Decoder decoder;
    int32_t frames = decoder.decodeStream(data.data(), static_cast<uint32_t>(data.size()));

    CHECK(frames >= 1);

    const Frame* frame = decoder.currentFrame();
    REQUIRE(frame != nullptr);
    CHECK(frame->width() == 640U);
    CHECK(frame->height() == 480U);

    // Flat black frame: Y should be near 0 (all-black content).
    // BT.601 "legal black" is Y=16, but ffmpeg testsrc black is Y=0.
    uint64_t sum = yPlaneSum(*frame);
    double avgY = static_cast<double>(sum) / (640.0 * 480.0);

    MESSAGE("flat_black decoded: " << frame->width() << "x" << frame->height()
            << " avg_Y=" << avgY);

    // Average Y should be very low for an all-black frame
    CHECK(avgY < 5.0);
}

TEST_CASE("Pipeline: H264Decoder processes SPS/PPS from baseline stream")
{
    auto data = loadFile(SUB0H264_TEST_FIXTURES_DIR "/baseline_640x480_short.h264");
    REQUIRE_FALSE(data.empty());

    H264Decoder decoder;

    // Feed just the first few NALs to test SPS/PPS processing
    std::vector<NalBounds> bounds;
    findNalUnits(data.data(), static_cast<uint32_t>(data.size()), bounds);
    REQUIRE(bounds.size() >= 3U);

    // Process SPS
    NalUnit spsNal;
    REQUIRE(parseNalUnit(data.data() + bounds[0].offset, bounds[0].size, spsNal));
    CHECK(spsNal.type == NalType::Sps);
    CHECK(decoder.processNal(spsNal) == DecodeStatus::NeedMoreData);

    // Process PPS
    NalUnit ppsNal;
    REQUIRE(parseNalUnit(data.data() + bounds[1].offset, bounds[1].size, ppsNal));
    CHECK(ppsNal.type == NalType::Pps);
    CHECK(decoder.processNal(ppsNal) == DecodeStatus::NeedMoreData);

    // Verify parameter sets are stored
    const Sps* sps = decoder.paramSets().getSps(0);
    REQUIRE(sps != nullptr);
    CHECK(sps->width() == 640U);
    CHECK(sps->height() == 480U);
}

TEST_CASE("Pipeline: H264Decoder lifecycle")
{
    H264Decoder decoder;

    CHECK(decoder.currentFrame() == nullptr);
    CHECK(decoder.frameCount() == 0U);

    // Decode empty stream
    const uint8_t empty[] = { 0x00 };
    int32_t frames = decoder.decodeStream(empty, 1U);
    CHECK(frames == 0);
}

TEST_CASE("Pipeline: frame allocation matches SPS")
{
    auto data = loadFile(SUB0H264_TEST_FIXTURES_DIR "/flat_black_640x480.h264");
    REQUIRE_FALSE(data.empty());

    H264Decoder decoder;
    decoder.decodeStream(data.data(), static_cast<uint32_t>(data.size()));

    const Frame* frame = decoder.currentFrame();
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
