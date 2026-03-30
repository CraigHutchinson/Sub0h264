/** Sub0h264 ESP32-P4 unit test runner
 *
 *  Runs the SAME doctest test suite as the desktop build on ESP32-P4.
 *  Test files are #included from tests/ — no separate test set.
 *  Test fixtures use embedded binary data via test_fixtures.hpp.
 *
 *  Platform-specific tests (PSRAM, stack HWM) are guarded by ESP_PLATFORM.
 *
 *  WDT strategy: idle task monitoring is disabled in sdkconfig.defaults
 *  (CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0=n) so the decode tests can
 *  monopolize the CPU without triggering HP_SYS_HP_WDT_RESET. The task
 *  WDT timeout is 120s which exceeds our longest test (~39s worst case).
 *  No vTaskDelay yields are needed — this preserves accurate timing.
 *
 *  SPDX-License-Identifier: MIT
 */
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// ── Doctest configuration for ESP-IDF ───────────────────────────────────
#define DOCTEST_CONFIG_IMPLEMENT
#define DOCTEST_CONFIG_NO_POSIX_SIGNALS
#define DOCTEST_CONFIG_NO_MULTITHREADING
#include "doctest.h"

// ── Include ALL desktop test files (shared test source) ─────────────────
// These files use getFixture() from test_fixtures.hpp which resolves to
// embedded binary data on ESP_PLATFORM and file I/O on desktop.

#include "../../../tests/test_version.cpp"
#include "../../../tests/test_bitstream.cpp"
#include "../../../tests/test_nal.cpp"
#include "../../../tests/test_sps_pps.cpp"
#include "../../../tests/test_slice.cpp"
#include "../../../tests/test_cavlc.cpp"
#include "../../../tests/test_iframe.cpp"
#include "../../../tests/test_pframe.cpp"
#include "../../../tests/test_deblock.cpp"
#include "../../../tests/test_cabac.cpp"
#include "../../../tests/test_decode_pipeline.cpp"
#include "../../../tests/test_bench.cpp"

// ── ESP32-P4 specific tests ─────────────────────────────────────────────

static const char* TAG = "sub0h264_test";

#ifdef ESP_PLATFORM

TEST_CASE("ESP32: PSRAM available and sufficient")
{
    size_t psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    ESP_LOGI(TAG, "PSRAM free: %u bytes", (unsigned)psram);
    /// Minimum PSRAM for decode buffers: DPB (4 frames x 460KB) + working memory.
    static constexpr size_t cMinPsramBytes = 2U * 1024U * 1024U;
    CHECK(psram >= cMinPsramBytes);
}

#endif // ESP_PLATFORM

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

    ESP_LOGI(TAG, "Free heap after tests: %u bytes",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_DEFAULT));
    ESP_LOGI(TAG, "Free PSRAM after tests: %u bytes",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    UBaseType_t hwm = uxTaskGetStackHighWaterMark(nullptr);
    ESP_LOGI(TAG, "Stack high-water mark: %u bytes remaining",
             (unsigned)(hwm * sizeof(StackType_t)));
}
