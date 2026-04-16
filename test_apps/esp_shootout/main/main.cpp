/** ESP32-P4 Shootout — Sub0h264 vs libavc performance comparison
 *
 *  Decodes embedded benchmark fixtures with both decoders on ESP32-P4.
 *  Reports comparative fps via serial output.
 *
 *  SPDX-License-Identifier: MIT
 */
#include "decoder.hpp"
#include "annexb.hpp"
#include "decode_timing.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

extern "C" {
#include "ih264_typedefs.h"
#include "ih264d.h"
#include "iv.h"
#include "ivd.h"
}

#include "esp_heap_caps.h"
#include "esp_log.h"

static const char* TAG = "shootout";

using namespace sub0h264;

// ── Embedded fixture access ────────────────────────────────────────────

#define DECLARE_FIXTURE(name) \
    extern const uint8_t _binary_##name##_h264_start[] asm("_binary_" #name "_h264_start"); \
    extern const uint8_t _binary_##name##_h264_end[]   asm("_binary_" #name "_h264_end");

DECLARE_FIXTURE(bench_scroll_baseline_640x480)
DECLARE_FIXTURE(bench_scroll_high_640x480)
DECLARE_FIXTURE(bench_ball_baseline_640x480)
DECLARE_FIXTURE(bench_ball_high_640x480)
DECLARE_FIXTURE(bench_still_baseline_640x480)
DECLARE_FIXTURE(bench_still_high_640x480)
DECLARE_FIXTURE(flat_black_baseline_640x480)
DECLARE_FIXTURE(baseline_640x480_short)
DECLARE_FIXTURE(scrolling_texture_baseline)
DECLARE_FIXTURE(scrolling_texture_high)
DECLARE_FIXTURE(tapo_c110_stream2_high)

#undef DECLARE_FIXTURE

struct Fixture
{
    const char* id;
    const uint8_t* start;
    const uint8_t* end;
};

#define FIX(id, name) {id, _binary_##name##_h264_start, _binary_##name##_h264_end}

static const Fixture cFixtures[] = {
    FIX("Scroll-Base-640",  bench_scroll_baseline_640x480),
    FIX("Scroll-High-640",  bench_scroll_high_640x480),
    FIX("Ball-Base-640",    bench_ball_baseline_640x480),
    FIX("Ball-High-640",    bench_ball_high_640x480),
    FIX("Still-Base-640",   bench_still_baseline_640x480),
    FIX("Still-High-640",   bench_still_high_640x480),
    FIX("Flat-Base-640",    flat_black_baseline_640x480),
    FIX("CAVLC-Base-640",   baseline_640x480_short),
    FIX("CAVLC-320",        scrolling_texture_baseline),
    FIX("CABAC-320",        scrolling_texture_high),
    FIX("Tapo-C110",        tapo_c110_stream2_high),
};

// ── Sub0h264 benchmark ─────────────────────────────────────────────────

static constexpr uint32_t cPasses = 3U;

static double benchSub0h264(const uint8_t* data, uint32_t size, int32_t& outFrames)
{
    auto dec = std::make_unique<H264Decoder>();
    outFrames = dec->decodeStream(data, size);
    if (outFrames <= 0) return 0.0;

    // Warm-up
    dec = std::make_unique<H264Decoder>();
    dec->decodeStream(data, size);

    std::array<double, cPasses> ms = {};
    for (uint32_t p = 0U; p < cPasses; ++p)
    {
        dec = std::make_unique<H264Decoder>();
        int64_t t0 = sub0h264TimerUs();
        dec->decodeStream(data, size);
        ms[p] = (sub0h264TimerUs() - t0) / 1000.0;
    }
    std::sort(ms.begin(), ms.end());
    double med = ms[cPasses / 2];
    return (med > 0.0) ? (outFrames * 1000.0 / med) : 0.0;
}

// ── libavc benchmark ────────────────────────────────────────────────────

