#pragma once

// Telemetry accumulation (01 §5.4 step 2j and 3, §6.2-6.3; E §7): the meters and control-path values of UiFrame and
// the 1 ms HistoryColumns, accumulated per sample from the buffers the host already has (the engine's ControlIo
// outputs and the audio pass), so the audio code gains no branch of its own. A pure component with one owner (K3 #12:
// F4); EngineHost.cpp orchestrates it and runs it only while the editor attach count is > 0.
//
// Columns (01 §6.3). A column is 1 ms of audio time, cut at ABSOLUTE sample boundaries: column j covers samples
// [floor(j fs / 1000), floor((j + 1) fs / 1000)) of the host's sample index (0 at configure), so at 44.1 kHz columns
// are 44 or 45 samples and the strip does not depend on the host block size. The grid advances while detached too
// (advance()), so a column boundary never moves; only complete columns are pushed. Per column:
//   inPeakDb, outPeakDb   max |x| over both channels, dBFS
//   detMaxDb              max curve-axis level over lanes 0-1: the engine's detDb (detector level of the pre-gained
//                         side chain) minus that sample's preGainDb (01 §7 axis contract)
//   grMaxDb, grMinDb      max and min over the column of the applied GR of the louder lane, max(lane0, lane1): spikes
//                         both ways survive (E §7)
//   tgtMaxDb              max static target GR over lanes 0-1
//   internal0             the Mode's history-flagged internal, as latched by the host (EngineHost latches it at every
//                         absolute multiple of kChunk, so it is block-size invariant too); 0 when the Mode has none
//   bits                  b0-1 the engine phase at the column's (first) max-GR sample; b2 auto-slow, b3 range-limited,
//                         b4 stage-2 active: OR over the column; b5 the first column after an attach (a gap); b6 a
//                         kernel crossfade ran (the host's `extra` bits); b8-15 the Mode slot
// Every dB value is floored at -200 and clamped at +200 (01 §5.4 step 3).
//
// Meters (01 §6.2, HR publishUiFrame): per output channel, a block value (peak |x|, or the RMS over the block) through
// an envelope with instant attack and a 40 ms release whose per-block coefficient is compensated for the block length
// (allowed for telemetry only, E §1): env = v > env ? v : env + (1 - e^(-n / (0.04 fs))) (v - env). Published in dBFS,
// floored at -200 and clamped at +200.
//
// Control path (UiFrame words 20-35): the end-of-block values of lanes 0-1 (curve x = detDb - preGainDb, target GR,
// applied GR, stage-2 GR), the max applied GR per lane over the block, and the status bits of the block: OR of b2-b4
// and the phase (b0-1) of the last sample. ControlIo carries only the phase of the louder lane (its bits layout), so
// both of UiFrame's per-lane phase fields carry it.
//
// Real time: every member is inline and FCDSP_NONBLOCKING; nothing allocates. prepare() runs in configure.

#include "fcdsp/core/FastMath.h"
#include "fcdsp/core/Rt.h"
#include "fcdsp/core/Simd.h"
#include "fcdsp/core/Units.h"
#include "fcdsp/telemetry/HistoryRing.h"
#include "fcdsp/telemetry/UiFrame.h"
#include <cstdint>

