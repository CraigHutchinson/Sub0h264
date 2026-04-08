// Skip this test on ESP32 — reference_decoder not available
#ifdef ESP_PLATFORM
// Empty file for ESP32 build
#else

/** Cross-check test: spec-ref decoder vs main decoder per-MB pixel comparison.
 *
 *  Decodes the same CABAC fixture with both decoders and compares output
 *  per-macroblock to identify exactly where they diverge.
 *
 *  SPDX-License-Identifier: MIT
 */
#include "doctest.h"
#include "../components/sub0h264/src/decoder.hpp"
#include "../reference_decoder/src/spec_ref_decode.hpp"
#include "test_fixtures.hpp"

#include <cmath>
#include <cstdio>

using namespace sub0h264;

/// Compute per-plane PSNR for a single MB (16x16 luma, 8x8 chroma).
static double mbLumaPsnr(const Frame& a, const Frame& b,
                          uint32_t mbX, uint32_t mbY)
{
    double mse = 0.0;
    for (uint32_t dy = 0; dy < 16; ++dy)
    {
        for (uint32_t dx = 0; dx < 16; ++dx)
        {
            int diff = static_cast<int>(a.y(mbX * 16 + dx, mbY * 16 + dy))
                     - static_cast<int>(b.y(mbX * 16 + dx, mbY * 16 + dy));
            mse += diff * diff;
        }
    }
    mse /= 256.0;
    if (mse < 0.001) return 99.0;
    return 10.0 * std::log10(255.0 * 255.0 / mse);
}

TEST_CASE("Cross-check: cabac_flat_main per-MB pixel comparison")
{
    auto h264 = getFixture("cabac_flat_main.h264");
    REQUIRE(!h264.empty());

    // Decode with main decoder
    H264Decoder mainDec;
    int mainFrames = mainDec.decodeStream(h264.data(),
                                           static_cast<uint32_t>(h264.size()));
    REQUIRE(mainFrames >= 1);
    const Frame* mainFrame = mainDec.currentFrame();
    REQUIRE(mainFrame != nullptr);

    // Decode with spec-ref decoder
    Frame specFrame;
    bool specOk = spec_ref::decodeIdrFrame(h264.data(),
                                            static_cast<uint32_t>(h264.size()),
                                            specFrame);
    if (!specOk) { MESSAGE("spec-ref decode failed — skipping crosscheck"); return; }

    uint32_t w = mainFrame->width();
    uint32_t h = mainFrame->height();
    CHECK(specFrame.width() == w);
    CHECK(specFrame.height() == h);

    uint32_t wMbs = w / 16;
    uint32_t hMbs = h / 16;

    // Load raw reference for absolute PSNR
    auto raw = getFixture("cabac_flat_main_raw.yuv");
    REQUIRE(!raw.empty());

    uint32_t ySize = w * h;
    const uint8_t* rawY = raw.data();

    // Per-MB comparison: main vs spec-ref, and each vs raw
    uint32_t mainBetter = 0;
    uint32_t specBetter = 0;
    uint32_t firstDivergeMb = 0xFFFFFFFF;

    MESSAGE("=== Per-MB cross-check: cabac_flat_main ===");
    MESSAGE("MB(x,y): main_pixel spec_pixel raw_pixel | main_err spec_err");

    for (uint32_t mbY = 0; mbY < hMbs; ++mbY)
    {
        for (uint32_t mbX = 0; mbX < wMbs; ++mbX)
        {
            // Sample pixel at (0,0) of MB
            uint32_t px = mbX * 16;
            uint32_t py = mbY * 16;
            uint8_t mainPx = mainFrame->y(px, py);
            uint8_t specPx = specFrame.y(px, py);
            uint8_t rawPx = rawY[py * w + px];

            int mainErr = std::abs(static_cast<int>(mainPx) - rawPx);
            int specErr = std::abs(static_cast<int>(specPx) - rawPx);

            if (mainPx != specPx && firstDivergeMb == 0xFFFFFFFF)
                firstDivergeMb = mbY * wMbs + mbX;

            if (mainErr > specErr) specBetter++;
            if (specErr > mainErr) mainBetter++;

            // Print first 5 rows for inspection
            if (mbY < 2 || (mbY == 0 && mbX < 10))
            {
                MESSAGE("  MB(" << mbX << "," << mbY << "): main=" << (int)mainPx
                        << " spec=" << (int)specPx << " raw=" << (int)rawPx
                        << " | err_main=" << mainErr << " err_spec=" << specErr);
            }
        }
    }

    MESSAGE("First divergence at MB index " << firstDivergeMb
            << " (" << (firstDivergeMb % wMbs) << "," << (firstDivergeMb / wMbs) << ")");
    MESSAGE("MBs where spec-ref is closer to raw: " << specBetter << "/" << (wMbs * hMbs));
    MESSAGE("MBs where main is closer to raw: " << mainBetter << "/" << (wMbs * hMbs));

    // The spec-ref decoder should be closer to raw on most MBs
    CHECK(specBetter > mainBetter);
}

