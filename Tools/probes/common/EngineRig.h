// Tools/probes/common/EngineRig.h: the Rig driver of the spec probes (03 §3.4 "Rig"; K3 #10; K2 #24).
//
// EngineRig runs ONE Mode engine the way EngineHost will, minus the host: it placement-constructs the ModeEntry's
// ModeEngine into its own 64-byte-aligned kArenaBytes arena (the same object code the plugin runs), calls prepare,
// setParams and snapParams, and drives control() and colour() at the base rate in kChunk-sample chunks, tapping
// ControlIo. Policy and Mode cards therefore pass their DoD before EngineHost is complete (F4/F7).
//
// What the Rig does in place of the host (documented choices; the host's own behaviour is 01 §5.4):
//   - the side chain is the input itself x linFromDb(preGainDb) (an internal key), lanes {L, R, L, R}: the aux lanes
//     carry a copy of the channels for the aux recurrences (crest, stage-2 detectors); no host SC filters (schpf,
//     sce), no router (stmode), no lookahead, no oversampling (ECO: osFactor 1, colour at the base rate);
//   - GR for base sample n is applied to audio sample n (ECO has no up-stage delay), as
//     wet = x * linFromDb(preGainDb - gr[n]) per channel (lanes 0-1), then colour(), then
//     y = mix * wet * linFromDb(makeupDb + autoMakeupDb()) + (1 - mix) * x (the un-pre-gained dry);
//   - preGain, makeup and mix are the block's values, unsmoothed (the host smooths them per path);
//   - every process() call opens fcdsp::ScopedFtz and does all its processing inside it (K2 #24; S2 lead revision 4:
//     the scope orders only values loaded and stored within it).
// The Rig allocates only in its constructor and in the tap (a probe tool, not the audio thread).
#pragma once