namespace fcdsp::host {

inline constexpr float kMeterReleaseMs = 40.0f;              // HR's meter release
inline constexpr float kTelemetryFloorDb = -200.0f;          // 01 §5.4 step 3
inline constexpr float kTelemetryCeilDb = 200.0f;
inline constexpr float kLiveInputLin = 3.16227766e-4f;       // -70 dBFS: kUiLive (01 §6.2)
inline constexpr float kLiveGrDb = 0.01f;

// HistoryColumn::bits (01 §6.3) and ControlIo::bits (01 §5.3) share b0-4.
inline constexpr uint32_t kBitsPhase = 3u, kBitsAutoSlow = 1u << 2, kBitsRangeLimited = 1u << 3,
                          kBitsS2Active = 1u << 4, kColGap = 1u << 5, kColFading = 1u << 6;
inline constexpr int kColSlotShift = 8;

// dB of a linear magnitude, floored and clamped for publishing.
inline float publishDb(float lin) noexcept FCDSP_NONBLOCKING
{
    const float db = simd::lane<0>(dbFromLin(simd::set1(lin)));
    return db < kTelemetryFloorDb ? kTelemetryFloorDb : (db > kTelemetryCeilDb ? kTelemetryCeilDb : db);
}
inline float clampDb(float db) noexcept FCDSP_NONBLOCKING
{
    return db < kTelemetryFloorDb ? kTelemetryFloorDb : (db > kTelemetryCeilDb ? kTelemetryCeilDb : db);
}

// One chunk of per-sample telemetry sources, all [n], channel pointers L/R.
struct TelemetryChunk {
    int n = 0;
    const float* in[2]{};            // sanitised main input (mono duplicated)
    const float* out[2]{};           // final output
    const float* colourIn[2]{};      // what entered colour(): dry x g
    const float* preGainDb = nullptr;
    const simd::f32x4* sc = nullptr; // the side chain as fed to control() (lanes 0-1 metered)
    const simd::f32x4* grDb = nullptr;
    const simd::f32x4* detDb = nullptr;
    const simd::f32x4* tgtDb = nullptr;
    const simd::f32x4* s2GrDb = nullptr;
    const uint8_t* bits = nullptr;
};

class TelemetryAccum {
public:
    // configure(): the column grid starts at sample 0; meters and accumulators are zeroed.
    void prepare(double fs) noexcept FCDSP_NONBLOCKING
    {
        fs_ = fs > 0.0 ? fs : 48000.0;
        const double whole = static_cast<double>(static_cast<uint64_t>(fs_));
        fsInt_ = whole == fs_ ? static_cast<uint64_t>(fs_) : 0;
        col_ = 0;
        nextCut_ = columnStart(1);
        if (nextCut_ == 0)
            nextCut_ = 1;                                       // below 1 kHz a column still holds >= 1 sample
        resetColumn();
        resetMeters();
        gap_ = true;
        beginBlock();
    }

    // Detached: move the grid over [first, first + n) without accumulating (completed columns are dropped).
    void advance(uint64_t first, int n) noexcept FCDSP_NONBLOCKING
    {
        const uint64_t end = first + static_cast<uint64_t>(n);
        while (end >= nextCut_)
            nextColumn();
    }

    // A 0 -> 1 attach transition (01 §6.3 writer rules): the column accumulator restarts here, the meter envelopes
    // restart, and the next pushed column carries b5.
    void attach() noexcept FCDSP_NONBLOCKING
    {
        resetColumn();
        resetMeters();
        gap_ = true;
    }

    void beginBlock() noexcept FCDSP_NONBLOCKING
    {
        for (int c = 0; c < 2; ++c)
        {
            inPeak_[c] = inSq_[c] = outPeak_[c] = outSq_[c] = scPeak_[c] = colPeak_[c] = 0.0f;
            blockMaxGr_[c] = 0.0f;
            lastX_[c] = lastTgt_[c] = lastGr_[c] = lastS2_[c] = 0.0f;
        }
        blockBits_ = 0;
        lastPhase_ = 0;
        outOver_ = false;
        haveControl_ = false;
    }

