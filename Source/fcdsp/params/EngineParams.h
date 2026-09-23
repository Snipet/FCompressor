#pragma once

// What the engine, the analysis functions and UiFrame consume (01 §4.2): POD, natural units, produced by
// resolve() = kit::physicalDefault + the Mode's physical(). Sprint-frozen; its size is asserted.

#include <cstdint>
#include <type_traits>

namespace fcdsp {

// Sentinels are the host range ends, so they smooth as finite values (K2 #20): never 1000.
inline constexpr float kRangeOff = 60.f;     // dB: rangeDb >= 60 = "no clamp" (the engine smooths min(rangeDb, 60))
inline constexpr float kS2Off    = 24.f;     // dB: s2ThrDb >= 24 = stage 2 off (the engine ramps s2On over 20 ms)
enum Topo : uint8_t { kTopoFF = 0, kTopoFB = 1 };
enum EngFlag : uint8_t { kEngAutoMakeup = 1, kEngGrOff = 2, kEngAutoRelease = 4, kEngTruePeak = 8 };

struct EngineParams {
    // level (dB). thrDb is the detector-domain threshold AFTER preGain; effective input threshold = thrDb - preGainDb
    float preGainDb = 0, thrDb = -18, slope = 0.75f, kneeDb = 6, rangeDb = kRangeOff;
    // time: tau (63 %) in ms, already converted from the published value through ParamSpec::law
    float atkTauMs = 10, relTauMs = 200, holdMs = 0, lookMs = 0;
    // gain staging
    float driveDb = 0, makeupDb = 0, mix = 1;
    // side chain and stereo
    float scHpfHz = 0 /*0 = off*/, sceDbOct = 0, link = 1;
    // stage 2
    float s2ThrDb = kS2Off, s2AtkTauMs = 1, s2RelTauMs = 100;
    // Mode-private constants, meanings documented in the Mode's header (e.g. opto emphasis, FET law)
    float m[8] {};
    // discrete (kernel key = slot, topo, det, stmode, voice, effective external key: 01 §5.5)
    uint8_t det = 0, stmode = 0, voice = 0, tmode = 0, topo = kTopoFF, flags = 0;
    uint16_t reserved = 0;
    uint32_t tags = 0;              // OR of the active step tags
};
static_assert(std::is_trivially_copyable_v<EngineParams> && sizeof(EngineParams) == 116);

} // namespace fcdsp