static double benchLibavc(const uint8_t* data, uint32_t size, int32_t expectedFrames)
{
    ih264d_create_ip_t createIn = {};
    ih264d_create_op_t createOut = {};
    createIn.s_ivd_create_ip_t.u4_size = sizeof(ih264d_create_ip_t);
    createIn.s_ivd_create_ip_t.e_cmd = IVD_CMD_CREATE;
    createIn.s_ivd_create_ip_t.u4_share_disp_buf = 0;
    createIn.s_ivd_create_ip_t.e_output_format = IV_YUV_420P;
    createIn.s_ivd_create_ip_t.pf_aligned_alloc = [](void*, WORD32 align, WORD32 sz) -> void* {
        return heap_caps_aligned_alloc(static_cast<size_t>(align),
                                       static_cast<size_t>(sz),
                                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    };
    createIn.s_ivd_create_ip_t.pf_aligned_free = [](void*, void* ptr) {
        heap_caps_free(ptr);
    };
    createIn.s_ivd_create_ip_t.pv_mem_ctxt = nullptr;
    createIn.u4_enable_frame_info = 0;
    createIn.u4_keep_threads_active = 0;
    createOut.s_ivd_create_op_t.u4_size = sizeof(ih264d_create_op_t);

    iv_obj_t* codec = nullptr;
    IV_API_CALL_STATUS_T status = ih264d_api_function(nullptr, &createIn, &createOut);
    if (status != IV_SUCCESS)
    {
        ESP_LOGE(TAG, "libavc CREATE failed: 0x%lx",
                 (unsigned long)createOut.s_ivd_create_op_t.u4_error_code);
        return 0.0;
    }
    codec = reinterpret_cast<iv_obj_t*>(createOut.s_ivd_create_op_t.pv_handle);
    codec->u4_size = sizeof(iv_obj_t);
    codec->pv_fxns = reinterpret_cast<void*>(ih264d_api_function);

    // Single-core for fair comparison
    ih264d_ctl_set_num_cores_ip_t coresIn = {};
    ih264d_ctl_set_num_cores_op_t coresOut = {};
    coresIn.e_cmd = IVD_CMD_VIDEO_CTL;
    coresIn.e_sub_cmd = static_cast<IVD_CONTROL_API_COMMAND_TYPE_T>(IH264D_CMD_CTL_SET_NUM_CORES);
    coresIn.u4_num_cores = 1;
    coresIn.u4_size = sizeof(coresIn);
    coresOut.u4_size = sizeof(coresOut);
    ih264d_api_function(codec, &coresIn, &coresOut);

    // Output buffers in PSRAM
    constexpr uint32_t maxW = 1920U;
    constexpr uint32_t maxH = 1088U;
    auto* yBuf = static_cast<uint8_t*>(heap_caps_malloc(maxW * maxH, MALLOC_CAP_SPIRAM));
    auto* uBuf = static_cast<uint8_t*>(heap_caps_malloc(maxW * maxH / 4, MALLOC_CAP_SPIRAM));
    auto* vBuf = static_cast<uint8_t*>(heap_caps_malloc(maxW * maxH / 4, MALLOC_CAP_SPIRAM));
    constexpr uint32_t mapSize = (maxW * maxH) >> 6;
    auto* qpMap = static_cast<uint8_t*>(heap_caps_malloc(mapSize, MALLOC_CAP_SPIRAM));
    auto* blkMap = static_cast<uint8_t*>(heap_caps_malloc(mapSize, MALLOC_CAP_SPIRAM));

    if (!yBuf || !uBuf || !vBuf || !qpMap || !blkMap)
    {
        ESP_LOGE(TAG, "PSRAM alloc failed for libavc output buffers");
        return 0.0;
    }

    auto decodeAll = [&]() -> int32_t {
        int32_t frames = 0;
        uint32_t offset = 0;
        while (offset < size)
        {
            ih264d_video_decode_ip_t decIn = {};
            ih264d_video_decode_op_t decOut = {};
            decIn.s_ivd_video_decode_ip_t.u4_size = sizeof(ih264d_video_decode_ip_t);
            decIn.s_ivd_video_decode_ip_t.e_cmd = IVD_CMD_VIDEO_DECODE;
            decIn.s_ivd_video_decode_ip_t.pv_stream_buffer = const_cast<uint8_t*>(data + offset);
            decIn.s_ivd_video_decode_ip_t.u4_num_Bytes = size - offset;
            decIn.s_ivd_video_decode_ip_t.s_out_buffer.u4_num_bufs = 3;
            decIn.s_ivd_video_decode_ip_t.s_out_buffer.pu1_bufs[0] = yBuf;
            decIn.s_ivd_video_decode_ip_t.s_out_buffer.pu1_bufs[1] = uBuf;
            decIn.s_ivd_video_decode_ip_t.s_out_buffer.pu1_bufs[2] = vBuf;
            decIn.s_ivd_video_decode_ip_t.s_out_buffer.u4_min_out_buf_size[0] = maxW * maxH;
            decIn.s_ivd_video_decode_ip_t.s_out_buffer.u4_min_out_buf_size[1] = maxW * maxH / 4;
            decIn.s_ivd_video_decode_ip_t.s_out_buffer.u4_min_out_buf_size[2] = maxW * maxH / 4;
            decIn.pu1_8x8_blk_qp_map = qpMap;
            decIn.pu1_8x8_blk_type_map = blkMap;
            decIn.u4_8x8_blk_qp_map_size = mapSize;
            decIn.u4_8x8_blk_type_map_size = mapSize;
            decOut.s_ivd_video_decode_op_t.u4_size = sizeof(ih264d_video_decode_op_t);

            ih264d_api_function(codec, &decIn, &decOut);
            auto& op = decOut.s_ivd_video_decode_op_t;
            if (op.u4_num_bytes_consumed == 0) break;
            offset += op.u4_num_bytes_consumed;
            if (op.u4_output_present) ++frames;
        }
        // Flush
        for (int f = 0; f < 32; ++f)
        {
            ih264d_video_decode_ip_t flIn = {};
            ih264d_video_decode_op_t flOut = {};
            flIn.s_ivd_video_decode_ip_t.u4_size = sizeof(ih264d_video_decode_ip_t);
            flIn.s_ivd_video_decode_ip_t.e_cmd = IVD_CMD_VIDEO_DECODE;
            flIn.s_ivd_video_decode_ip_t.pv_stream_buffer = nullptr;
            flIn.s_ivd_video_decode_ip_t.u4_num_Bytes = 0;
            flIn.s_ivd_video_decode_ip_t.s_out_buffer.u4_num_bufs = 3;
            flIn.s_ivd_video_decode_ip_t.s_out_buffer.pu1_bufs[0] = yBuf;
            flIn.s_ivd_video_decode_ip_t.s_out_buffer.pu1_bufs[1] = uBuf;
            flIn.s_ivd_video_decode_ip_t.s_out_buffer.pu1_bufs[2] = vBuf;
            flIn.s_ivd_video_decode_ip_t.s_out_buffer.u4_min_out_buf_size[0] = maxW * maxH;
            flIn.s_ivd_video_decode_ip_t.s_out_buffer.u4_min_out_buf_size[1] = maxW * maxH / 4;
            flIn.s_ivd_video_decode_ip_t.s_out_buffer.u4_min_out_buf_size[2] = maxW * maxH / 4;
            flOut.s_ivd_video_decode_op_t.u4_size = sizeof(ih264d_video_decode_op_t);
            ih264d_api_function(codec, &flIn, &flOut);
            if (flOut.s_ivd_video_decode_op_t.u4_output_present) ++frames;
            else break;
        }
        return frames;
    };

    // Validate
    int32_t valFrames = decodeAll();
    if (valFrames == 0)
    {
        ESP_LOGE(TAG, "libavc decoded 0 frames");
        goto cleanup;
    }

    {
        // Measured passes (reset between each)
        std::array<double, cPasses> ms = {};
        for (uint32_t p = 0; p < cPasses; ++p)
        {
            ivd_ctl_reset_ip_t rstIn = {};
            ivd_ctl_reset_op_t rstOut = {};
            rstIn.u4_size = sizeof(rstIn);
            rstIn.e_cmd = IVD_CMD_VIDEO_CTL;
            rstIn.e_sub_cmd = IVD_CMD_CTL_RESET;
            rstOut.u4_size = sizeof(rstOut);
            ih264d_api_function(codec, &rstIn, &rstOut);

            int64_t t0 = sub0h264TimerUs();
            decodeAll();
            ms[p] = (sub0h264TimerUs() - t0) / 1000.0;
        }

        // Cleanup
        ih264d_delete_ip_t delIn = {};
        ih264d_delete_op_t delOut = {};
        delIn.s_ivd_delete_ip_t.u4_size = sizeof(delIn);
        delIn.s_ivd_delete_ip_t.e_cmd = IVD_CMD_DELETE;
        delOut.s_ivd_delete_op_t.u4_size = sizeof(delOut);
        ih264d_api_function(codec, &delIn, &delOut);
        heap_caps_free(yBuf); heap_caps_free(uBuf); heap_caps_free(vBuf);
        heap_caps_free(qpMap); heap_caps_free(blkMap);

        std::sort(ms.begin(), ms.end());
        double med = ms[cPasses / 2];
        return (med > 0.0) ? (expectedFrames * 1000.0 / med) : 0.0;
    }

cleanup:
    ih264d_delete_ip_t delIn = {};
    ih264d_delete_op_t delOut = {};
    delIn.s_ivd_delete_ip_t.u4_size = sizeof(delIn);
    delIn.s_ivd_delete_ip_t.e_cmd = IVD_CMD_DELETE;
    delOut.s_ivd_delete_op_t.u4_size = sizeof(delOut);
    ih264d_api_function(codec, &delIn, &delOut);
    heap_caps_free(yBuf); heap_caps_free(uBuf); heap_caps_free(vBuf);
    heap_caps_free(qpMap); heap_caps_free(blkMap);
    return 0.0;
}

// ── Main ────────────────────────────────────────────────────────────────

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "Sub0h264 vs libavc Shootout on ESP32-P4");
    ESP_LOGI(TAG, "Free PSRAM: %lu bytes", (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    printf("\n%-20s %8s %10s %10s %8s\n", "Stream", "Frames", "sub0h264", "libavc", "Ratio");
    printf("%s\n", "--------------------------------------------------------------");

    for (const auto& fix : cFixtures)
    {
        uint32_t size = static_cast<uint32_t>(fix.end - fix.start);
        if (size == 0)
        {
            printf("%-20s SKIPPED (empty)\n", fix.id);
            continue;
        }

        int32_t frames = 0;
        double sub0fps = benchSub0h264(fix.start, size, frames);

        double libavcFps = benchLibavc(fix.start, size, frames);

        double ratio = (libavcFps > 0.0) ? (sub0fps / libavcFps) : 0.0;
        printf("%-20s %5ld %8.1f fps %8.1f fps %6.2fx\n",
               fix.id, static_cast<long>(frames), sub0fps, libavcFps, ratio);

        printf("SHOOTOUT_JSON {\"stream\":\"%s\",\"decoder\":\"sub0h264\","
               "\"fps\":%.1f,\"frames\":%ld,\"platform\":\"esp32p4\"}\n",
               fix.id, sub0fps, static_cast<long>(frames));
        printf("SHOOTOUT_JSON {\"stream\":\"%s\",\"decoder\":\"libavc\","
               "\"fps\":%.1f,\"frames\":%ld,\"platform\":\"esp32p4\"}\n",
               fix.id, libavcFps, static_cast<long>(frames));
    }

    printf("\nShootout complete.\n");
}
