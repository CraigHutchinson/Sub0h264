/** Sub0h264 ESP32-P4 unit test runner
 *
 *  Runs the full doctest test suite on ESP32-P4 hardware.
 *  Test fixture H.264 files are embedded in flash as binary data.
 *
 *  SPDX-License-Identifier: MIT
 */
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// ── Embedded test fixtures (linked by target_add_binary_data) ───────────
extern const uint8_t flat_black_640x480_h264_start[] asm("_binary_flat_black_640x480_h264_start");
extern const uint8_t flat_black_640x480_h264_end[]   asm("_binary_flat_black_640x480_h264_end");

extern const uint8_t baseline_640x480_short_h264_start[] asm("_binary_baseline_640x480_short_h264_start");
extern const uint8_t baseline_640x480_short_h264_end[]   asm("_binary_baseline_640x480_short_h264_end");

extern const uint8_t high_640x480_h264_start[] asm("_binary_high_640x480_h264_start");
extern const uint8_t high_640x480_h264_end[]   asm("_binary_high_640x480_h264_end");

// ── Doctest configuration for ESP-IDF (no main(), custom reporter) ──────
#define DOCTEST_CONFIG_IMPLEMENT
#define DOCTEST_CONFIG_NO_POSIX_SIGNALS
#define DOCTEST_CONFIG_NO_MULTITHREADING
#include "doctest.h"

// ── Decoder headers ─────────────────────────────────────────────────────
#include <memory>
#include "decoder.hpp"
#include "bitstream.hpp"
#include "annexb.hpp"
#include "nal.hpp"
#include "sps.hpp"
#include "pps.hpp"
#include "slice.hpp"
#include "cavlc.hpp"
#include "transform.hpp"
#include "intra_pred.hpp"
#include "inter_pred.hpp"
#include "motion.hpp"
#include "deblock.hpp"
#include "cabac.hpp"
#include "frame.hpp"

using namespace sub0h264;

static const char* TAG = "sub0h264_test";

// ── Helper: compute Y plane average ─────────────────────────────────────
static double yPlaneAvg(const Frame& frame)
{
    uint64_t sum = 0U;
    for (uint32_t row = 0U; row < frame.height(); ++row)
    {
        const uint8_t* rowPtr = frame.yRow(row);
        for (uint32_t col = 0U; col < frame.width(); ++col)
            sum += rowPtr[col];
    }
    return static_cast<double>(sum) / (frame.width() * frame.height());
}

// ── Core unit tests (non-file-dependent) ────────────────────────────────

TEST_CASE("ESP32: BitReader basics")
{
    const uint8_t data[] = { 0xAB, 0xCD };
    BitReader br(data, 2U);
    CHECK(br.readBits(8U) == 0xABU);
    CHECK(br.readBits(8U) == 0xCDU);
}

TEST_CASE("ESP32: clipU8 boundaries")
{
    CHECK(clipU8(-10) == 0);
    CHECK(clipU8(128) == 128);
    CHECK(clipU8(300) == 255);
}

TEST_CASE("ESP32: MV median3")
{
    CHECK(median3(1, 2, 3) == 2);
    CHECK(median3(-5, 0, 5) == 0);
}

TEST_CASE("ESP32: Frame allocate 640x480")
{
    Frame frame;
    REQUIRE(frame.allocate(640U, 480U));
    CHECK(frame.width() == 640U);
    CHECK(frame.height() == 480U);
    CHECK(frame.yStride() == 640U);

    // Verify PSRAM allocation worked
    size_t freeHeap = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
    ESP_LOGI(TAG, "Free heap after 640x480 frame: %u bytes", (unsigned)freeHeap);
}

TEST_CASE("ESP32: Hadamard 2x2 roundtrip")
{
    int16_t dc[4] = { 4, 0, 0, 0 };
    inverseHadamard2x2(dc);
    CHECK(dc[0] == 4);
    CHECK(dc[1] == 4);
    CHECK(dc[2] == 4);
    CHECK(dc[3] == 4);
}

