#pragma once

// Integer delay lines for the host (01 §5.4 steps 2c and 2i; E §5.2-5.4; K2 #11b, #21a): the lookahead delay of the
// main signal (L_la), the side-chain delay (L_la - look + D_up(quality) - engine.scDelaySamples(), floored at 0), the
// latency-aligned dry signal of the bypass ramp and the delayed SC of SC listen. A pure component with one owner (K3
// #12: F5); EngineHost.cpp (F7) orchestrates it.
//
// DelayLine<T> is one stream of T: float for an audio channel (one line per channel), simd::f32x4 for the side chain's
// lanes {c0, c1, aux0, aux1}. A power-of-two ring written before it is read, so delay 0 returns the input bit for bit,
// and a delay of d returns the input of d samples ago bit for bit (01 §5.8: the lines hold sanitised input only).
//
// Buffers come from configure() only (the ONLY allocation, on the thread that runs EngineHost::configure); process(),
// reset() and the delay setters never allocate. Unconfigured, a line passes its input through (delay 0).
//
// Two ways to set the delay:
//   setDelay(d)   jumps (configure, snap, a Quality/budget change: the latency changes there anyway);
//   setTarget(d)  slews one sample per control tick toward d, at ABSOLUTE sample-index multiples of kTickSamples, so the
//                 output does not depend on the host block size. The SC read position moves by at most one sample per
//                 tick when `look` is automated (K2 #21 Brickwall owner spec), one repeated or skipped SC sample at a
//                 time, and moving `look` never changes the latency (the main delay is fixed; 01 §5.4 step 2c).
// Both clamp to [0, maxDelay()].
//
// Real time: every member but configure() is inline and FCDSP_NONBLOCKING.

#include "fcdsp/core/ControlTicker.h"
#include "fcdsp/core/Rt.h"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace fcdsp::host {

template <class T>
class DelayLine {
public:
    // Allocates a zeroed ring of nextPow2(maxDelay + 1) elements; the delay starts at 0. Not real-time.
    void configure(int maxDelay)
    {
        max_ = maxDelay > 0 ? maxDelay : 0;
        std::size_t cap = 1;
        while (cap < static_cast<std::size_t>(max_) + 1u)
            cap <<= 1;
        buf_.assign(cap, T{});
        mask_ = static_cast<uint32_t>(cap - 1u);
        w_ = 0;
        cur_ = tgt_ = 0;
    }

    int maxDelay() const noexcept FCDSP_NONBLOCKING { return max_; }
    int delay() const noexcept FCDSP_NONBLOCKING { return cur_; }
    int target() const noexcept FCDSP_NONBLOCKING { return tgt_; }

    // Silence: the ring zeroed (bounded, O(capacity)) and the write position restarted; the delays stay.
    void reset() noexcept FCDSP_NONBLOCKING
    {
        for (T& v : buf_)
            v = T{};
        w_ = 0;
    }

    // Jump: the delay and its target become d (clamped).
    void setDelay(int d) noexcept FCDSP_NONBLOCKING
    {
        cur_ = tgt_ = clampDelay(d);
    }

    // Slew toward d (clamped), one sample per control tick.
    void setTarget(int d) noexcept FCDSP_NONBLOCKING { tgt_ = clampDelay(d); }

    // n samples; out may alias in. sampleIndex is the absolute index of in[0] (the slew's tick grid).
    void process(const T* in, T* out, int n, uint64_t sampleIndex) noexcept FCDSP_NONBLOCKING
    {
        if (buf_.empty())
        {
            if (in != out)
                for (int i = 0; i < n; ++i)
                    out[i] = in[i];
            return;
        }
        constexpr uint64_t kTick = static_cast<uint64_t>(kTickSamples);
        T* const b = buf_.data();
        for (int i = 0; i < n; ++i)
        {
            if (cur_ != tgt_ && (sampleIndex + static_cast<uint64_t>(i)) % kTick == 0)
                cur_ += tgt_ > cur_ ? 1 : -1;
            b[w_] = in[i];
            out[i] = b[(w_ - static_cast<uint32_t>(cur_)) & mask_];
            w_ = (w_ + 1u) & mask_;
        }
    }

private:
    int clampDelay(int d) const noexcept FCDSP_NONBLOCKING { return d > 0 ? (d < max_ ? d : max_) : 0; }

    std::vector<T> buf_;
    uint32_t mask_ = 0, w_ = 0;
    int max_ = 0, cur_ = 0, tgt_ = 0;
};

} // namespace fcdsp::host
