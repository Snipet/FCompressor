#pragma once

// UiFrame: the per-block telemetry snapshot the editor reads (01 §6.2; E §7, F §7.1): 72 words, 288 bytes, published
// through a Seqlock once per process() call while the editor attach count is > 0. GR is positive dB of attenuation;
// the UI negates it for display. The editor treats the stream as stale 0.5 s after publishCount stops moving.
// Sprint-frozen; overlaySmoothed is implemented here (header-only).

#include "fcdsp/engine/IEngine.h"          // kInternals
#include "fcdsp/params/EngineParams.h"
#include <cstdint>
#include <type_traits>

namespace fcdsp {

enum UiFlag : uint32_t {
    kUiBypassed = 1u << 0, kUiDelta = 1u << 1, kUiListen = 1u << 2, kUiExtKeyActive = 1u << 3,
    kUiMidSide = 1u << 4,       // engine lanes are M/S
    kUiFading = 1u << 5, kUiLookahead = 1u << 6, kUiOutOver = 1u << 7,      // output > 0 dBFS this block
    kUiTopoFB = 1u << 8, kUiGrOff = 1u << 9, kUiAutoSlow = 1u << 10,        // DualRelease slow stage dominant
    kUiRangeLimited = 1u << 11, kUiS2Active = 1u << 12, kUiPoisonReset = 1u << 13,
    kUiLive = 1u << 14,         // input > -70 dBFS or GR > 0.01 dB this block (F: history holds when not live)
    // bits 16-17: phase lane 0, bits 18-19: phase lane 1 (0 idle, 1 attack, 2 hold, 3 release)
};

struct UiFrame {
    // identity and host ------------------------------------------------------------------------------------ 8 words
    uint32_t publishCount;
    uint16_t modeSlot;            // slot the audio runs (the incoming one once a fade starts)
    uint16_t fadeFromSlot;        // outgoing slot during a fade; == modeSlot otherwise
    uint32_t flags;               // UiFlag
    float    sampleRate;
    uint32_t latencySamples;
    float    fadeProgress;        // 0...1 kernel crossfade; 1 = none running
    float    bypassAmt;           // bypass ramp position 0 (processed) ... 1 (bypassed)
    uint32_t historyWritten;      // low 32 bits of HistoryRing::written() at publish
    // meters, per OUTPUT channel (L/R), dBFS floored at -200; instant attack, 40 ms release (HR) ------- 12 words
    float inPeakDb[2], inRmsDb[2], outPeakDb[2], outRmsDb[2], scPeakDb[2], colourInPeakDb[2];
    // control path, per ENGINE lane (L/R or M/S), end of block; GR >= 0 = attenuation ------------------ 16 words
    float curveXDb[2];            // operating-point x: plugin-input level in the Mode's detector law (01 §7)
    float targetGrDb[2];          // static-curve GR at curveXDb (FB: static FB solve)
    float appliedGrDb[2];         // after ballistics, link and stage 2: what multiplies the audio
    float blockMaxGrDb[2];        // max applied GR within the block (short spikes are not lost)
    float s2GrDb[2];              // stage-2 GR, else 0
    float attackNowMs[2];         // effective attack tau (program-dependent Modes)
    float releaseNowMs[2];        // effective release tau
    float crestDb[2];             // CrestAuto, else 0
    // parameters the audio actually used (resolved + smoothed) ---------------------------------------- 20 words
    float preGainDb, thrDb, slope, kneeDb, rangeDb, atkTauMs, relTauMs, holdMs, lookMs,
          driveDb, makeupEffDb /*incl. auto*/, mix, scHpfHz, sceDbOct, link, s2ThrDb, s2AtkTauMs, s2RelTauMs;
    uint32_t tags;
    uint32_t discrete;            // det | stmode << 8 | voice << 16 | tmode << 24
    // Mode internals; meanings in ModeDescriptor::internals (<= 8 declared; words 8-15 reserved) ------ 16 words
    float internals[kInternals];
};
static_assert(std::is_trivially_copyable_v<UiFrame> && sizeof(UiFrame) == 72 * 4);

// Live curves (K1 #7, K2 #24). UiFrame does not carry EngineParams::m[8], topo or flags, so the UI (and probe ui.truth)
// never builds an EngineParams from the frame alone. It runs resolve() on the current raw values (cached by hash), then
// copies the smoothed continuous fields (preGainDb ... s2RelTauMs; makeupEffDb -> makeupDb with kEngAutoMakeup cleared,
// because makeupEffDb already includes the auto part) onto that result. The caller skips the overlay when
// frame.modeSlot != the resolved slot or kUiFading is set.
inline void overlaySmoothed(const UiFrame& f, EngineParams& e) noexcept {
    e.preGainDb  = f.preGainDb;
    e.thrDb      = f.thrDb;
    e.slope      = f.slope;
    e.kneeDb     = f.kneeDb;
    e.rangeDb    = f.rangeDb;
    e.atkTauMs   = f.atkTauMs;
    e.relTauMs   = f.relTauMs;
    e.holdMs     = f.holdMs;
    e.lookMs     = f.lookMs;
    e.driveDb    = f.driveDb;
    e.makeupDb   = f.makeupEffDb;
    e.flags      = static_cast<uint8_t>(e.flags & ~kEngAutoMakeup);
    e.mix        = f.mix;
    e.scHpfHz    = f.scHpfHz;
    e.sceDbOct   = f.sceDbOct;
    e.link       = f.link;
    e.s2ThrDb    = f.s2ThrDb;
    e.s2AtkTauMs = f.s2AtkTauMs;
    e.s2RelTauMs = f.s2RelTauMs;
}

} // namespace fcdsp
