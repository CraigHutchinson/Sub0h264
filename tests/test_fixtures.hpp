/** Sub0h264 — Test fixture provider (cross-platform)
 *
 *  Provides H.264 test vector data on both desktop (file I/O) and
 *  ESP32-P4 (embedded binary data in flash). Test files #include this
 *  header and call getFixture() instead of loadFile().
 *
 *  SPDX-License-Identifier: MIT
 */
#ifndef CROG_SUB0H264_TEST_FIXTURES_HPP
#define CROG_SUB0H264_TEST_FIXTURES_HPP

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <vector>

#ifdef ESP_PLATFORM

// ── ESP32: Embedded binary data (linked by target_add_binary_data) ──────
// All *.h264 fixtures from tests/fixtures/ are auto-embedded via CMake glob.
// Each fixture needs an extern declaration and a lookup entry below.

#define DECLARE_FIXTURE(name) \
    extern const uint8_t _binary_##name##_h264_start[] asm("_binary_" #name "_h264_start"); \
    extern const uint8_t _binary_##name##_h264_end[]   asm("_binary_" #name "_h264_end");

DECLARE_FIXTURE(baseline_640x480)
DECLARE_FIXTURE(baseline_640x480_short)
DECLARE_FIXTURE(bouncing_ball)
DECLARE_FIXTURE(bouncing_ball_ionly)
DECLARE_FIXTURE(cabac_flat_main)
DECLARE_FIXTURE(cabac_idr_only)
DECLARE_FIXTURE(flat_black_640x480)
DECLARE_FIXTURE(gradient_pan)
DECLARE_FIXTURE(gradient_pan_high)
DECLARE_FIXTURE(gradient_pan_ionly)
DECLARE_FIXTURE(high_640x480)
DECLARE_FIXTURE(pan_down)
DECLARE_FIXTURE(pan_fast_diag)
DECLARE_FIXTURE(pan_left)
DECLARE_FIXTURE(pan_slow)
DECLARE_FIXTURE(pan_up)
DECLARE_FIXTURE(scrolling_texture)
DECLARE_FIXTURE(scrolling_texture_high)
DECLARE_FIXTURE(scrolling_texture_ionly)
DECLARE_FIXTURE(static_scene)

#undef DECLARE_FIXTURE

/** Get test fixture data by name. Returns pointer + size for embedded data. */
inline std::vector<uint8_t> getFixture(const char* name)
{
    const char* n = name;
    for (const char* p = name; *p; ++p)
        if (*p == '/' || *p == '\\') n = p + 1;

#define MATCH_FIXTURE(fname) \
    if (std::strcmp(n, #fname ".h264") == 0) \
        return {_binary_##fname##_h264_start, _binary_##fname##_h264_end};

    MATCH_FIXTURE(baseline_640x480)
    MATCH_FIXTURE(baseline_640x480_short)
    MATCH_FIXTURE(bouncing_ball)
    MATCH_FIXTURE(bouncing_ball_ionly)
    MATCH_FIXTURE(cabac_flat_main)
    MATCH_FIXTURE(cabac_idr_only)
    MATCH_FIXTURE(flat_black_640x480)
    MATCH_FIXTURE(gradient_pan)
    MATCH_FIXTURE(gradient_pan_high)
    MATCH_FIXTURE(gradient_pan_ionly)
    MATCH_FIXTURE(high_640x480)
    MATCH_FIXTURE(pan_down)
    MATCH_FIXTURE(pan_fast_diag)
    MATCH_FIXTURE(pan_left)
    MATCH_FIXTURE(pan_slow)
    MATCH_FIXTURE(pan_up)
    MATCH_FIXTURE(scrolling_texture)
    MATCH_FIXTURE(scrolling_texture_high)
    MATCH_FIXTURE(scrolling_texture_ionly)
    MATCH_FIXTURE(static_scene)

#undef MATCH_FIXTURE

    return {}; // Unknown fixture
}

#else

// ── Desktop: File I/O ───────────────────────────────────────────────────

#include <fstream>
#include <string>

#ifndef SUB0H264_TEST_FIXTURES_DIR
#error "SUB0H264_TEST_FIXTURES_DIR must be defined (path to tests/fixtures/)"
#endif

/** Get test fixture data by name. Reads from filesystem. */
inline std::vector<uint8_t> getFixture(const char* name)
{
    std::string path = std::string(SUB0H264_TEST_FIXTURES_DIR) + "/" + name;
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.is_open())
        return {};
    auto size = f.tellg();
    f.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(static_cast<size_t>(size));
    f.read(reinterpret_cast<char*>(data.data()), size);
    return data;
}

#endif // ESP_PLATFORM

#endif // CROG_SUB0H264_TEST_FIXTURES_HPP
