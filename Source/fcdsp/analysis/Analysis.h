#pragma once

// The analysis API (01 §7; E §6; K2 #24): pure functions that use the SAME policy code the audio thread runs, for the
// always-visible band, the full-panel Characteristics screen and the spec probes. They run on the message thread or
// the PreviewWorker, may allocate, are never called from the audio thread, and every entry point opens
// fcdsp::ScopedFtz, so the UI computes what the probes (also under FTZ) check. Every function takes an EngineParams:
// callers resolve first, and overlay the live smoothed fields with overlaySmoothed (UiFrame.h). Sprint-frozen;
// F8 (S5) implements Analysis.cpp. analysis/ is exempt from the audio-path libm rule for plot-only extras.

#include "fcdsp/core/Units.h"          // TimeLaw
#include "fcdsp/params/EngineParams.h"
#include <span>

namespace fcdsp {
struct ModeEntry;                      // Registry.h
}

namespace fcdsp::analysis {

struct CurveOpts { bool colour = false; bool stage2 = true; };   // colour: add the describing-function gain (E §6.3)

// Axis contract: x = PLUGIN-INPUT level (dBFS) in the Mode's DetectorLaw (peak: sine peak; rms: peak - 3.01 dB).
// gainDb = preGainDb - GR(x + preGainDb) [+ colour DF]. Excludes makeup, mix and output.
// TRANSFER draws y = x + gainDb - preGainDb (GR only); the net curve draws y = x + netGainDb(gainDb, makeupEffDb, mix)
// (K1 #22; probe ui.curve uses the same formulas).
void staticGain(const ModeEntry&, const EngineParams&, std::span<const float> xDb, std::span<float> gainDb,
                CurveOpts = {}) noexcept;
// The spec probes' function: GR at the gain computer for DETECTOR-domain x (after preGain). FF: Computer::target over
// four abscissae per call, bit-identical to the DSP symbol (E §6.5). FB: Computer::solveFb with FbAffine{0, 1};
// non-closed-form laws iterate to convergence (<= 6 Newton steps, bisection bracket [0, max(0, x - T + W)]).
void staticGr(const ModeEntry&, const EngineParams&, std::span<const float> xDetDb, std::span<float> grDb) noexcept;
float localRatio(const ModeEntry&, const EngineParams&, float xDb) noexcept;   // 1/(1 - d(GR)/dx), central diff +-0.05 dB
float netGainDb(float gainDb, float makeupEffDb, float mix) noexcept;          // 20*log10(mix*10^((g+mk)/20) + 1 - mix)
// Input-referred threshold T_in, where the TRANSFER threshold handle and the HISTORY threshold line sit (K1 #9).
// T_in is affine in thr with slope 1 in every Mode (registry lint: |dT_in/dthr - 1| <= 1e-4 at 5 points per ratio step),
// so a threshold handle drag writes thr_new = thr_cur + (T_target - T_cur).
inline float inputThresholdDb(const EngineParams& e) noexcept { return e.thrDb - e.preGainDb; }

struct StepStimulus {                  // level-domain stimulus injected AFTER the host SC filters (peak == RMS)
    float fs = 48000;
    float preSec = 0.05f;              // silence
    // Levels are relative to eng.thrDb in the DETECTOR domain (after preGain), because the stimulus is injected after
    // the SC filters (K1 #35).
    float hiDbOverThr = 12, hiSec = 0.5f;
    float loDbUnderThr = 12, loSec = 2.0f;
    int   decimate = 1;                // write every n-th sample (min/max decimation is the caller's)
};
// Runs a PRIVATE ModeEngine (constructed into a local 64-aligned kArenaBytes buffer, with a heap scratch sized as the
// host's) on the stimulus and writes the applied GR (>= 0 dB). Bit-identical to the plugin rendering the same burst
// (E §6.5). Returns samples written.
int stepResponse(const ModeEntry&, const EngineParams&, const StepStimulus&, std::span<float> grDbOut);
// Time readouts from a response: t63 / t10-90 / t50 per law, for the TIME view and D2 cross-checks.
struct TimeReadout { float attackS, releaseS; TimeLaw law; };
TimeReadout measure(std::span<const float> grDb, const StepStimulus&, TimeLaw) noexcept;

// Detector-path magnitude: host SC HPF + sce tilt + the Mode's internal SC shaping (R37, 33609 SLOW HP, Thrust).
void scResponse(const ModeEntry&, const EngineParams&, float fs, std::span<const float> hz, std::span<float> magDb) noexcept;
// Colour-stage static transfer at a given GR (the COLOUR view); harmonics of shape(A*sin), 64-point DFT.
void colourCurve(const ModeEntry&, const EngineParams&, float grDb, std::span<const float> x, std::span<float> y) noexcept;
// h[k] = harmonic k+1 in dB relative to the fundamental (h[0] = 0): H2...H5 = h[1..4], THD from h[1..7] (02 §9.7 #8).
void harmonicsDb(const ModeEntry&, const EngineParams&, float grDb, float amp, std::span<float, 8> h) noexcept;

} // namespace fcdsp::analysis
