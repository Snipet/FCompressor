// Tools/probes/common/EngineRig.cpp: see EngineRig.h.
#include "EngineRig.h"

#include "Measure.h"
#include "Signals.h"

#include "fcdsp/core/FastMath.h"
#include "fcdsp/core/ScopedFtz.h"
#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/params/HostParams.h"
#include "fcdsp/params/Pid.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace fcmp::probe
{
    namespace simd = fcdsp::simd;

    namespace
    {
        constexpr double kRmsPeakOffsetDb = 3.0102999566398120;    // a sine's peak over its RMS, dB

        bool isFinite(float v) noexcept { return std::isfinite(v); }

        std::size_t samples(double seconds, float fs) { return static_cast<std::size_t>(seconds * fs + 0.5); }
    } // namespace

    // ---- parameters -------------------------------------------------------------------------------------------------

    const fcdsp::ModeEntry& modeEntry(std::string_view key)
    {
        const fcdsp::ModeEntry* e = fcdsp::byKey(key);
        if (e == nullptr || e->desc == nullptr)
            throw std::runtime_error("Mode '" + std::string(key) + "' is not registered (Modes.def)");
        return *e;
    }

    fcdsp::RawParams hostDefaults(const fcdsp::ModeEntry& entry, fcdsp::LookaheadBudget budget)
    {
        fcdsp::RawParams raw;
        for (std::size_t i = 0; i < fcdsp::kNumModeParams; ++i)
            raw.v[i] = fcdsp::kHostParams[i].def;
        const int slot = fcdsp::slotOf(entry);
        raw.modeSlot = static_cast<std::uint8_t>(slot < 0 ? 0 : slot);
        raw.budget = budget;
        return raw;
    }

    fcdsp::RawParams modeRaw(const fcdsp::ModeEntry& entry, fcdsp::LookaheadBudget budget)
    {
        fcdsp::RawParams raw = hostDefaults(entry, budget);
        fcdsp::modeDefaults(*entry.desc, raw);
        return raw;
    }

    fcdsp::Resolution resolveRaw(const fcdsp::ModeEntry& entry, const fcdsp::RawParams& raw)
    {
        fcdsp::Resolution res;
        fcdsp::resolve(entry, raw, res);
        return res;
    }

    double peakOffsetDb(const fcdsp::ModeEntry& entry, const fcdsp::EngineParams& eng)
    {
        return entry.desc->detectorLaw(eng) == fcdsp::DetectorLaw::rms ? kRmsPeakOffsetDb : 0.0;
    }

    // ---- the rig ----------------------------------------------------------------------------------------------------

    void RigTap::clear() noexcept
    {
        grDb.clear();
        detDb.clear();
        tgtDb.clear();
        s2GrDb.clear();
        bits.clear();
    }

    std::vector<float> RigTap::lane(const std::vector<simd::f32x4>& v, int ln) const
    {
        std::vector<float> out(v.size());
        alignas(16) float t[4];
        for (std::size_t i = 0; i < v.size(); ++i)
        {
            simd::store(t, v[i]);
            out[i] = t[ln & 3];
        }
        return out;
    }

    EngineRig::EngineRig(const fcdsp::ModeEntry& entry, const fcdsp::EngineParams& eng, float fs)
        : entry_(entry), eng_(eng), fs_(fs)
    {
        if (entry.construct == nullptr)
            throw std::runtime_error("EngineRig: the ModeEntry has no construct()");
        if (entry.engineBytes > fcdsp::kArenaBytes || entry.engineAlign > 64)
            throw std::runtime_error("EngineRig: the engine does not fit the arena");
        if (!(fs > 0.0f))
            throw std::runtime_error("EngineRig: fs must be > 0");
        // The host's scratch per slot: 4 x nextPow2(L_la,max + kChunk) floats, L_la,max = 20 ms (01 §5.3).
        const std::size_t la = static_cast<std::size_t>(std::ceil(0.02 * static_cast<double>(fs)));
        std::size_t pow2 = 1;
        while (pow2 < la + static_cast<std::size_t>(fcdsp::kChunk))
            pow2 <<= 1;
        scratch_.assign(4 * pow2, 0.0f);

        engine_ = entry.construct(arena_.data());
        engine_->prepare(fcdsp::PrepareInfo{ fs, 1, std::span<float>(scratch_) });
        engine_->setParams(eng_);
        engine_->snapParams();
    }

    EngineRig::~EngineRig()
    {
        if (engine_ != nullptr)
            engine_->~IEngine();
    }

    void EngineRig::setParams(const fcdsp::EngineParams& eng) noexcept
    {
        eng_ = eng;
        engine_->setParams(eng_);
    }

    void EngineRig::snapParams() noexcept { engine_->snapParams(); }

    void EngineRig::reset() noexcept { engine_->reset(); }

    float EngineRig::makeupTotalDb() const noexcept { return eng_.makeupDb + engine_->autoMakeupDb(); }

    void EngineRig::process(const float* inL, const float* inR, float* outL, float* outR, std::size_t n)
    {
        const fcdsp::ScopedFtz ftz;                     // K2 #24: everything below runs inside the scope
        for (std::size_t off = 0; off < n;)
        {
            const int m = static_cast<int>(std::min<std::size_t>(static_cast<std::size_t>(fcdsp::kChunk), n - off));
            chunk(inL + off, inR + off, outL + off, outR + off, m);
            off += static_cast<std::size_t>(m);
        }
    }

    void EngineRig::chunk(const float* inL, const float* inR, float* outL, float* outR, int n)
    {
        const std::size_t un = static_cast<std::size_t>(n);
        const simd::f32x4 pre = simd::set1(eng_.preGainDb);
        const float pg = simd::lane<0>(fcdsp::linFromDb(pre));
        for (std::size_t i = 0; i < un; ++i)
        {
            dryL_[i] = inL[i];
            dryR_[i] = inR[i];
            alignas(16) const float v[4] = { dryL_[i] * pg, dryR_[i] * pg, dryL_[i] * pg, dryR_[i] * pg };
            sc_[i] = simd::load(v);
        }

        fcdsp::ControlIo io;
        io.n = n;
        io.sampleIndex = index_;
        io.sc = sc_.data();
        io.grDb = gr_.data();
        io.detDb = tapping_ ? det_.data() : nullptr;
        io.tgtDb = tapping_ ? tgt_.data() : nullptr;
        io.s2GrDb = tapping_ ? s2_.data() : nullptr;
        io.bits = tapping_ ? bits_.data() : nullptr;
        engine_->control(io);

        for (std::size_t i = 0; i < un; ++i)
        {
            const simd::f32x4 g = fcdsp::linFromDb(simd::sub(pre, gr_[i]));
            wetL_[i] = dryL_[i] * simd::lane<0>(g);
            wetR_[i] = dryR_[i] * simd::lane<1>(g);
            grL_[i] = simd::lane<0>(gr_[i]);
            grR_[i] = simd::lane<1>(gr_[i]);
        }
        float* const wet[2] = { wetL_.data(), wetR_.data() };
        const float* const grOs[2] = { grL_.data(), grR_.data() };
        fcdsp::AudioIo audio;
        audio.nOs = n;
        audio.wet = wet;
        audio.grDbOs = grOs;
        engine_->colour(audio);

        const float mk = simd::lane<0>(fcdsp::linFromDb(simd::set1(makeupTotalDb())));
        const float mix = eng_.mix, dryAmt = 1.0f - eng_.mix;
        for (std::size_t i = 0; i < un; ++i)
        {
            outL[i] = mix * (wetL_[i] * mk) + dryAmt * dryL_[i];
            outR[i] = mix * (wetR_[i] * mk) + dryAmt * dryR_[i];
        }

        if (tapping_)
            for (std::size_t i = 0; i < un; ++i)
            {
                tap_.grDb.push_back(gr_[i]);
                tap_.detDb.push_back(det_[i]);
                tap_.tgtDb.push_back(tgt_[i]);
                tap_.s2GrDb.push_back(s2_[i]);
                tap_.bits.push_back(bits_[i]);
            }
        index_ += static_cast<std::uint64_t>(n);
    }

    // ---- drivers ----------------------------------------------------------------------------------------------------

    CurveRun runSineStaircase(EngineRig& rig, std::span<const double> levelsDb, double peakOffsetDb, double hz,
                              double holdS, double measureS)
    {
        const float fs = rig.fs();
        const std::size_t hold = samples(holdS, fs), win = samples(measureS, fs), len = hold + win;
        const measure::SingleBin bin(hz, fs, win);      // throws unless the window holds whole cycles

        // The sine is periodic in `win` (whole cycles), and sig::sineAt reduces its phase exactly, so a one-window
        // table indexed by (absolute index mod win) IS the closed-form sine.
        std::vector<float> table(win);
        for (std::size_t i = 0; i < win; ++i)
            table[i] = sig::sineAt(static_cast<std::int64_t>(i), hz, fs);

        std::vector<float> in(len), outL(len), outR(len);
        CurveRun run;
        run.points.reserve(levelsDb.size());
        bool firstGr = true;
        for (const double level : levelsDb)
        {
            const std::uint64_t n0 = rig.sampleIndex();
            const auto amp = static_cast<float>(measure::amplitudeFromDb(level + peakOffsetDb));
            for (std::size_t k = 0; k < len; ++k)
                in[k] = amp * table[static_cast<std::size_t>((n0 + k) % win)];

            rig.setTapping(false);
            rig.process(in.data(), in.data(), outL.data(), outR.data(), hold);
            rig.tap().clear();
            rig.setTapping(true);
            rig.process(in.data() + hold, in.data() + hold, outL.data() + hold, outR.data() + hold, win);
            rig.setTapping(false);

            for (std::size_t k = 0; k < len; ++k)
                run.nonfinite += (isFinite(outL[k]) ? 0 : 1) + (isFinite(outR[k]) ? 0 : 1);

            CurvePoint pt;
            pt.levelDb = level;
            pt.gainDb = bin.gainDb(std::span<const float>(in).subspan(hold), std::span<const float>(outL).subspan(hold),
                                   static_cast<std::int64_t>(n0 + hold));
            const std::vector<float> gr0 = rig.tap().lane(rig.tap().grDb, 0);
            const std::vector<float> gr1 = rig.tap().lane(rig.tap().grDb, 1);
            double sum = 0.0;
            pt.tapGrMinDb = gr0.empty() ? 0.0 : gr0[0];
            pt.tapGrMaxDb = pt.tapGrMinDb;
            for (std::size_t k = 0; k < gr0.size(); ++k)
            {
                sum += gr0[k];
                pt.tapGrMinDb = std::min(pt.tapGrMinDb, static_cast<double>(gr0[k]));
                pt.tapGrMaxDb = std::max(pt.tapGrMaxDb, static_cast<double>(gr0[k]));
                const double lo = std::min(gr0[k], gr1[k]);
                run.minGrDb = firstGr ? lo : std::min(run.minGrDb, lo);
                firstGr = false;
            }
            pt.tapGrDb = gr0.empty() ? 0.0 : sum / static_cast<double>(gr0.size());
            run.points.push_back(pt);
            rig.tap().clear();
        }
        return run;
    }

    StepRun runSquareSteps(EngineRig& rig, std::span<const Segment> segments, double peakOffsetDb, double hz)
    {
        const float fs = rig.fs();
        const double fsd = static_cast<double>(fs);
        StepRun run;
        std::size_t total = 0;
        for (const Segment& s : segments)
        {
            run.edges.push_back(total);
            total += samples(s.seconds, fs);
        }

        const std::uint64_t n0 = rig.sampleIndex();
        std::vector<float> in(total), outR(total);
        run.out.assign(total, 0.0f);
        for (std::size_t si = 0; si < segments.size(); ++si)
        {
            const auto amp = static_cast<float>(measure::amplitudeFromDb(segments[si].levelDb + peakOffsetDb));
            const std::size_t end = si + 1 < segments.size() ? run.edges[si + 1] : total;
            for (std::size_t k = run.edges[si]; k < end; ++k)
            {
                // Square at hz: +A for the first half of each period, -A for the second (the phase is an fmod of
                // integers, so exact).
                const double phase = std::fmod(hz * static_cast<double>(n0 + k), fsd);
                in[k] = phase < 0.5 * fsd ? amp : -amp;
            }
        }

        run.tapGrDb.reserve(total);
        constexpr std::size_t kBlock = 4096;
        rig.tap().clear();
        rig.setTapping(true);
        for (std::size_t off = 0; off < total; off += kBlock)
        {
            const std::size_t m = std::min(kBlock, total - off);
            rig.process(in.data() + off, in.data() + off, run.out.data() + off, outR.data() + off, m);
            const std::vector<float> gr0 = rig.tap().lane(rig.tap().grDb, 0);
            run.tapGrDb.insert(run.tapGrDb.end(), gr0.begin(), gr0.end());
            rig.tap().clear();
        }
        rig.setTapping(false);

        const double offset = static_cast<double>(rig.params().preGainDb) + static_cast<double>(rig.makeupTotalDb());
        run.audioGrDb.resize(total);
        for (std::size_t k = 0; k < total; ++k)
        {
            run.nonfinite += (isFinite(run.out[k]) ? 0 : 1) + (isFinite(outR[k]) ? 0 : 1);
            const double ratio = std::fabs(static_cast<double>(run.out[k]) / static_cast<double>(in[k]));
            run.audioGrDb[k] = static_cast<float>(offset - measure::dbFromAmplitude(ratio));
        }
        return run;
    }
} // namespace fcmp::probe