    // Per sample of one chunk starting at absolute index `first`: block meters, control-path values and columns;
    // completed columns are pushed to `ring`. `extraBits` = the host's column bits (b6 fading, slot << 8); internal0 =
    // the latched history internal.
    void accumulate(const TelemetryChunk& t, uint64_t first, uint32_t extraBits, float internal0,
                    HistoryRing& ring) noexcept FCDSP_NONBLOCKING
    {
        for (int i = 0; i < t.n; ++i)
        {
            const float aIn0 = absf(t.in[0][i]), aIn1 = absf(t.in[1][i]);
            const float aOut0 = absf(t.out[0][i]), aOut1 = absf(t.out[1][i]);
            inPeak_[0] = maxf(inPeak_[0], aIn0);
            inPeak_[1] = maxf(inPeak_[1], aIn1);
            outPeak_[0] = maxf(outPeak_[0], aOut0);
            outPeak_[1] = maxf(outPeak_[1], aOut1);
            inSq_[0] += t.in[0][i] * t.in[0][i];
            inSq_[1] += t.in[1][i] * t.in[1][i];
            outSq_[0] += t.out[0][i] * t.out[0][i];
            outSq_[1] += t.out[1][i] * t.out[1][i];
            colPeak_[0] = maxf(colPeak_[0], absf(t.colourIn[0][i]));
            colPeak_[1] = maxf(colPeak_[1], absf(t.colourIn[1][i]));
            outOver_ = outOver_ || aOut0 > 1.0f || aOut1 > 1.0f;

            const simd::f32x4 sc = t.sc[i], gr = t.grDb[i], det = t.detDb[i], tgt = t.tgtDb[i], s2 = t.s2GrDb[i];
            scPeak_[0] = maxf(scPeak_[0], absf(simd::lane<0>(sc)));
            scPeak_[1] = maxf(scPeak_[1], absf(simd::lane<1>(sc)));
            const float pre = t.preGainDb[i];
            lastGr_[0] = simd::lane<0>(gr);
            lastGr_[1] = simd::lane<1>(gr);
            lastX_[0] = simd::lane<0>(det) - pre;
            lastX_[1] = simd::lane<1>(det) - pre;
            lastTgt_[0] = simd::lane<0>(tgt);
            lastTgt_[1] = simd::lane<1>(tgt);
            lastS2_[0] = simd::lane<0>(s2);
            lastS2_[1] = simd::lane<1>(s2);
            blockMaxGr_[0] = maxf(blockMaxGr_[0], lastGr_[0]);
            blockMaxGr_[1] = maxf(blockMaxGr_[1], lastGr_[1]);
            const uint32_t b = t.bits[i];
            blockBits_ |= b;
            lastPhase_ = b & kBitsPhase;
            haveControl_ = true;

            // column
            const float g = maxf(lastGr_[0], lastGr_[1]);
            if (!colAny_ || g > colGrMax_)
            {
                colGrMax_ = g;
                colPhase_ = b & kBitsPhase;
            }
            colGrMin_ = colAny_ ? minf(colGrMin_, g) : g;
            colIn_ = maxf(colIn_, maxf(aIn0, aIn1));
            colOut_ = maxf(colOut_, maxf(aOut0, aOut1));
            const float x = maxf(lastX_[0], lastX_[1]);
            colDet_ = colAny_ ? maxf(colDet_, x) : x;
            const float tg = maxf(lastTgt_[0], lastTgt_[1]);
            colTgt_ = colAny_ ? maxf(colTgt_, tg) : tg;
            colBits_ |= b & (kBitsAutoSlow | kBitsRangeLimited | kBitsS2Active);
            colAny_ = true;

            if (first + static_cast<uint64_t>(i) + 1u == nextCut_)
            {
                ring.push(column(extraBits, internal0));
                nextColumn();
            }
        }
    }

    // Block end: meters (envelopes over this block of n samples) and the control-path words into f; returns the
    // UiFlag bits the block's samples decide (kUiOutOver, kUiLive, kUiAutoSlow, kUiRangeLimited, kUiS2Active and the
    // two per-lane phase fields).
    uint32_t publish(int n, UiFrame& f) noexcept FCDSP_NONBLOCKING
    {
        const float invN = n > 0 ? 1.0f / static_cast<float>(n) : 0.0f;
        const float rel = oneMinusAlpha(kMeterReleaseMs, static_cast<float>(fs_) * invN);
        for (int c = 0; c < 2; ++c)
        {
            envelope(envInPeak_[c], inPeak_[c], rel);
            envelope(envInRms_[c], rootf(inSq_[c] * invN), rel);
            envelope(envOutPeak_[c], outPeak_[c], rel);
            envelope(envOutRms_[c], rootf(outSq_[c] * invN), rel);
            envelope(envScPeak_[c], scPeak_[c], rel);
            envelope(envColPeak_[c], colPeak_[c], rel);
            f.inPeakDb[c] = publishDb(envInPeak_[c]);
            f.inRmsDb[c] = publishDb(envInRms_[c]);
            f.outPeakDb[c] = publishDb(envOutPeak_[c]);
            f.outRmsDb[c] = publishDb(envOutRms_[c]);
            f.scPeakDb[c] = publishDb(envScPeak_[c]);
            f.colourInPeakDb[c] = publishDb(envColPeak_[c]);
            f.curveXDb[c] = clampDb(lastX_[c]);
            f.targetGrDb[c] = clampDb(lastTgt_[c]);
            f.appliedGrDb[c] = clampDb(lastGr_[c]);
            f.blockMaxGrDb[c] = clampDb(blockMaxGr_[c]);
            f.s2GrDb[c] = clampDb(lastS2_[c]);
        }
        uint32_t flags = 0;
        if (outOver_)
            flags |= kUiOutOver;
        if (maxf(inPeak_[0], inPeak_[1]) > kLiveInputLin || maxf(blockMaxGr_[0], blockMaxGr_[1]) > kLiveGrDb)
            flags |= kUiLive;
        if ((blockBits_ & kBitsAutoSlow) != 0)
            flags |= kUiAutoSlow;
        if ((blockBits_ & kBitsRangeLimited) != 0)
            flags |= kUiRangeLimited;
        if ((blockBits_ & kBitsS2Active) != 0)
            flags |= kUiS2Active;
        if (haveControl_)
            flags |= (lastPhase_ << 16) | (lastPhase_ << 18);
        return flags;
    }

private:
    static float absf(float v) noexcept FCDSP_NONBLOCKING { return v < 0.0f ? -v : v; }
    static float maxf(float a, float b) noexcept FCDSP_NONBLOCKING { return a > b ? a : b; }
    static float minf(float a, float b) noexcept FCDSP_NONBLOCKING { return a < b ? a : b; }
    static float rootf(float v) noexcept FCDSP_NONBLOCKING { return simd::lane<0>(simd::sqrt(simd::set1(v))); }
    static void envelope(float& env, float v, float rel) noexcept FCDSP_NONBLOCKING
    {
        env = v > env ? v : env + rel * (v - env);
    }

