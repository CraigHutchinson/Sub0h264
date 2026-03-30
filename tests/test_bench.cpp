/** Sub0h264 — Decode benchmarks as doctest test cases
 *
 *  Tagged with [bench] for CTest label filtering:
 *    ctest --preset default -L bench     (desktop)
 *    ctest --preset esp32p4              (ESP32 — runs all including bench)
 *
 *  Methodology: validate → warm-up → 3 measured passes → report median fps.
 *  Uses doctest MESSAGE() for output so results appear in CTest logs.
 *
 *  SPDX-License-Identifier: MIT
 */
#include "doctest.h"
#include "test_fixtures.hpp"
#include "../components/sub0h264/src/decode_timing.hpp"
#include "../components/sub0h264/src/decoder.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <memory>

using namespace sub0h264;

/// Number of timed passes after warm-up.
static constexpr uint32_t cBenchPasses = 3U;

/** Run a decode benchmark: validate, warm-up, measure, report. */
static void benchStream(const char* name, const char* fixture)
{
    auto data = getFixture(fixture);
    REQUIRE_FALSE(data.empty());

    // Validate
    int32_t frameCount;
    {
        auto dec = std::make_unique<H264Decoder>();
        frameCount = dec->decodeStream(data.data(), static_cast<uint32_t>(data.size()));
    }
    REQUIRE(frameCount > 0);

    // Warm-up (discarded)
    {
        auto dec = std::make_unique<H264Decoder>();
        dec->decodeStream(data.data(), static_cast<uint32_t>(data.size()));
    }

    // Measured passes
    std::array<double, cBenchPasses> passMs = {};
    for (uint32_t p = 0U; p < cBenchPasses; ++p)
    {
        auto dec = std::make_unique<H264Decoder>();
        int64_t t0 = sub0h264TimerUs();
        dec->decodeStream(data.data(), static_cast<uint32_t>(data.size()));
        int64_t elapsed = sub0h264TimerUs() - t0;
        passMs[p] = elapsed / 1000.0;
    }

    // Median
    auto sorted = passMs;
    std::sort(sorted.begin(), sorted.end());
    double medianMs = sorted[cBenchPasses / 2U];
    double medianFps = (medianMs > 0.0) ? (frameCount * 1000.0 / medianMs) : 0.0;

    // Report via doctest MESSAGE (captured by CTest)
    char buf[256];
    std::snprintf(buf, sizeof(buf),
        "BENCH %s: %ld frames, %.1f ms median (%.1f / %.1f / %.1f), %.1f fps",
        name, (long)frameCount, medianMs,
        passMs[0], passMs[1], passMs[2], medianFps);
    MESSAGE(buf);
}

TEST_CASE("Bench: Baseline CAVLC 640x480 (short)" * doctest::test_suite("bench"))
{
    benchStream("Baseline CAVLC (short)", "baseline_640x480_short.h264");
}

TEST_CASE("Bench: High CABAC 640x480" * doctest::test_suite("bench"))
{
    benchStream("High CABAC", "high_640x480.h264");
}

TEST_CASE("Bench: Flat black 640x480" * doctest::test_suite("bench"))
{
    benchStream("Flat black", "flat_black_640x480.h264");
}
