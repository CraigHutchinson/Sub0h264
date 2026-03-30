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
#include <vector>

#ifdef ESP_PLATFORM

// ── ESP32: Embedded binary data (linked by target_add_binary_data) ──────

extern const uint8_t flat_black_640x480_h264_start[] asm("_binary_flat_black_640x480_h264_start");
extern const uint8_t flat_black_640x480_h264_end[]   asm("_binary_flat_black_640x480_h264_end");

extern const uint8_t baseline_640x480_short_h264_start[] asm("_binary_baseline_640x480_short_h264_start");
extern const uint8_t baseline_640x480_short_h264_end[]   asm("_binary_baseline_640x480_short_h264_end");

extern const uint8_t high_640x480_h264_start[] asm("_binary_high_640x480_h264_start");
extern const uint8_t high_640x480_h264_end[]   asm("_binary_high_640x480_h264_end");

/** Get test fixture data by name. Returns pointer + size for embedded data. */
inline std::vector<uint8_t> getFixture(const char* name)
{
    // Match by fixture name (filename without path)
    const char* n = name;
    // Skip any path prefix
    for (const char* p = name; *p; ++p)
        if (*p == '/' || *p == '\\') n = p + 1;

    if (__builtin_strcmp(n, "flat_black_640x480.h264") == 0)
        return {flat_black_640x480_h264_start, flat_black_640x480_h264_end};
    if (__builtin_strcmp(n, "baseline_640x480_short.h264") == 0)
        return {baseline_640x480_short_h264_start, baseline_640x480_short_h264_end};
    if (__builtin_strcmp(n, "high_640x480.h264") == 0)
        return {high_640x480_h264_start, high_640x480_h264_end};

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
