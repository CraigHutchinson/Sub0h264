/** Sub0h264 — Structured decode trace system
 *
 *  Compile-time + runtime filtered tracing for decoder debugging.
 *  Zero cost when SUB0H264_TRACE is not defined (all calls compile away).
 *  When enabled, supports runtime MB/block filtering so only the
 *  macroblocks of interest produce output.
 *
 *  Build with trace: cmake --preset debug-trace
 *  Or: -DSUB0H264_TRACE=1
 *
 *  Usage in decoder:
 *    if (trace_.shouldTrace(mbX, mbY))
 *        trace_.log("MB(%u,%u) mbType=%u bitOff=%u", mbX, mbY, ...);
 *
 *  SPDX-License-Identifier: MIT
 */
#ifndef CROG_SUB0H264_DECODE_TRACE_HPP
#define CROG_SUB0H264_DECODE_TRACE_HPP

#include <cstdint>
#include <cstdio>
#include <cstdarg>

namespace sub0h264 {

/// Sentinel value: trace all MBs/blocks (no filter).
inline constexpr uint32_t cTraceAll = UINT32_MAX;

#if SUB0H264_TRACE

/** Runtime trace filter + logger.
 *  Set mbX/mbY to filter specific macroblocks.
 *  Set blkIdx to filter specific 4x4 blocks within a MB.
 *  Leave as cTraceAll to trace everything.
 */
struct DecodeTrace
{
    uint32_t filterMbX = cTraceAll;   ///< Only trace this MB X (cTraceAll = all)
    uint32_t filterMbY = cTraceAll;   ///< Only trace this MB Y (cTraceAll = all)
    uint32_t filterBlk = cTraceAll;   ///< Only trace this block index (cTraceAll = all)
    bool enabled = true;              ///< Master enable

    /** Should we trace this macroblock? */
    bool shouldTrace(uint32_t mbX, uint32_t mbY) const noexcept
    {
        if (!enabled) return false;
        if (filterMbX != cTraceAll && filterMbX != mbX) return false;
        if (filterMbY != cTraceAll && filterMbY != mbY) return false;
        return true;
    }

    /** Should we trace this specific block within an MB? */
    bool shouldTraceBlock(uint32_t mbX, uint32_t mbY, uint32_t blk) const noexcept
    {
        if (!shouldTrace(mbX, mbY)) return false;
        if (filterBlk != cTraceAll && filterBlk != blk) return false;
        return true;
    }

    /** Log a formatted trace message (printf-style). */
    void log(const char* fmt, ...) const noexcept
    {
        std::va_list args;
        va_start(args, fmt);
        std::printf("[TRACE] ");
        std::vprintf(fmt, args);
        std::printf("\n");
        va_end(args);
    }
};

#else // SUB0H264_TRACE not defined

/** No-op trace — compiles away completely. */
struct DecodeTrace
{
    uint32_t filterMbX = cTraceAll;
    uint32_t filterMbY = cTraceAll;
    uint32_t filterBlk = cTraceAll;
    bool enabled = false;

    constexpr bool shouldTrace(uint32_t, uint32_t) const noexcept { return false; }
    constexpr bool shouldTraceBlock(uint32_t, uint32_t, uint32_t) const noexcept { return false; }

    // No-op log — optimizer removes entirely since shouldTrace returns false
    void log(const char*, ...) const noexcept {}
};

#endif // SUB0H264_TRACE

} // namespace sub0h264

#endif // CROG_SUB0H264_DECODE_TRACE_HPP