TEST_CASE("ESP32: DPB init and reference")
{
    Dpb dpb;
    dpb.init(16U, 16U, 3U);
    Frame* f = dpb.getDecodeTarget();
    REQUIRE(f != nullptr);
    f->fill(42U, 0U, 0U);
    dpb.markAsReference(0U);
    const Frame* ref = dpb.getReference(0U);
    REQUIRE(ref != nullptr);
    CHECK(ref->y(0U, 0U) == 42U);
}

TEST_CASE("ESP32: Deblocking tables valid")
{
    CHECK(cAlphaTable[0] == 0U);
    CHECK(cAlphaTable[51] == 255U);
    CHECK(cBetaTable[51] == 18U);
}

// ── Pipeline tests with embedded fixtures ───────────────────────────────

TEST_CASE("ESP32: AAA NAL parse only (no decode)")
{
    // Lightweight test: just parse NAL units from flat_black, no reconstruction
    uint32_t size = static_cast<uint32_t>(flat_black_640x480_h264_end - flat_black_640x480_h264_start);
    ESP_LOGI(TAG, "NAL parse test: flat_black %u bytes", size);
    REQUIRE(size > 0U);

    std::vector<NalBounds> bounds;
    findNalUnits(flat_black_640x480_h264_start, size, bounds);
    ESP_LOGI(TAG, "Found %u NAL units", (unsigned)bounds.size());
    CHECK(bounds.size() >= 3U); // SPS + PPS + IDR at minimum

    // Parse first NAL (should be SPS)
    NalUnit spsNal;
    bool ok = parseNalUnit(flat_black_640x480_h264_start + bounds[0].offset, bounds[0].size, spsNal);
    CHECK(ok);
    CHECK(spsNal.type == NalType::Sps);
    ESP_LOGI(TAG, "NAL parse test PASSED");
}

TEST_CASE("ESP32: AAB SPS/PPS parse (no decode)")
{
    uint32_t size = static_cast<uint32_t>(flat_black_640x480_h264_end - flat_black_640x480_h264_start);
    std::vector<NalBounds> bounds;
    findNalUnits(flat_black_640x480_h264_start, size, bounds);

    // Heap-allocate decoder to avoid stack overflow (ParamSets is large)
    auto decoder = std::make_unique<H264Decoder>();
    for (uint32_t i = 0U; i < bounds.size() && i < 2U; ++i)
    {
        NalUnit nal;
        if (parseNalUnit(flat_black_640x480_h264_start + bounds[i].offset, bounds[i].size, nal))
            decoder->processNal(nal);
    }
    const Sps* sps = decoder->paramSets().getSps(0);
    REQUIRE(sps != nullptr);
    CHECK(sps->width() == 640U);
    CHECK(sps->height() == 480U);
    ESP_LOGI(TAG, "SPS/PPS parse PASSED: %ux%u", sps->width(), sps->height());
}

TEST_CASE("ESP32: Decode flat_black IDR")
{
    uint32_t size = static_cast<uint32_t>(flat_black_640x480_h264_end - flat_black_640x480_h264_start);
    ESP_LOGI(TAG, "flat_black fixture: %u bytes, starting decode...", size);
    REQUIRE(size > 0U);

    ESP_LOGI(TAG, "Free heap before decode: %u", (unsigned)heap_caps_get_free_size(MALLOC_CAP_DEFAULT));

    int64_t t0 = esp_timer_get_time();
    auto decoder = std::make_unique<H264Decoder>();
    int32_t frames = decoder->decodeStream(flat_black_640x480_h264_start, size);
    int64_t elapsed = esp_timer_get_time() - t0;

    ESP_LOGI(TAG, "flat_black: %d frames in %lld us (%.1f ms)",
             frames, elapsed, elapsed / 1000.0);

    CHECK(frames >= 1);
    const Frame* frame = decoder->currentFrame();
    REQUIRE(frame != nullptr);
    CHECK(frame->width() == 640U);
    CHECK(frame->height() == 480U);

    double avgY = yPlaneAvg(*frame);
    ESP_LOGI(TAG, "flat_black avg_Y=%.2f", avgY);
    CHECK(avgY < 5.0);
}