#include "fcdsp/core/Simd.h"
#include "fcdsp/engine/IEngine.h"
#include "fcdsp/modes/Registry.h"
#include "fcdsp/params/EngineParams.h"
#include "fcdsp/params/Resolve.h"
#include "fcdsp/params/Setup.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace fcmp::probe
{
    // ---- parameters -------------------------------------------------------------------------------------------------
    // The registered entry of `key`; throws std::runtime_error when the key is not registered (a harness error).
    const fcdsp::ModeEntry& modeEntry(std::string_view key);
    // The 22 Mode-filtered host defaults (kHostParams), the entry's slot and the configured budget.
    fcdsp::RawParams hostDefaults(const fcdsp::ModeEntry&, fcdsp::LookaheadBudget = fcdsp::LookaheadBudget::off);
    // hostDefaults with the Mode's own defaults applied (modeDefaults): a fresh instance of the Mode.
    fcdsp::RawParams modeRaw(const fcdsp::ModeEntry&, fcdsp::LookaheadBudget = fcdsp::LookaheadBudget::off);
    fcdsp::Resolution resolveRaw(const fcdsp::ModeEntry&, const fcdsp::RawParams&);
    // The sine-peak offset of the Mode's detector law at these parameters: +3.0103 dB for rms (a sine's RMS is its peak
    // - 3.01 dB, so the x axis level L is a sine peak of L + 3.01), 0 otherwise (C §5.2).
    double peakOffsetDb(const fcdsp::ModeEntry&, const fcdsp::EngineParams&);

    // ---- the rig ----------------------------------------------------------------------------------------------------
    struct RigTap                                          // one element per base-rate sample, while tapping
    {
        std::vector<fcdsp::simd::f32x4> grDb, detDb, tgtDb, s2GrDb;
        std::vector<std::uint8_t> bits;
        void clear() noexcept;
        std::size_t size() const noexcept { return grDb.size(); }
        std::vector<float> lane(const std::vector<fcdsp::simd::f32x4>& v, int ln) const;   // one lane as floats
    };

    class EngineRig
    {
    public:
        EngineRig(const fcdsp::ModeEntry& entry, const fcdsp::EngineParams& eng, float fs);
        ~EngineRig();
        EngineRig(const EngineRig&) = delete;
        EngineRig& operator=(const EngineRig&) = delete;

        const fcdsp::ModeEntry& entry() const noexcept { return entry_; }
        fcdsp::IEngine& engine() noexcept { return *engine_; }
        const fcdsp::EngineParams& params() const noexcept { return eng_; }
        float fs() const noexcept { return fs_; }
        std::uint64_t sampleIndex() const noexcept { return index_; }

        void setParams(const fcdsp::EngineParams& eng) noexcept;    // per block (the smoothers move), as the host
        void snapParams() noexcept;
        void reset() noexcept;                                     // engine state only; the sample index runs on

        void setTapping(bool on) noexcept { tapping_ = on; }
        RigTap& tap() noexcept { return tap_; }

        // The makeup the Rig applies: makeupDb + the engine's autoMakeupDb().
        float makeupTotalDb() const noexcept;

        // Stereo in, stereo out, any n (chunked internally); out may alias in.
        void process(const float* inL, const float* inR, float* outL, float* outR, std::size_t n);

    private:
        void chunk(const float* inL, const float* inR, float* outL, float* outR, int n);

        const fcdsp::ModeEntry& entry_;
        fcdsp::EngineParams eng_;
        float fs_;
        std::uint64_t index_ = 0;
        alignas(64) std::array<std::byte, fcdsp::kArenaBytes> arena_{};
        fcdsp::IEngine* engine_ = nullptr;
        std::vector<float> scratch_;

        alignas(16) std::array<fcdsp::simd::f32x4, fcdsp::kChunk> sc_{}, gr_{}, det_{}, tgt_{}, s2_{};
        std::array<std::uint8_t, fcdsp::kChunk> bits_{};
        std::array<float, fcdsp::kChunk> dryL_{}, dryR_{}, wetL_{}, wetR_{}, grL_{}, grR_{};

        bool tapping_ = false;
        RigTap tap_;
    };

    // ---- drivers ----------------------------------------------------------------------------------------------------
    // D1 (C §5.2): a staircase of sine levels (the x-axis level in dBFS; the sine peak is level + peakOffsetDb), L = R.
    // Each step plays holdS, then measureS, of the sine at `hz`, continuing from the rig's current sample index; the
    // measure window must hold whole cycles. Per step: the single-bin gain of the left channel and the tap's lane-0 GR
    // over the window (mean, min, max).
    struct CurvePoint
    {
        double levelDb = 0, gainDb = 0, tapGrDb = 0, tapGrMinDb = 0, tapGrMaxDb = 0;
    };
    struct CurveRun
    {
        std::vector<CurvePoint> points;
        std::int64_t nonfinite = 0;                        // non-finite output samples, whole run
        double minGrDb = 0;                                // smallest tapped GR (lanes 0-1) over the windows
    };
    CurveRun runSineStaircase(EngineRig&, std::span<const double> levelsDb, double peakOffsetDb, double hz = 1000.0,
                              double holdS = 0.3, double measureS = 0.1);

    // D2 (C §5.3, 01 §7 StepStimulus): level segments of a square wave at `hz` (|x| = A at every sample, so peak ==
    // RMS and every detector reads the level exactly), L = R. Returns per sample: the tap's lane-0 GR and the GR
    // derived from the audio, preGainDb + makeupTotalDb - 20 log10 |y / x| (x != 0 at every sample).
    struct Segment
    {
        double levelDb = 0, seconds = 0;
    };
    struct StepRun
    {
        std::vector<float> tapGrDb, audioGrDb;
        std::vector<float> out;                            // left output, for click metrics
        std::vector<std::size_t> edges;                    // first sample of each segment
        std::int64_t nonfinite = 0;
    };
    StepRun runSquareSteps(EngineRig&, std::span<const Segment> segments, double peakOffsetDb, double hz = 1000.0);
} // namespace fcmp::probe
