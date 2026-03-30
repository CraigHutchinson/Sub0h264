/** Sub0h264 ESP32-P4 unit test runner
 *
 *  Provides the doctest entry point for ESP32-P4. Shared test files
 *  are compiled as separate TUs by the CMake component (from
 *  tests/test_sources.cmake) — no #include of .cpp files needed.
 *
 *  Platform-specific tests (PSRAM) are guarded by ESP_PLATFORM.
 *
 *  WDT: idle task monitoring disabled in sdkconfig.defaults so decode
 *  tests can monopolize the CPU. 120s task WDT timeout. No yields.
 *
 *  SPDX-License-Identifier: MIT
 */
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define DOCTEST_CONFIG_IMPLEMENT
#define DOCTEST_CONFIG_NO_POSIX_SIGNALS
#define DOCTEST_CONFIG_NO_MULTITHREADING
#include "doctest.h"

// ── ESP32-P4 specific tests ─────────────────────────────────────────────

static const char* TAG = "sub0h264_test";

TEST_CASE("ESP32: PSRAM available and sufficient")
{
    size_t psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    ESP_LOGI(TAG, "PSRAM free: %u bytes", (unsigned)psram);
    /// Minimum PSRAM for decode buffers: DPB (4 frames x 460KB) + working memory.
    static constexpr size_t cMinPsramBytes = 2U * 1024U * 1024U;
    CHECK(psram >= cMinPsramBytes);
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

    ESP_LOGI(TAG, "Free heap after tests: %u bytes",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_DEFAULT));
    ESP_LOGI(TAG, "Free PSRAM after tests: %u bytes",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    UBaseType_t hwm = uxTaskGetStackHighWaterMark(nullptr);
    ESP_LOGI(TAG, "Stack high-water mark: %u bytes remaining",
             (unsigned)(hwm * sizeof(StackType_t)));
}
