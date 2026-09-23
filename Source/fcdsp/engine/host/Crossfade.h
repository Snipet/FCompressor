#pragma once

// Mode and kernel switching (01 §5.5; E §5.1; K2 #3, #22). A change of the KernelKey at a block start runs a 20 ms
// equal-gain crossfade between two engine paths; each path keeps its own frozen parameters and gain staging, so
// neither is driven by the other Mode's (K2 #3a-b). Crossfade starts are at least kMinFadeGapMs apart, counted in
// samples (deterministic); a newer request during a fade or within the gap is latched, latest wins.
//
// Switch sequence: construct the new engine into the idle arena slot (prepare, setParams, snapParams; its gain
// smoother snapped), seed it from the active engine's carry() (if carry.msDomain differs from the new engine's lane
// domain, every lane of grDb, detDb and s2GrDb first takes max(lane0, lane1): never under-compresses, K2 #3d), run
// both for kFadeMs, then destroy the old engine with a plain ~IEngine() and swap the path pointers.
//
// Sprint-frozen types. F7 (S6) owns this header after S0 and adds the crossfade logic as bodies/helpers.
//
// ==== The F7 helpers (S6) ============================================================================================
// kernelKey()      the block's key {slot, topo, det, stmode, voice, keyExt} (01 §5.4 step 1; keyExt = extKey AND an
//                  active key bus, K2 #22).
// fadeGapSamples() kMinFadeGapMs in base-rate samples, rounded up (2400 at 48 kHz, K2 #22).
// FadeClock        when a crossfade may start and how far it has run. The weight is a LinearRamp at the OS rate over
//                  kFadeMs (01 §5.4 step 2f: "linear per OS sample over 20 ms", equal gain); host::blend() applies the
//                  host ramp shape to its position (host/Ramps.h: smootherstep, S6 lead revision 2), so the fade lands
//                  exactly on 1 and blend() then returns the incoming path bit for bit. Starts are counted on the
//                  host's absolute base-rate sample index: a start is allowed when no fade runs and at least
//                  fadeGapSamples() have passed since the previous start. The latch is the host's re-evaluation at
//                  every block start: a request that may not start yet is simply seen again at the next block, with
//                  that block's (latest) parameters.
// Everything here is header-inline, allocation-free and FCDSP_NONBLOCKING; prepare() runs in EngineHost::configure.

#include "fcdsp/core/Rt.h"
#include "fcdsp/core/Smoother.h"
#include "fcdsp/engine/IEngine.h"
#include "fcdsp/engine/host/Ramps.h"
#include "fcdsp/params/EngineParams.h"
#include <cstdint>

namespace fcdsp {

struct KernelKey { uint8_t slot, topo, det, stmode, voice, keyExt; bool operator==(const KernelKey&) const = default; };

struct PathState {                                    // one per arena slot, owned by EngineHost
    IEngine*     engine = nullptr;
    KernelKey    key{};
    EngineParams eng{};                               // incoming path: updated every block; outgoing: FROZEN at its last value
    Smoother4    gain{};                              // {preGainDb, makeupTotalDb (= makeupDb + engine->autoMakeupDb()), -, -}
};

inline constexpr float kFadeMs = 20.f;
inline constexpr float kMinFadeGapMs = 50.f;          // between crossfade STARTS, counted in samples (deterministic)

namespace host {

// The kernel key of a block (01 §5.4 step 1): the effective Mode slot, the kernel-selecting values and the effective
// external key.
inline KernelKey kernelKey(uint8_t slot, const EngineParams& e, bool keyExt) noexcept FCDSP_NONBLOCKING
{
    return KernelKey{ slot, e.topo, e.det, e.stmode, e.voice, static_cast<uint8_t>(keyExt ? 1 : 0) };
}

// kMinFadeGapMs in base-rate samples, rounded up; at least 1.
inline uint64_t fadeGapSamples(double fs) noexcept FCDSP_NONBLOCKING
{
    const double samples = static_cast<double>(kMinFadeGapMs) * (fs > 0.0 ? fs : 48000.0) / 1000.0;
    auto whole = static_cast<uint64_t>(samples);
    if (static_cast<double>(whole) < samples)
        ++whole;
    return whole > 0 ? whole : 1;
}

class FadeClock {
public:
    // The weight ramp at the OS rate (kFadeMs long) and the start gap at the base rate; no fade runs and none ran.
    void prepare(float fsOs, double fs) noexcept FCDSP_NONBLOCKING
    {
        ramp_.prepare(fsOs, kFadeMs);
        gap_ = fadeGapSamples(fs);
        reset();
    }

    // No fade runs, and the next request may start at once (configure, a poison reset).
    void reset() noexcept FCDSP_NONBLOCKING
    {
        ramp_.cur = ramp_.tgt = 1.0f;
        running_ = false;
        started_ = false;
        lastStart_ = 0;
    }

    bool running() const noexcept FCDSP_NONBLOCKING { return running_; }

    // A start is allowed at base-rate sample `index`: no fade runs and the previous start is >= the gap ago.
    bool mayStart(uint64_t index) const noexcept FCDSP_NONBLOCKING
    {
        return !running_ && (!started_ || index - lastStart_ >= gap_);
    }

    void start(uint64_t index) noexcept FCDSP_NONBLOCKING
    {
        ramp_.cur = 0.0f;
        ramp_.tgt = 1.0f;
        running_ = true;
        started_ = true;
        lastStart_ = index;
    }

    // The fade is over (it landed, or the host abandoned it): the incoming path alone from here.
    void finish() noexcept FCDSP_NONBLOCKING
    {
        ramp_.cur = ramp_.tgt = 1.0f;
        running_ = false;
    }

    // n OS samples of the fade's linear position (0 -> 1; host::blend(outgoing, incoming, t) applies the shape).
    // Returns true when the position is exactly 1 at the last of them: the outgoing path contributes nothing from then.
    bool positions(float* t, int n) noexcept FCDSP_NONBLOCKING
    {
        for (int i = 0; i < n; ++i)
            t[i] = ramp_.tick();
        return ramp_.cur >= 1.0f;
    }

    float position() const noexcept FCDSP_NONBLOCKING { return running_ ? ramp_.cur : 1.0f; }
    uint64_t gapSamples() const noexcept FCDSP_NONBLOCKING { return gap_; }
    uint64_t lastStart() const noexcept FCDSP_NONBLOCKING { return lastStart_; }

private:
    LinearRamp ramp_{};
    uint64_t gap_ = 2400, lastStart_ = 0;
    bool running_ = false, started_ = false;
};

} // namespace host

} // namespace fcdsp