    // First sample of column j: floor(j fs / 1000), exact in integers for an integral rate.
    uint64_t columnStart(uint64_t j) const noexcept FCDSP_NONBLOCKING
    {
        if (fsInt_ != 0)
            return j * fsInt_ / 1000u;
        return static_cast<uint64_t>(static_cast<double>(j) * fs_ / 1000.0);
    }

    void nextColumn() noexcept FCDSP_NONBLOCKING
    {
        uint64_t next = nextCut_;
        while (next <= nextCut_)                                // below 1 kHz a column still holds >= 1 sample
        {
            ++col_;
            next = columnStart(col_ + 1);
        }
        nextCut_ = next;
        resetColumn();
    }

    void resetColumn() noexcept FCDSP_NONBLOCKING
    {
        colIn_ = colOut_ = 0.0f;
        colDet_ = colGrMax_ = colGrMin_ = colTgt_ = 0.0f;
        colBits_ = 0;
        colPhase_ = 0;
        colAny_ = false;
    }

    void resetMeters() noexcept FCDSP_NONBLOCKING
    {
        for (int c = 0; c < 2; ++c)
            envInPeak_[c] = envInRms_[c] = envOutPeak_[c] = envOutRms_[c] = envScPeak_[c] = envColPeak_[c] = 0.0f;
    }

    HistoryColumn column(uint32_t extraBits, float internal0) noexcept FCDSP_NONBLOCKING
    {
        HistoryColumn c{};
        c.inPeakDb = publishDb(colIn_);
        c.outPeakDb = publishDb(colOut_);
        c.detMaxDb = clampDb(colDet_);
        c.grMaxDb = clampDb(colGrMax_);
        c.grMinDb = clampDb(colGrMin_);
        c.tgtMaxDb = clampDb(colTgt_);
        c.internal0 = internal0;
        c.bits = colPhase_ | colBits_ | (gap_ ? kColGap : 0u) | extraBits;
        gap_ = false;
        return c;
    }

    double fs_ = 48000.0;
    uint64_t fsInt_ = 48000;
    uint64_t col_ = 0, nextCut_ = 48;
    bool gap_ = true;

    // the open column
    float colIn_ = 0, colOut_ = 0, colDet_ = 0, colGrMax_ = 0, colGrMin_ = 0, colTgt_ = 0;
    uint32_t colBits_ = 0, colPhase_ = 0;
    bool colAny_ = false;

    // this block
    float inPeak_[2]{}, inSq_[2]{}, outPeak_[2]{}, outSq_[2]{}, scPeak_[2]{}, colPeak_[2]{};
    float blockMaxGr_[2]{}, lastX_[2]{}, lastTgt_[2]{}, lastGr_[2]{}, lastS2_[2]{};
    uint32_t blockBits_ = 0, lastPhase_ = 0;
    bool outOver_ = false, haveControl_ = false;

    // meter envelopes (linear)
    float envInPeak_[2]{}, envInRms_[2]{}, envOutPeak_[2]{}, envOutRms_[2]{}, envScPeak_[2]{}, envColPeak_[2]{};
};

} // namespace fcdsp::host
