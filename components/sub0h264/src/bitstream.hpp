/** Sub0h264 — Bit-level reader + Exp-Golomb decoder
 *
 *  Reads from a byte buffer at bit granularity. All H.264 bitstream
 *  parsing (headers, entropy, etc.) builds on this class.
 *
 *  SPDX-License-Identifier: MIT
 */
#ifndef CROG_SUB0H264_BITSTREAM_HPP
#define CROG_SUB0H264_BITSTREAM_HPP

#include <cstdint>
#include <cstring>

#ifdef _MSC_VER
#include <intrin.h>
#endif

namespace sub0h264 {

/// Sentinel value returned by readUev() on overflow (>31 leading zeros).
inline constexpr uint32_t cExpGolombOverflow = 0xFFFFFFFFU;

/** Bit-level reader over a byte buffer.
 *
 *  Data is read MSB-first (network/big-endian bit order).
 *  The buffer does NOT need to be 32-bit aligned — we handle unaligned access.
 */
class BitReader
{
public:
    BitReader() noexcept = default;

    /** Construct from raw byte buffer.
     *  @param data   Pointer to bitstream data
     *  @param sizeBytes  Size in bytes
     */
    BitReader(const uint8_t* data, uint32_t sizeBytes) noexcept
        : data_(data)
        , sizeBytes_(sizeBytes)
        , bitOffset_(0U)
    {}

    /** Read N bits (1-32) and advance the offset. */
    uint32_t readBits(uint32_t n) noexcept
    {
        uint32_t val = peekBits(n);
        bitOffset_ += n;
        return val;
    }

    /** Peek N bits (1-32) without advancing the offset. */
    uint32_t peekBits(uint32_t n) const noexcept
    {
        if (n == 0U)
            return 0U;

        uint32_t byteOff = bitOffset_ >> 3U;
        uint32_t bitPos  = bitOffset_ & 7U;

        // Read up to 5 bytes into a 40-bit accumulator.
        // Bytes are placed MSB-first: data[byteOff] at bits [39:32], etc.
        uint64_t accum = 0U;
        uint32_t bytesRead = 0U;
        for (uint32_t i = 0U; i < 5U && (byteOff + i) < sizeBytes_; ++i)
        {
            accum = (accum << 8U) | data_[byteOff + i];
            ++bytesRead;
        }
        // Pad to fill the 40-bit window if we hit the end of the buffer
        accum <<= (5U - bytesRead) * 8U;

        // Extract N bits starting at bitPos within the 40-bit window.
        // Bits are numbered [39..0] MSB-first. We want bits [39-bitPos .. 39-bitPos-n+1].
        uint32_t shift = 40U - bitPos - n;
        uint64_t mask  = (1ULL << n) - 1ULL;
        return static_cast<uint32_t>((accum >> shift) & mask);
    }

    /** Skip N bits. */
    void skipBits(uint32_t n) noexcept
    {
        bitOffset_ += n;
    }

    /** Read a single bit. */
    uint32_t readBit() noexcept
    {
        return readBits(1U);
    }

    /** Read unsigned Exp-Golomb coded value: ue(v).
     *
     *  Encoding: leadingZeros '1' suffix
     *  Value = (1 << leadingZeros) + suffix - 1
     */
    uint32_t readUev() noexcept
    {
        uint32_t leadingZeros = 0U;
        while (readBit() == 0U)
        {
            ++leadingZeros;
            if (leadingZeros > 31U)
                return cExpGolombOverflow;
        }
        if (leadingZeros == 0U)
            return 0U;
        uint32_t suffix = readBits(leadingZeros);
        return (1U << leadingZeros) - 1U + suffix;
    }

    /** Read signed Exp-Golomb coded value: se(v).
     *
     *  Maps: 0→0, 1→1, 2→-1, 3→2, 4→-2, ...
     */
    int32_t readSev() noexcept
    {
        uint32_t uval = readUev();
        if (uval == 0U)
            return 0;
        int32_t val = static_cast<int32_t>((uval + 1U) >> 1U);
        return (uval & 1U) ? val : -val;
    }

    /** Read truncated Exp-Golomb: te(v).
     *  When range == 1: read 1 bit, return !bit.
     *  Otherwise: same as ue(v).
     */
    uint32_t readTev(uint32_t range) noexcept
    {
        if (range <= 1U)
            return 1U - readBit();
        return readUev();
    }

    /** Check if the offset is byte-aligned. */
    bool isAligned() const noexcept
    {
        return (bitOffset_ & 7U) == 0U;
    }

    /** Advance to the next byte boundary. */
    void alignToByte() noexcept
    {
        bitOffset_ = (bitOffset_ + 7U) & ~7U;
    }

    /** @return Current bit offset from start. */
    uint32_t bitOffset() const noexcept { return bitOffset_; }

    /** @return Total bits available. */
    uint32_t totalBits() const noexcept { return sizeBytes_ * 8U; }

    /** @return True if there are at least N bits remaining. */
    bool hasBits(uint32_t n) const noexcept
    {
        return (bitOffset_ + n) <= (sizeBytes_ * 8U);
    }

    /** @return True if all bits have been consumed. */
    bool isExhausted() const noexcept
    {
        return bitOffset_ >= (sizeBytes_ * 8U);
    }

    /** @return Pointer to the underlying data. */
    const uint8_t* data() const noexcept { return data_; }

    /** @return Size in bytes. */
    uint32_t sizeBytes() const noexcept { return sizeBytes_; }

private:
    const uint8_t* data_ = nullptr;
    uint32_t sizeBytes_  = 0U;
    uint32_t bitOffset_  = 0U;
};

} // namespace sub0h264

#endif // CROG_SUB0H264_BITSTREAM_HPP
