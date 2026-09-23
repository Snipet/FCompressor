#pragma once

// ModeEngine<Traits>: one Mode's engine, compiled from its traits' policies (01 §5.3; E §3.5-3.6). A Mode is one
// directory with a Traits header naming one policy per slot, for example (modes/fet-76/Fet76.h):
//
//     struct Fet76 {
//         static constexpr const ModeDescriptor& desc = kFet76;     // extern, defined in Fet76Desc.cpp (D24)
//         using Detector   = stage::PeakLog;
//         using Computer   = stage::QuadKnee;
//         using Link       = stage::LinkMax;
//         using Ballistics = stage::SmoothBranching;
//         using Stage2     = stage::NoStage2;
//         using Colour     = stage::ColourSelect<stage::FetColour>; // voice picks the revision's constants
//         using ScShape    = stage::Flat;                           // Mode-internal SC shaping (not the host filter)
//         static constexpr uint8_t kTopologies = 1u << kTopoFB;     // kernels compiled in (bitmask)
//         static void internals(const auto& engine, float out[kInternals]) noexcept;
//     };
//
// FCDSP_DEFINE_MODE(Traits) (DefineMode.h) instantiates the engine and its analysis entry points in the Mode's own TU
// and asserts that it fits the arena. Sprint-frozen: F0 declares the members; F3 (S2) adds the FF bodies and F9 (S3)
// the FB bodies. Per chunk, control() branches on p_.topo: the FF or FB step of Stage.h, per sample, with the
// coefficients designed on control ticks.

#include "fcdsp/core/ControlTicker.h"
#include "fcdsp/core/Simd.h"
#include "fcdsp/core/Smoother.h"
#include "fcdsp/engine/IEngine.h"
#include "fcdsp/engine/Stage.h"
#include "fcdsp/params/EngineParams.h"

namespace fcdsp {

template <class M>
class ModeEngine final : public IEngine {
    static_assert(DetectorPolicy<typename M::Detector> && GainComputerPolicy<typename M::Computer> &&
                  BallisticsPolicy<typename M::Ballistics> && Stage2Policy<typename M::Stage2> &&
                  ColourPolicy<typename M::Colour>);
    alignas(16) typename M::Detector::State   det_{};
    alignas(16) typename M::Ballistics::State bal_{};
    alignas(16) typename M::Stage2::State     s2_{};
    alignas(16) typename M::Colour::State     col_[2]{};
    typename M::Detector::Coeffs dc_{}; typename M::Computer::Coeffs gc_{};
    typename M::Ballistics::Coeffs bc_{}; typename M::Stage2::Coeffs s2c_{}; typename M::Colour::Coeffs cc_{};
    typename M::ScShape::State sh_{}; typename M::ScShape::Coeffs shc_{};
    Smoother4 lvl_{}, lvl2_{};        // {thrDb, slope, min(rangeDb,60), -}, {s2ThrDb, kneeDb, -, -}
    LinearRamp offAmt_{}, s2On_{};    // GR OFF and stage 2 on/off: 20 ms ramps, never a step (K2 #4, #20)
    EngineParams p_{}; ControlTicker tick_{}; StageCtx ctx_{};

public:
    static IEngine* construct(void* arena) noexcept;         // placement-new; RT-safe

    void  prepare(const PrepareInfo&) noexcept override;
    void  reset() noexcept override;
    void  setParams(const EngineParams&) noexcept override;
    void  snapParams() noexcept override;
    Carry carry() const noexcept override;
    void  seed(const Carry&) noexcept override;
    void  control(const ControlIo& io) noexcept FCDSP_NONBLOCKING override;   // loop: tick -> design; FF or FB step
                                                                              // (Stage.h); branch per CHUNK on p_.topo
    void  colour(const AudioIo&) noexcept FCDSP_NONBLOCKING override;
    float autoMakeupDb() const noexcept override;
    int   scDelaySamples() const noexcept override;
    void  internals(float out[kInternals]) const noexcept override;
    void  telemetry(EngineTelemetry&) const noexcept override;
    bool  finite() const noexcept override;

    // Analysis entry points, instantiated from the SAME policies the audio thread runs (01 §7; E §6.5):
    static void staticGr(const EngineParams&, const float* xDetDb, float* grDb, int n) noexcept;
    static void scShapeDb(const EngineParams&, float fs, const float* hz, float* magDb, int n) noexcept;
    static void colourCurve(const EngineParams&, float grDb, const float* x, float* y, int n) noexcept;
};
// The size/alignment asserts live in FCDSP_DEFINE_MODE, one per Mode TU (01 §8.2).

} // namespace fcdsp