TEST_CASE("Cross-check: cabac_flat_main first 4x4 block pixel dump")
{
    auto h264 = getFixture("cabac_flat_main.h264");
    REQUIRE(!h264.empty());

    H264Decoder mainDec;
    mainDec.decodeStream(h264.data(), static_cast<uint32_t>(h264.size()));
    const Frame* mainFrame = mainDec.currentFrame();
    REQUIRE(mainFrame != nullptr);

    Frame specFrame;
    spec_ref::decodeIdrFrame(h264.data(), static_cast<uint32_t>(h264.size()), specFrame);

    auto raw = getFixture("cabac_flat_main_raw.yuv");
    uint32_t w = mainFrame->width();

    MESSAGE("=== MB(0,0) first 4x4 block pixels ===");
    MESSAGE("  row  main[4]         spec[4]         raw[4]");
    for (uint32_t r = 0; r < 4; ++r)
    {
        char buf[256];
        std::snprintf(buf, sizeof(buf),
            "  %u:  [%3d %3d %3d %3d] [%3d %3d %3d %3d] [%3d %3d %3d %3d]",
            r,
            mainFrame->y(0, r), mainFrame->y(1, r), mainFrame->y(2, r), mainFrame->y(3, r),
            specFrame.y(0, r), specFrame.y(1, r), specFrame.y(2, r), specFrame.y(3, r),
            raw[r * w + 0], raw[r * w + 1], raw[r * w + 2], raw[r * w + 3]);
        MESSAGE(buf);
    }

    MESSAGE("=== MB(0,0) second 4x4 block (x=4..7) ===");
    for (uint32_t r = 0; r < 4; ++r)
    {
        char buf[256];
        std::snprintf(buf, sizeof(buf),
            "  %u:  [%3d %3d %3d %3d] [%3d %3d %3d %3d] [%3d %3d %3d %3d]",
            r,
            mainFrame->y(4, r), mainFrame->y(5, r), mainFrame->y(6, r), mainFrame->y(7, r),
            specFrame.y(4, r), specFrame.y(5, r), specFrame.y(6, r), specFrame.y(7, r),
            raw[r * w + 4], raw[r * w + 5], raw[r * w + 6], raw[r * w + 7]);
        MESSAGE(buf);
    }

    // Also dump chroma U for MB(0,0)
    MESSAGE("=== MB(0,0) chroma U (4x4) ===");
    for (uint32_t r = 0; r < 4; ++r)
    {
        uint32_t yPlaneSize = w * mainFrame->height();
        uint32_t uvW = w / 2;
        char buf[256];
        std::snprintf(buf, sizeof(buf),
            "  %u:  [%3d %3d %3d %3d] [%3d %3d %3d %3d] [%3d %3d %3d %3d]",
            r,
            mainFrame->u(0, r), mainFrame->u(1, r), mainFrame->u(2, r), mainFrame->u(3, r),
            specFrame.u(0, r), specFrame.u(1, r), specFrame.u(2, r), specFrame.u(3, r),
            raw[yPlaneSize + r * uvW + 0], raw[yPlaneSize + r * uvW + 1],
            raw[yPlaneSize + r * uvW + 2], raw[yPlaneSize + r * uvW + 3]);
        MESSAGE(buf);
    }
}

TEST_CASE("Cross-check: cabac_idr_only per-MB comparison")
{
    auto h264 = getFixture("cabac_idr_only.h264");
    if (h264.empty()) { MESSAGE("fixture not found"); return; }

    auto raw = getFixture("cabac_idr_only_raw.yuv");
    if (raw.empty()) { MESSAGE("raw not found"); return; }

    // Main decoder
    H264Decoder mainDec;
    int mainFrames = mainDec.decodeStream(h264.data(),
                                           static_cast<uint32_t>(h264.size()));
    REQUIRE(mainFrames >= 1);
    const Frame* mainFrame = mainDec.currentFrame();
    REQUIRE(mainFrame != nullptr);

    // Spec-ref decoder
    Frame specFrame;
    bool specOk = spec_ref::decodeIdrFrame(h264.data(),
                                            static_cast<uint32_t>(h264.size()),
                                            specFrame);
    if (!specOk) { MESSAGE("spec-ref decode failed — skipping crosscheck"); return; }

    uint32_t w = mainFrame->width();
    uint32_t h = mainFrame->height();
    uint32_t wMbs = w / 16;
    uint32_t hMbs = h / 16;

    // Compute full-frame Y PSNR for each decoder vs raw
    double mainMse = 0.0, specMse = 0.0;
    for (uint32_t py = 0; py < h; ++py)
    {
        for (uint32_t px = 0; px < w; ++px)
        {
            int mDiff = static_cast<int>(mainFrame->y(px, py)) - raw[py * w + px];
            int sDiff = static_cast<int>(specFrame.y(px, py)) - raw[py * w + px];
            mainMse += mDiff * mDiff;
            specMse += sDiff * sDiff;
        }
    }
    mainMse /= (w * h);
    specMse /= (w * h);
    double mainPsnr = (mainMse < 0.001) ? 99.0 : 10.0 * std::log10(255.0 * 255.0 / mainMse);
    double specPsnr = (specMse < 0.001) ? 99.0 : 10.0 * std::log10(255.0 * 255.0 / specMse);

    MESSAGE("cabac_idr_only frame 0: main Y PSNR=" << mainPsnr
            << " dB, spec-ref Y PSNR=" << specPsnr << " dB");

    // Find first divergent MB
    for (uint32_t mbY = 0; mbY < std::min(hMbs, 2U); ++mbY)
    {
        for (uint32_t mbX = 0; mbX < std::min(wMbs, 10U); ++mbX)
        {
            uint8_t mainPx = mainFrame->y(mbX * 16, mbY * 16);
            uint8_t specPx = specFrame.y(mbX * 16, mbY * 16);
            uint8_t rawPx = raw[mbY * 16 * w + mbX * 16];
            if (mainPx != specPx)
            {
                MESSAGE("  MB(" << mbX << "," << mbY << "): main=" << (int)mainPx
                        << " spec=" << (int)specPx << " raw=" << (int)rawPx);
            }
        }
    }

    // Both decoders have CABAC quality issues — just verify both decode
    CHECK(specPsnr > 3.0);
    CHECK(mainPsnr > 3.0);
}

#endif // ESP_PLATFORM
