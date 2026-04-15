/** Sub0h264 — Lock-step entropy comparison tool
 *
 *  Decodes an H.264 Annex B bitstream with CABAC bin trace enabled for a
 *  target slice. The trace goes to stderr in a format that can be compared
 *  against the JM reference decoder's trace output.
 *
 *  Usage:
 *    sub0h264_lockstep <input.h264> [--slice N]
 *
 *  Options:
 *    --slice N   Which CABAC slice to trace (0=IDR, 1=first P-frame, etc.)
 *                Default: 1 (first P-frame after IDR).
 *
 *  Output (to stderr):
 *    OUR_SLICE_START slice=N R=510 O=...
 *    <binIdx> <pre_state_raw> <post_mpsState> <decoded_bit> <post_range> <post_offset> <ctxIdx>
 *    <binIdx> BP <decoded_bit> <post_range> <post_offset>  (bypass bins)
 *
 *  Compare against JM trace with:
 *    python scripts/lockstep_compare.py our_trace.txt jm_trace.txt
 *
 *  Requires SUB0H264_ENTROPY_TRACE=1 (set by CMakeLists.txt).
 */
#include "decoder.hpp"
#include "annexb.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>

using namespace sub0h264;

static std::vector<uint8_t> loadFile(const char* path)
{
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) { std::fprintf(stderr, "Cannot open: %s\n", path); return {}; }
    auto sz = f.tellg();
    f.seekg(0);
    std::vector<uint8_t> buf(static_cast<size_t>(sz));
    f.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(sz));
    return buf;
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::fprintf(stderr, "Usage: %s <input.h264> [--slice N]\n", argv[0]);
        std::fprintf(stderr, "  --slice N  Trace slice N (0=IDR, 1=first P-frame; default 1)\n");
        std::fprintf(stderr, "Outputs CABAC bin trace for the target slice to stderr.\n");
        std::fprintf(stderr, "Redirect stderr to file, then compare with:\n");
        std::fprintf(stderr, "  python scripts/lockstep_compare.py our_trace.txt jm_trace.txt\n");
        return 1;
    }

    const char* inputPath = argv[1];
    uint32_t targetSlice = 1U; // default: first P-frame

    for (int i = 2; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--slice") == 0 && i + 1 < argc)
        {
            targetSlice = static_cast<uint32_t>(std::atoi(argv[++i]));
        }
        else
        {
            std::fprintf(stderr, "Unknown argument: %s\n", argv[i]);
            return 1;
        }
    }

    auto data = loadFile(inputPath);
    if (data.empty()) return 1;

    std::fprintf(stderr, "# Loaded %zu bytes from %s\n", data.size(), inputPath);
    std::fprintf(stderr, "# Tracing CABAC bins for slice=%u\n", targetSlice);

    H264Decoder decoder;
    decoder.setEntropyTraceSlice(targetSlice);
    decoder.setEntropyTraceOutput(stderr);

    std::vector<NalBounds> bounds;
    findNalUnits(data.data(), static_cast<uint32_t>(data.size()), bounds);

    uint32_t framesDecoded = 0U;
    for (const auto& b : bounds)
    {
        NalUnit nal;
        if (!parseNalUnit(data.data() + b.offset, b.size, nal))
            continue;

        DecodeStatus status = decoder.processNal(nal);
        if (status == DecodeStatus::FrameDecoded)
        {
            ++framesDecoded;
            const Frame* frame = decoder.currentFrame();
            std::fprintf(stderr, "# Frame %u decoded (%ux%u)\n",
                framesDecoded,
                frame ? frame->width() : 0U,
                frame ? frame->height() : 0U);

            // Stop after we have decoded the frame containing the target slice.
            // Slice N corresponds to frame N (IDR=frame0, first P=frame1, etc.)
            if (framesDecoded > targetSlice)
                break;
        }
    }

    std::fprintf(stderr, "# Done. %u frame(s) decoded.\n", framesDecoded);
    return 0;
}
