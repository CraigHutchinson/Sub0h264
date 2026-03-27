/** Sub0h264 — Decoded Picture Buffer (DPB)
 *
 *  Manages reference frames for inter prediction. Supports short-term
 *  reference marking for P-frame decoding.
 *
 *  Reference: ITU-T H.264 §8.2.5
 *
 *  SPDX-License-Identifier: MIT
 */
#ifndef CROG_SUB0H264_DPB_HPP
#define CROG_SUB0H264_DPB_HPP

#include "frame.hpp"
#include "sps.hpp"

#include <cstdint>
#include <vector>
#include <algorithm>

namespace sub0h264 {

/// Maximum DPB size (reference frames + current).
inline constexpr uint32_t cMaxDpbSize = 17U;

/** Entry in the Decoded Picture Buffer. */
struct DpbEntry
{
    Frame frame;
    uint16_t frameNum = 0U;       ///< H.264 frame_num
    int32_t picOrderCnt = 0;      ///< Picture order count
    bool isReference = false;     ///< True if used as reference
    bool isLongTerm = false;      ///< True if long-term reference
    bool isOutput = false;        ///< True if ready for display
    bool occupied = false;        ///< True if this slot is in use
};

/** Simplified DPB for Baseline/P-frame decoding. */
class Dpb
{
public:
    /** Initialize DPB for given SPS parameters. */
    void init(uint16_t width, uint16_t height, uint8_t numRefFrames) noexcept
    {
        width_ = width;
        height_ = height;
        maxRefFrames_ = numRefFrames;
        entries_.resize(numRefFrames + 1U);
        for (auto& e : entries_)
        {
            e = DpbEntry{};
            e.frame.allocate(width, height);
        }
    }

    /** Get a free slot for the next decoded frame.
     *  If DPB is full, bumps the oldest short-term reference.
     *  @return Pointer to the frame buffer to decode into.
     */
    Frame* getDecodeTarget() noexcept
    {
        // Find a free slot
        for (auto& e : entries_)
        {
            if (!e.occupied)
            {
                e.occupied = true;
                currentEntry_ = &e;
                return &e.frame;
            }
        }

        // DPB full: evict oldest short-term reference (FIFO)
        DpbEntry* oldest = nullptr;
        for (auto& e : entries_)
        {
            if (e.occupied && e.isReference && !e.isLongTerm)
            {
                if (!oldest || e.frameNum < oldest->frameNum)
                    oldest = &e;
            }
        }

        if (oldest)
        {
            oldest->isReference = false;
            oldest->occupied = false;
            oldest->occupied = true;
            currentEntry_ = oldest;
            return &oldest->frame;
        }

        // Last resort: use first entry
        currentEntry_ = &entries_[0];
        return &entries_[0].frame;
    }

    /** Mark the current frame as a short-term reference. */
    void markAsReference(uint16_t frameNum) noexcept
    {
        if (currentEntry_)
        {
            currentEntry_->frameNum = frameNum;
            currentEntry_->isReference = true;
            currentEntry_->isLongTerm = false;
            currentEntry_->isOutput = true;
        }
    }

    /** Mark all references as unused (IDR reset). */
    void flush() noexcept
    {
        for (auto& e : entries_)
        {
            e.isReference = false;
            e.occupied = false;
        }
        currentEntry_ = nullptr;
    }

    /** Get reference frame by index (L0 list, 0-based).
     *  For simple P-frame decode, index 0 is the most recent reference.
     *  @return Pointer to reference frame, or nullptr.
     */
    const Frame* getReference(uint8_t refIdx) const noexcept
    {
        // Build ref list: short-term references sorted by frameNum descending
        std::vector<const DpbEntry*> refList;
        for (const auto& e : entries_)
        {
            if (e.occupied && e.isReference)
                refList.push_back(&e);
        }

        // Sort by frameNum descending (most recent first)
        std::sort(refList.begin(), refList.end(),
                  [](const DpbEntry* a, const DpbEntry* b) {
                      return a->frameNum > b->frameNum;
                  });

        if (refIdx < refList.size())
            return &refList[refIdx]->frame;

        return nullptr;
    }

    /** @return The most recently decoded frame. */
    const Frame* currentFrame() const noexcept
    {
        return currentEntry_ ? &currentEntry_->frame : nullptr;
    }

    /** @return Number of active reference frames. */
    uint32_t numReferences() const noexcept
    {
        uint32_t count = 0U;
        for (const auto& e : entries_)
            if (e.occupied && e.isReference)
                ++count;
        return count;
    }

private:
    std::vector<DpbEntry> entries_;
    DpbEntry* currentEntry_ = nullptr;
    uint16_t width_ = 0U;
    uint16_t height_ = 0U;
    uint8_t maxRefFrames_ = 0U;
};

} // namespace sub0h264

#endif // CROG_SUB0H264_DPB_HPP
