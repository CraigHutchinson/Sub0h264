// Standalone tool: trace per-block bit offsets for MB(0,0) of bouncing ball.
// Build: g++ -O2 -std=c++17 -DSUB0H264_TRACE=1 -I components/sub0h264/include -o build/trace_bb scripts/trace_bb_mb0_blocks.cpp
// Usage: build/trace_bb tests/fixtures/bouncing_ball.h264

#define SUB0H264_TRACE 1
#include "../components/sub0h264/src/decoder.hpp"
#include <cstdio>
#include <fstream>
#include <vector>

using namespace sub0h264;

static std::vector<uint8_t> loadFile(const char* path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return {};
    auto sz = f.tellg();
    f.seekg(0);
    std::vector<uint8_t> d(static_cast<size_t>(sz));
    f.read(reinterpret_cast<char*>(d.data()), sz);
    return d;
}

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "Usage: %s <h264>\n", argv[0]); return 1; }
    auto h264 = loadFile(argv[1]);

    // Enable trace for MB(0,0) only
    H264Decoder dec;
    dec.trace().enabled = true;
    dec.trace().filterMbX = 0;
    dec.trace().filterMbY = 0;

    std::vector<NalBounds> bounds;
    findNalUnits(h264.data(), static_cast<uint32_t>(h264.size()), bounds);

    for (const auto& b : bounds) {
        NalUnit nal;
        if (!parseNalUnit(h264.data() + b.offset, b.size, nal)) continue;
        if (dec.processNal(nal) == DecodeStatus::FrameDecoded) {
            // Dump frame 0
            const Frame* f = dec.currentFrame();
            if (f && argc >= 3) {
                FILE* out = std::fopen(argv[2], "wb");
                if (out) {
                    for (uint32_t r = 0; r < f->height(); ++r)
                        std::fwrite(f->yRow(r), 1, f->width(), out);
                    for (uint32_t r = 0; r < f->height()/2; ++r)
                        std::fwrite(f->uRow(r), 1, f->width()/2, out);
                    for (uint32_t r = 0; r < f->height()/2; ++r)
                        std::fwrite(f->vRow(r), 1, f->width()/2, out);
                    std::fclose(out);
                    std::printf("Dumped frame to %s\n", argv[2]);
                }
            }
            break;
        }
    }
    return 0;
}
