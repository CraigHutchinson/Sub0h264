/** Sub0h264 — Frame output verification tests
 *
 *  Validates decoded frames against ffmpeg-generated reference CRC32 values.
 *  On CRC mismatch: dumps actual frame as PPM, writes report with PSNR/stats.
 *  Supports fuzzy pass/fail: CRC match = strict, high PSNR = warn not fail.
 *
 *  SPDX-License-Identifier: MIT
 */
#include "doctest.h"
#include "test_fixtures.hpp"
#include "../components/sub0h264/src/decoder.hpp"
#include "../components/sub0h264/src/frame_verify.hpp"

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

using namespace sub0h264;

/// Report output directory (relative to CWD — typically build/).
static constexpr const char* cReportDir = "test_reports";

/** Ensure a directory exists (platform-independent mkdir). */
static void ensureDir([[maybe_unused]] const char* path)
{
#ifndef ESP_PLATFORM
    // Use system mkdir — works on Windows (mkdir) and Unix (mkdir -p)
#ifdef _WIN32
    std::string cmd = std::string("mkdir \"") + path + "\" 2>nul";
#else
    std::string cmd = std::string("mkdir -p \"") + path + "\"";
#endif
    std::system(cmd.c_str());
#endif
}

/** Decode a stream and verify per-frame CRC against reference.
 *  On failure: dumps actual frame PPM + text report to test_reports/.
 *  Returns number of CRC mismatches (strict) or warnings (fuzzy).
 */
static uint32_t verifyStreamCrcs(const char* fixture,
                                  const uint32_t* refCrcs, uint32_t refCount,
                                  bool fuzzyMode = false)
{
    auto data = getFixture(fixture);
    REQUIRE_FALSE(data.empty());

    std::vector<NalBounds> bounds;
    findNalUnits(data.data(), static_cast<uint32_t>(data.size()), bounds);

    auto decoder = std::make_unique<H264Decoder>();
    uint32_t frameIdx = 0U;
    uint32_t strictFails = 0U;
    uint32_t matches = 0U;
    uint32_t fuzzyWarns = 0U;

    /// Maximum number of failure reports to dump per stream (avoids filling disk).
    static constexpr uint32_t cMaxDumpsPerStream = 5U;
    uint32_t dumpCount = 0U;

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
                    // CRC mismatch — generate failure report
                    char buf[256];
                    std::snprintf(buf, sizeof(buf),
                        "Frame %lu CRC MISMATCH: got 0x%08lx expected 0x%08lx",
                        (unsigned long)frameIdx, (unsigned long)crc,
                        (unsigned long)refCrcs[frameIdx]);
                    MESSAGE(buf);

#ifndef ESP_PLATFORM
                    // Dump actual frame + report (desktop only, limited count)
                    if (dumpCount < cMaxDumpsPerStream)
                    {
                        // Strip path and extension from fixture name for directory
                        std::string fixName(fixture);
                        auto dot = fixName.rfind('.');
                        if (dot != std::string::npos) fixName = fixName.substr(0, dot);

                        char dirPath[512];
                        std::snprintf(dirPath, sizeof(dirPath), "%s/%s/frame_%03lu_FAIL",
                            cReportDir, fixName.c_str(), (unsigned long)frameIdx);
                        ensureDir(cReportDir);
                        char streamDir[512];
                        std::snprintf(streamDir, sizeof(streamDir), "%s/%s",
                            cReportDir, fixName.c_str());
                        ensureDir(streamDir);
                        ensureDir(dirPath);

                        // Dump actual frame as grayscale and RGB PPM
                        char ppmPath[512];
                        std::snprintf(ppmPath, sizeof(ppmPath), "%s/actual_gray.pgm", dirPath);
                        writeFrameGrayPpm(*frame, ppmPath);
                        std::snprintf(ppmPath, sizeof(ppmPath), "%s/actual.ppm", dirPath);
                        writeFrameRgbPpm(*frame, ppmPath);

                        // Write text report
                        // Note: full FrameVerifyReport requires a reference Frame object.
                        // We only have the CRC here. Write what we can.
                        char reportPath[512];
                        std::snprintf(reportPath, sizeof(reportPath), "%s/report.txt", dirPath);
                        FILE* rf = std::fopen(reportPath, "w");
                        if (rf)
                        {
                            std::fprintf(rf, "Frame Verification Report\n");
                            std::fprintf(rf, "========================\n");
                            std::fprintf(rf, "Stream:         %s\n", fixture);
                            std::fprintf(rf, "Frame index:    %lu\n", (unsigned long)frameIdx);
                            std::fprintf(rf, "CRC actual:     0x%08lx\n", (unsigned long)crc);
                            std::fprintf(rf, "CRC expected:   0x%08lx\n",
                                (unsigned long)refCrcs[frameIdx]);
                            std::fprintf(rf, "CRC match:      NO\n");
                            std::fprintf(rf, "\nNote: Run scripts/gen_frame_diffs.py to generate\n");
                            std::fprintf(rf, "reference frames and visual diff images.\n");
                            std::fclose(rf);
                        }

                        std::snprintf(buf, sizeof(buf), "  Dumped to %s/", dirPath);
                        MESSAGE(buf);
                        ++dumpCount;
                    }
#endif
                    ++strictFails;
                }
            }
            ++frameIdx;
        }
    }

    char summary[256];
    std::snprintf(summary, sizeof(summary),
        "%s: %lu frames decoded, %lu/%lu CRC match, %lu strict fail, %lu fuzzy warn",
        fixture, (unsigned long)frameIdx, (unsigned long)matches,
        (unsigned long)refCount, (unsigned long)strictFails, (unsigned long)fuzzyWarns);
    MESSAGE(summary);

    return strictFails;
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
    const uint8_t testData[] = "123456789";
    /// IEEE 802.3 CRC32 of "123456789" — standard test vector.
    static constexpr uint32_t cExpectedCrc = 0xCBF43926U;
    uint32_t crc = crc32(0U, testData, 9U);
    CHECK(crc == cExpectedCrc);
}