TEST_CASE("ESP32: Decode baseline_640x480_short (CAVLC I+P)")
{
    uint32_t size = static_cast<uint32_t>(baseline_640x480_short_h264_end - baseline_640x480_short_h264_start);
    ESP_LOGI(TAG, "baseline_short fixture: %u bytes", size);
    REQUIRE(size > 0U);

    int64_t t0 = esp_timer_get_time();
    auto decoder = std::make_unique<H264Decoder>();
    int32_t frames = decoder->decodeStream(baseline_640x480_short_h264_start, size);
    int64_t elapsed = esp_timer_get_time() - t0;

    double fps = (elapsed > 0) ? (frames * 1000000.0 / elapsed) : 0;
    ESP_LOGI(TAG, "baseline_short: %d frames in %lld us (%.1f ms, %.1f fps)",
             frames, elapsed, elapsed / 1000.0, fps);

    CHECK(frames >= 1);
    const Frame* frame = decoder->currentFrame();
    REQUIRE(frame != nullptr);
    CHECK(frame->width() == 640U);
    CHECK(frame->height() == 480U);
}

TEST_CASE("ESP32: Decode high_640x480 (CABAC I+P)")
{
    uint32_t size = static_cast<uint32_t>(high_640x480_h264_end - high_640x480_h264_start);
    ESP_LOGI(TAG, "high_640x480 fixture: %u bytes", size);
    REQUIRE(size > 0U);

    int64_t t0 = esp_timer_get_time();
    auto decoder = std::make_unique<H264Decoder>();
    int32_t frames = decoder->decodeStream(high_640x480_h264_start, size);
    int64_t elapsed = esp_timer_get_time() - t0;

    double fps = (elapsed > 0) ? (frames * 1000000.0 / elapsed) : 0;
    ESP_LOGI(TAG, "high_640x480: %d frames in %lld us (%.1f ms, %.1f fps)",
             frames, elapsed, elapsed / 1000.0, fps);

    CHECK(frames >= 1);
    const Frame* frame = decoder->currentFrame();
    REQUIRE(frame != nullptr);
    CHECK(frame->width() == 640U);
    CHECK(frame->height() == 480U);
}

// ── Hardware verification tests ─────────────────────────────────────────

TEST_CASE("ESP32: PSRAM available and sufficient")
{
    size_t psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    ESP_LOGI(TAG, "PSRAM free: %u bytes", (unsigned)psram);
    // Require at least 2MB free PSRAM for decode buffers (DPB + frames)
    CHECK(psram >= (2U * 1024U * 1024U));
}

// ── ESP-IDF entry point ─────────────────────────────────────────────────

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "Sub0h264 unit tests starting...");
    ESP_LOGI(TAG, "Free heap: %u bytes", (unsigned)heap_caps_get_free_size(MALLOC_CAP_DEFAULT));
    ESP_LOGI(TAG, "Free PSRAM: %u bytes", (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    doctest::Context ctx;
    ctx.setOption("no-breaks", true);
    ctx.setOption("duration", true);
    int result = ctx.run();

    ESP_LOGI(TAG, "Tests %s (%d failures)",
             result == 0 ? "PASSED" : "FAILED", result);

    // Print heap and stack usage after tests
    ESP_LOGI(TAG, "Free heap after tests: %u bytes",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_DEFAULT));
    ESP_LOGI(TAG, "Free PSRAM after tests: %u bytes",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    UBaseType_t hwm = uxTaskGetStackHighWaterMark(nullptr);
    ESP_LOGI(TAG, "Stack high-water mark: %u bytes remaining",
             (unsigned)(hwm * sizeof(StackType_t)));
}
