/** Sub0h264 — Frame output verification tests
 *
 *  Validates decoded frames against ffmpeg-generated reference CRC32 values.
 *  This is the ground truth for decoder correctness.
 *
 *  SPDX-License-Identifier: MIT
 */
#include "doctest.h"
#include "test_fixtures.hpp"
#include "../components/sub0h264/src/decoder.hpp"
#include "../components/sub0h264/src/frame_verify.hpp"

#include <cstdio>
#include <memory>

using namespace sub0h264;

/** Decode a stream and verify per-frame CRC against reference.
 *  Returns number of CRC mismatches.
 */
static uint32_t verifyStreamCrcs(const char* fixture,
                                  const uint32_t* refCrcs, uint32_t refCount)
{
    auto data = getFixture(fixture);
    REQUIRE_FALSE(data.empty());

    // Parse NALs and decode frame-by-frame for per-frame CRC
    std::vector<NalBounds> bounds;
    findNalUnits(data.data(), static_cast<uint32_t>(data.size()), bounds);

    auto decoder = std::make_unique<H264Decoder>();
    uint32_t frameIdx = 0U;
    uint32_t mismatches = 0U;
    uint32_t matches = 0U;

    for (const auto& b : bounds)
    {
        NalUnit nal;
        if (!parseNalUnit(data.data() + b.offset, b.size, nal))
            continue;

        DecodeStatus status = decoder->processNal(nal);
        if (status == DecodeStatus::FrameDecoded)
        {
            const Frame* frame = decoder->currentFrame();
            REQUIRE(frame != nullptr);

            uint32_t crc = frameCrc32(*frame);

            if (frameIdx < refCount)
            {
                if (crc == refCrcs[frameIdx])
                {
                    ++matches;
                }
                else
                {
                    ++mismatches;
                    char buf[128];
                    std::snprintf(buf, sizeof(buf),
                        "Frame %lu CRC MISMATCH: got 0x%08lx expected 0x%08lx",
                        (unsigned long)frameIdx, (unsigned long)crc,
                        (unsigned long)refCrcs[frameIdx]);
                    MESSAGE(buf);
                }
            }
            ++frameIdx;
        }
    }

    char summary[128];
    std::snprintf(summary, sizeof(summary),
        "%s: %lu frames decoded, %lu/%lu CRC match, %lu mismatch",
        fixture, (unsigned long)frameIdx, (unsigned long)matches,
        (unsigned long)refCount, (unsigned long)mismatches);
    MESSAGE(summary);

    return mismatches;
}

TEST_CASE("Verify: flat_black_640x480 per-frame CRC")
{
    uint32_t mismatches = verifyStreamCrcs("flat_black_640x480.h264",
                                            cRefCrcFlatBlack, cRefCrcFlatBlackCount);
    CHECK(mismatches == 0U);
}

TEST_CASE("Verify: baseline_640x480_short per-frame CRC")
{
    uint32_t mismatches = verifyStreamCrcs("baseline_640x480_short.h264",
                                            cRefCrcBaseline, cRefCrcBaselineCount);
    // Report match rate even if not 100%
    // This tells us exactly how many frames are correct
    CHECK(mismatches == 0U);
}

TEST_CASE("Verify: high_640x480 per-frame CRC (first 5)")
{
    uint32_t mismatches = verifyStreamCrcs("high_640x480.h264",
                                            cRefCrcHigh, cRefCrcHighCount);
    CHECK(mismatches == 0U);
}

TEST_CASE("Verify: CRC32 implementation matches zlib")
{
    // Known test: CRC32 of "123456789" = 0xCBF43926 (IEEE 802.3)
    const uint8_t testData[] = "123456789";
    /// IEEE 802.3 CRC32 of "123456789" — standard test vector.
    static constexpr uint32_t cExpectedCrc = 0xCBF43926U;
    uint32_t crc = crc32(0U, testData, 9U);
    CHECK(crc == cExpectedCrc);
}
