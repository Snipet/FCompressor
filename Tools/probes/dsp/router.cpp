// FCMP_PROBE layer=dsp name=router scope=global timeout=120
//
// dsp.router (F5, S4; SPRINTS §7 D13; 01 §3.1 `stmode`, §5.4 steps 2a/2c/2e/2i, §5.5; E §8, §9.2 rows 9-10; K2 #3d,
// #22): the host's stereo router (engine/host/Router.h) and its delay lines (engine/host/Delay.h). Spec rows only,
// plus a few golden rows of the end-to-end gains.
//
// Router, per universal stereo code (0..5 and the reserved 6, 7):
//   router.table.mismatches           the route of every code against the table in Router.h's header (domain, side-chain
//                                     source, which lane takes the GR); router.domain.engine_mismatches: the lane
//                                     domain equals ModeEngine's rule for Carry::msDomain (K2 #3d)
//   router.<mode>.roundtrip.mismatches  decode(encode(x)) with the lanes untouched is x BIT FOR BIT (the residual
//                                     decode), for 2^16 random normal floats per channel plus signed zeros and
//                                     extremes, out of place and in place
//   router.<mode>.scaled.max_rel_err  both lanes scaled by g in [0.1, 4]: the output against g x, relative to
//                                     |L| + |R|: <= 1e-6 (the decode is D(w) up to rounding)
//   router.<mode>.split.max_rel_err   the GR of maskGr(6 dB, 6 dB) per lane: the output against the exact decode of the
//                                     scaled lanes (double): <= 1e-6
//   router.<mode>.sc.mismatches       encodeSc against its formulas (M = (L + R) / 2, S = (L - R) / 2 per route), bit
//                                     for bit; router.<mode>.mask.mismatches, router.<mode>.listen.mismatches likewise
//   router.key.*                      keyExternal's truth table (extKey AND an active key bus, K2 #22); selectSc picks
//                                     the key or the main input, duplicates a mono source; scLanes lays {L, R, L, R}
// End to end (router.e2e.*), the reference Mode (slot 0) through a minimal ECO host built from Router.h (select, SC
// encode, control, maskGr, encode, gain, colour, residual decode) at 48 kHz, peak-hold ballistics (fastest attack,
// slowest release), L = m + s and R = m - s with m a 1 kHz and s a 250 Hz sine:
//   router.e2e.<mode>.<a|b>.{m,s}_gain_db  stimulus a: m at T + 10, s at T - 30; b: the reverse; link 0. A component
//                                     the route compresses (M/S: the loud one; MID: M; SIDE: S; M>S: S keyed by M;
//                                     S>M: M keyed by S; STEREO: both) reads <= -3 dB, one it passes reads 0 within
//                                     0.001 dB; ms_linked (M/S at link 1: the link couples M and S) compresses both
//   router.e2e.<mode>.carry_domain    the engine's Carry::msDomain equals Router.h's laneDomain(code)
//   router.e2e.key.<case>.gain_db     STEREO, main at T - 30, a stereo key at T + 10: EXT with an active 2-channel bus
//                                     compresses (<= -3 dB); EXT with the bus inactive and INT do not (0 within 0.001
//                                     dB); router.e2e.key.mono.lanes_equal: a 1-channel key gives both lanes the same GR
//   golden (abs:0.01): router.e2e.<mode>.<a|b>.{m,s}_gain_db
// Delay lines (DelayLine<float> and DelayLine<simd::f32x4>):
//   router.delay.<t>.d<d>.mismatches  out[i] == in[i - d] bit for bit (zeros before the start), d in {0, 1, 63, 64,
//                                     65, 1000, max}; router.delay.<t>.alias.mismatches in place
//   router.delay.clamp                setDelay/setTarget clamp to [0, maxDelay()]
//   router.delay.slew.mismatches      setTarget moves the delay one sample per control tick at absolute multiples of
//                                     kTickSamples: out[i] == in[i - d(i)] with that schedule, at block sizes {1, 17,
//                                     64, 4096}
//   router.delay.reset.nonzero        reset() empties the ring; router.delay.unconfigured.mismatches: passes through
//   router.delay.allocs               allocations in process/reset/setDelay/setTarget: 0
#include "ProbeRegistry.h"

#include "EngineRig.h"
#include "Measure.h"
#include "Signals.h"

#include "fcdsp/core/FastMath.h"
#include "fcdsp/core/ScopedFtz.h"
#include "fcdsp/core/Simd.h"
#include "fcdsp/engine/IEngine.h"
#include "fcdsp/engine/ModeEngine.h"
#include "fcdsp/engine/host/Delay.h"
#include "fcdsp/engine/host/Router.h"
#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/modes/Registry.h"
#include "fcdsp/params/EngineParams.h"
#include "fcdsp/params/Pid.h"
#include "fcdsp/params/Resolve.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    using namespace fcdsp;
    using funkgui::test::Probe;
    using funkgui::test::Tol;
    namespace sig = fcmp::probe::sig;
    namespace measure = fcmp::probe::measure;

    constexpr float kFs = 48000.0f;
    constexpr std::array<const char*, host::kStereoCodes> kModeNames{ "stereo", "ms", "mid", "side",
                                                                       "mks", "skm", "r6", "r7" };

    bool same(float a, float b) noexcept { return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b); }

    // ---- signals ------------------------------------------------------------------------------------------------------

    // Random normal floats in +-[2^-60, 2^60] with random signs, then signed zeros and extremes at the front. (The
    // host's input is sanitised to |x| <= 1e6, 01 §5.1; values near FLT_MAX would overflow the M/S sum, and subnormals
    // are flushed by the FTZ every audio-path operation runs under.)
    std::vector<float> wideNoise(std::size_t n, std::uint64_t seed)
    {
        sig::Pcg32 rng(seed, 3);
        std::vector<float> v(n);
        for (float& x : v)
        {
            const float mant = 1.0f + rng.uniform();
            const int e = static_cast<int>(rng.bounded(121)) - 60;
            x = std::ldexp(mant, e) * ((rng.next() & 1u) != 0u ? -1.0f : 1.0f);
        }
        const float special[] = { 0.0f, -0.0f, 1.0f, -1.0f, 1e6f, -1e6f, 1e-30f, -1e-30f, 1e30f, -1e30f };
        for (std::size_t i = 0; i < std::size(special) && i < n; ++i)
            v[i] = special[i];
        return v;
    }

    // ---- the minimal ECO host (Router.h end to end) --------------------------------------------------------------------

    class MiniHost
    {
    public:
        MiniHost(const ModeEntry& entry, const EngineParams& eng) : eng_(eng)
        {
            if (entry.construct == nullptr || entry.engineBytes > kArenaBytes || entry.engineAlign > 64)
                throw std::runtime_error("MiniHost: the reference Mode does not fit the arena");
            scratch_.assign(4096, 0.0f);
            engine_ = entry.construct(arena_.data());
            engine_->prepare(PrepareInfo{ kFs, 1, std::span<float>(scratch_) });
            engine_->setParams(eng_);
            engine_->snapParams();
        }
        ~MiniHost()
        {
            if (engine_ != nullptr)
                engine_->~IEngine();
        }
        MiniHost(const MiniHost&) = delete;
        MiniHost& operator=(const MiniHost&) = delete;

        IEngine& engine() noexcept { return *engine_; }

        // Any n; key may be null. gr (optional) receives the masked GR lanes per sample.
        void process(const float* l, const float* r, const float* const* key, int numKey, int keyChans, bool extKey,
                     float* outL, float* outR, std::size_t n, std::vector<simd::f32x4>* gr)
        {
            const ScopedFtz ftz;
            const host::Route route = host::routeOf(eng_.stmode);
            const bool ext = host::keyExternal(extKey, keyChans, key, numKey);
            const float pre = simd::lane<0>(linFromDb(simd::set1(eng_.preGainDb)));
            for (std::size_t off = 0; off < n;)
            {
                const int m = static_cast<int>(std::min<std::size_t>(static_cast<std::size_t>(kChunk), n - off));
                const float* mainPtr[2] = { l + off, r + off };
                const float* keyPtr[2] = { nullptr, nullptr };
                if (ext)
                    for (int c = 0; c < numKey && c < 2; ++c)
                        keyPtr[c] = key[c] + off;
                const host::ScSource src = host::selectSc(mainPtr, 2, keyPtr, numKey, ext);
                host::scLanes(src, sc_.data(), m);
                if (!ext)
                    for (int i = 0; i < m; ++i)
                        sc_[static_cast<std::size_t>(i)] = simd::mul(sc_[static_cast<std::size_t>(i)], simd::set1(pre));
                host::encodeSc(sc_.data(), sc_.data(), m, route);

                ControlIo io;
                io.n = m;
                io.sampleIndex = index_;
                io.sc = sc_.data();
                io.grDb = gr_.data();
                io.keyExternal = ext;
                engine_->control(io);

                host::encode(l + off, r + off, w0_.data(), w1_.data(), m, route);
                for (int i = 0; i < m; ++i)
                {
                    const auto u = static_cast<std::size_t>(i);
                    const simd::f32x4 g = host::maskGr(gr_[u], route);
                    if (gr != nullptr)
                        gr->push_back(g);
                    grL_[u] = simd::lane<0>(g);
                    grR_[u] = simd::lane<1>(g);
                    w0_[u] *= simd::lane<0>(linFromDb(simd::set1(eng_.preGainDb - grL_[u])));
                    w1_[u] *= simd::lane<0>(linFromDb(simd::set1(eng_.preGainDb - grR_[u])));
                }
                float* wet[2] = { w0_.data(), w1_.data() };
                const float* grOs[2] = { grL_.data(), grR_.data() };
                AudioIo aio;
                aio.nOs = m;
                aio.wet = wet;
                aio.grDbOs = grOs;
                engine_->colour(aio);
                host::decode(w0_.data(), w1_.data(), l + off, r + off, outL + off, outR + off, m, route);
                index_ += static_cast<std::uint64_t>(m);
                off += static_cast<std::size_t>(m);
            }
        }

    private:
        EngineParams eng_;
        alignas(64) std::array<std::byte, kArenaBytes> arena_{};
        IEngine* engine_ = nullptr;
        std::vector<float> scratch_;
        std::uint64_t index_ = 0;
        alignas(16) std::array<simd::f32x4, kChunk> sc_{}, gr_{};
        std::array<float, kChunk> w0_{}, w1_{}, grL_{}, grR_{};
    };

    // The reference Mode's parameters for the e2e rows: its defaults with the fastest attack and slowest release its
    // specs allow (peak-hold ballistics, so a sine's GR is steady; dsp.static's choice), the given stereo code and link.
    EngineParams e2eParams(const ModeEntry& en, int code, float link)
    {
        RawParams raw = fcmp::probe::modeRaw(en);
        ParamView v;
        resolveView(*en.desc, raw, v);
        const ParamSpec* atk = v.spec[idx(Pid::atk)];
        const ParamSpec* rel = v.spec[idx(Pid::rel)];
        if (atk != nullptr && (atk->kind == Kind::continuous || atk->kind == Kind::hybrid))
            raw[Pid::atk] = atk->lo;
        if (rel != nullptr && (rel->kind == Kind::continuous || rel->kind == Kind::hybrid))
            raw[Pid::rel] = rel->hi;
        EngineParams e = fcmp::probe::resolveRaw(en, raw).eng;
        e.stmode = static_cast<std::uint8_t>(code);      // the router's input is the code; the engine's lane domain too
        e.mix = 1.0f;
        e.link = link;                                   // M/S: link k couples the M and S lanes (E §8: usually 0)
        return e;
    }

    struct Gains
    {
        double m = 0, s = 0;
    };

    // m at levels (mDb, sDb) relative to the input threshold; returns the single-bin gains of M' at 1 kHz and S' at 250 Hz.
    Gains e2eRun(const ModeEntry& en, const EngineParams& e, double mDb, double sDb, std::int64_t& carryDomain)
    {
        const std::size_t hold = 24000, win = 4800, n = hold + win;
        const double t = static_cast<double>(e.thrDb - e.preGainDb);
        const double am = measure::amplitudeFromDb(t + mDb), as = measure::amplitudeFromDb(t + sDb);
        std::vector<float> l(n), r(n), m(n), s(n), outL(n), outR(n);
        for (std::size_t i = 0; i < n; ++i)
        {
            m[i] = sig::sineAt(static_cast<std::int64_t>(i), 1000.0, kFs, am);
            s[i] = sig::sineAt(static_cast<std::int64_t>(i), 250.0, kFs, as);
            l[i] = m[i] + s[i];
            r[i] = m[i] - s[i];
        }
        MiniHost h(en, e);
        h.process(l.data(), r.data(), nullptr, 0, 0, false, outL.data(), outR.data(), n, nullptr);
        carryDomain = h.engine().carry().msDomain;
        std::vector<float> mo(win), so(win);
        for (std::size_t i = 0; i < win; ++i)
        {
            mo[i] = 0.5f * (outL[hold + i] + outR[hold + i]);
            so[i] = 0.5f * (outL[hold + i] - outR[hold + i]);
        }
        const measure::SingleBin b1(1000.0, kFs, win), b2(250.0, kFs, win);
        const std::span<const float> mi(m.data() + hold, win), si(s.data() + hold, win);
        return { b1.gainDb(mi, mo, static_cast<std::int64_t>(hold)), b2.gainDb(si, so, static_cast<std::int64_t>(hold)) };
    }

    // ---- delay helpers --------------------------------------------------------------------------------------------------

    template <class T> T valueOf(float x) noexcept;
    template <> float valueOf<float>(float x) noexcept { return x; }
    template <> simd::f32x4 valueOf<simd::f32x4>(float x) noexcept
    {
        alignas(16) const float v[4] = { x, -x, 0.5f * x, x + 1.0f };
        return simd::load(v);
    }

    bool sameT(float a, float b) noexcept { return same(a, b); }
    bool sameT(simd::f32x4 a, simd::f32x4 b) noexcept
    {
        return same(simd::lane<0>(a), simd::lane<0>(b)) && same(simd::lane<1>(a), simd::lane<1>(b))
            && same(simd::lane<2>(a), simd::lane<2>(b)) && same(simd::lane<3>(a), simd::lane<3>(b));
    }

    template <class T>
    std::vector<T> delayInput(std::size_t n, std::uint64_t seed)
    {
        sig::Pcg32 rng(seed, 5);
        std::vector<T> v(n);
        for (T& x : v)
            x = valueOf<T>(rng.bipolar());
        return v;
    }

    template <class T>
    void delayRows(Probe& P, const char* tname)
    {
        constexpr int kMax = 2000;
        const std::size_t n = 8192;
        const std::vector<T> in = delayInput<T>(n, 21);
        const std::string k = std::string("router.delay.") + tname;

        for (const int d : { 0, 1, 63, 64, 65, 1000, kMax })
        {
            host::DelayLine<T> line;
            line.configure(kMax);
            line.setDelay(d);
            std::vector<T> out(n);
            for (std::size_t off = 0; off < n; off += 100)
            {
                const std::size_t m = std::min<std::size_t>(100, n - off);
                line.process(in.data() + off, out.data() + off, static_cast<int>(m), off);
            }
            std::int64_t bad = 0;
            for (std::size_t i = 0; i < n; ++i)
                bad += sameT(out[i], i >= static_cast<std::size_t>(d) ? in[i - static_cast<std::size_t>(d)] : T{}) ? 0 : 1;
            P.eq(k + ".d" + std::to_string(d) + ".mismatches", bad, 0);
        }
        {
            host::DelayLine<T> line;
            line.configure(kMax);
            line.setDelay(65);
            std::vector<T> buf(in);
            for (std::size_t off = 0; off < n; off += 64)
                line.process(buf.data() + off, buf.data() + off, 64, off);
            std::int64_t bad = 0;
            for (std::size_t i = 0; i < n; ++i)
                bad += sameT(buf[i], i >= 65 ? in[i - 65] : T{}) ? 0 : 1;
            P.eq(k + ".alias.mismatches", bad, 0);
        }
        {
            // Slew from 100 to 110 set at sample 1000, then to 95 at 3000: one step per tick at absolute multiples of
            // kTickSamples (the first at 1008), whatever the block size.
            std::vector<int> expected(n);
            int d = 100, tgt = 100;
            for (std::size_t i = 0; i < n; ++i)
            {
                if (i == 1000)
                    tgt = 110;
                if (i == 3000)
                    tgt = 95;
                if (d != tgt && i % static_cast<std::size_t>(kTickSamples) == 0)
                    d += tgt > d ? 1 : -1;
                expected[i] = d;
            }
            std::int64_t bad = 0;
            for (const int block : { 1, 17, 64, 4096 })
            {
                host::DelayLine<T> line;
                line.configure(kMax);
                line.setDelay(100);
                std::vector<T> out(n);
                // Targets are set at the samples 1000 and 3000 exactly: split the blocks there.
                for (std::size_t off = 0; off < n;)
                {
                    std::size_t m = std::min<std::size_t>(static_cast<std::size_t>(block), n - off);
                    for (const std::size_t edge : { std::size_t{ 1000 }, std::size_t{ 3000 } })
                        if (off < edge && off + m > edge)
                            m = edge - off;
                    if (off == 1000)
                        line.setTarget(110);
                    if (off == 3000)
                        line.setTarget(95);
                    line.process(in.data() + off, out.data() + off, static_cast<int>(m), off);
                    off += m;
                }
                for (std::size_t i = 0; i < n; ++i)
                {
                    const auto de = static_cast<std::size_t>(expected[i]);
                    bad += sameT(out[i], i >= de ? in[i - de] : T{}) ? 0 : 1;
                }
                bad += line.delay() == 95 ? 0 : 1;
            }
            P.eq(k + ".slew.mismatches", bad, 0);
        }
    }
} // namespace

FCMP_PROBE(dsp, router)
{
    // ---- the table ---------------------------------------------------------------------------------------------------
    {
        struct Want { host::StereoMode mode; LaneDomain domain; int scFrom; bool g0, g1; };
        constexpr Want want[host::kStereoCodes] = {
            { host::StereoMode::stereo, LaneDomain::lr, -1, true, true },
            { host::StereoMode::midSide, LaneDomain::ms, -1, true, true },
            { host::StereoMode::mid, LaneDomain::ms, 0, true, false },
            { host::StereoMode::side, LaneDomain::ms, 1, false, true },
            { host::StereoMode::midKeysSide, LaneDomain::ms, 0, false, true },
            { host::StereoMode::sideKeysMid, LaneDomain::ms, 1, true, false },
            { host::StereoMode::midSide, LaneDomain::ms, -1, true, true },
            { host::StereoMode::midSide, LaneDomain::ms, -1, true, true } };
        std::int64_t bad = 0, engine = 0;
        for (int c = 0; c < host::kStereoCodes; ++c)
        {
            const host::Route r = host::routeOf(static_cast<std::uint8_t>(c));
            const Want& w = want[c];
            bad += r.mode == w.mode && r.domain == w.domain && r.scFrom == w.scFrom && r.gain0 == w.g0 && r.gain1 == w.g1
                     ? 0 : 1;
            EngineParams p;
            p.stmode = static_cast<std::uint8_t>(c);
            engine += detail::modeengine::laneDomain(p) == static_cast<std::uint8_t>(host::laneDomain(p.stmode)) ? 0 : 1;
        }
        P.eq("router.table.mismatches", bad, 0);
        P.eq("router.domain.engine_mismatches", engine, 0);
    }

    // ---- encode / decode ---------------------------------------------------------------------------------------------
    const std::size_t n = 65536;
    const std::vector<float> L = wideNoise(n, 1), R = wideNoise(n, 2);
    sig::Pcg32 grng(99, 1);
    std::vector<float> gains(n);
    for (float& g : gains)
        g = 0.1f + 3.9f * grng.uniform();
    for (int c = 0; c < host::kStereoCodes; ++c)
    {
        const host::Route rt = host::routeOf(static_cast<std::uint8_t>(c));
        const std::string k = std::string("router.") + kModeNames[static_cast<std::size_t>(c)];
        std::vector<float> c0(n), c1(n), oL(n), oR(n);

        // Round trip, out of place and in place (the outputs over the processed lanes, as a host may).
        host::encode(L.data(), R.data(), c0.data(), c1.data(), static_cast<int>(n), rt);
        host::decode(c0.data(), c1.data(), L.data(), R.data(), oL.data(), oR.data(), static_cast<int>(n), rt);
        std::int64_t bad = 0;
        for (std::size_t i = 0; i < n; ++i)
            bad += (same(oL[i], L[i]) ? 0 : 1) + (same(oR[i], R[i]) ? 0 : 1);
        std::vector<float> w0(c0), w1(c1);
        host::decode(w0.data(), w1.data(), L.data(), R.data(), w0.data(), w1.data(), static_cast<int>(n), rt);
        for (std::size_t i = 0; i < n; ++i)
            bad += (same(w0[i], L[i]) ? 0 : 1) + (same(w1[i], R[i]) ? 0 : 1);
        P.eq(k + ".roundtrip.mismatches", bad, 0);

        // Processing: both lanes scaled (the decode is D(w) up to rounding), then the GR split of maskGr.
        double scaled = 0.0, split = 0.0;
        const simd::f32x4 gr = host::maskGr(simd::set1(6.0f), rt);
        const double g0 = static_cast<double>(simd::lane<0>(linFromDb(simd::set1(-simd::lane<0>(gr)))));
        const double g1 = static_cast<double>(simd::lane<0>(linFromDb(simd::set1(-simd::lane<1>(gr)))));
        for (std::size_t i = 0; i < 4096; ++i)
        {
            const std::size_t j = 10 + i;                           // past the extremes (3e38 * 4 overflows)
            const float a = 0.5f * (L[j] >= 0.0f ? 1.0f : -1.0f) * std::fabs(std::fmod(L[j], 1.0f));
            const float b = 0.5f * (R[j] >= 0.0f ? 1.0f : -1.0f) * std::fabs(std::fmod(R[j], 1.0f));
            float e0 = 0.0f, e1 = 0.0f, y0 = 0.0f, y1 = 0.0f;
            host::encode(&a, &b, &e0, &e1, 1, rt);
            float s0 = e0 * gains[i], s1 = e1 * gains[i];
            host::decode(&s0, &s1, &a, &b, &y0, &y1, 1, rt);
            const double norm = std::max(1e-30, std::fabs(static_cast<double>(a)) + std::fabs(static_cast<double>(b)));
            scaled = std::max(scaled, std::max(std::fabs(static_cast<double>(y0) - static_cast<double>(gains[i]) * a),
                                               std::fabs(static_cast<double>(y1) - static_cast<double>(gains[i]) * b))
                                          / (norm * static_cast<double>(gains[i])));
            float t0 = e0 * static_cast<float>(g0), t1 = e1 * static_cast<float>(g1);
            host::decode(&t0, &t1, &a, &b, &y0, &y1, 1, rt);
            const bool ms = rt.domain == LaneDomain::ms;
            const double m = ms ? 0.5 * (static_cast<double>(a) + b) : static_cast<double>(a);
            const double s = ms ? 0.5 * (static_cast<double>(a) - b) : static_cast<double>(b);
            const double x0 = ms ? m * g0 + s * g1 : m * g0, x1 = ms ? m * g0 - s * g1 : s * g1;
            split = std::max(split, std::max(std::fabs(static_cast<double>(y0) - x0), std::fabs(static_cast<double>(y1) - x1))
                                        / norm);
        }
        P.le(k + ".scaled.max_rel_err", scaled, 1e-6);
        P.le(k + ".split.max_rel_err", split, 1e-6);

        // Side-chain encode, GR mask and listen against their formulas.
        std::int64_t scBad = 0, maskBad = 0, listenBad = 0;
        std::vector<simd::f32x4> sc(1024), enc(1024);
        for (std::size_t i = 0; i < sc.size(); ++i)
        {
            alignas(16) const float v[4] = { L[100 + i], R[100 + i], 7.0f, 9.0f };
            sc[i] = simd::load(v);
        }
        host::encodeSc(sc.data(), enc.data(), static_cast<int>(sc.size()), rt);
        std::vector<float> ll(sc.size()), lr(sc.size());
        host::listen(enc.data(), ll.data(), lr.data(), static_cast<int>(sc.size()), rt);
        for (std::size_t i = 0; i < sc.size(); ++i)
        {
            const float a = simd::lane<0>(sc[i]), b = simd::lane<1>(sc[i]);
            const float m = 0.5f * (a + b), s = 0.5f * (a - b);
            float c0w = a, c1w = b;
            if (rt.domain == LaneDomain::ms)
            {
                c0w = rt.scFrom == 1 ? s : m;
                c1w = rt.scFrom == 0 ? m : s;
            }
            const simd::f32x4 e = enc[i];
            scBad += same(simd::lane<0>(e), c0w) && same(simd::lane<1>(e), c1w) && same(simd::lane<2>(e), c0w)
                          && same(simd::lane<3>(e), c1w) ? 0 : 1;
            float l0 = c0w, l1 = c1w;
            if (rt.domain == LaneDomain::ms)
            {
                l0 = rt.scFrom >= 0 ? c0w : c0w + c1w;
                l1 = rt.scFrom >= 0 ? c0w : c0w - c1w;
            }
            listenBad += same(ll[i], l0) && same(lr[i], l1) ? 0 : 1;
        }
        alignas(16) const float gv[4] = { 3.0f, 4.0f, 5.0f, 6.0f };
        const simd::f32x4 masked = host::maskGr(simd::load(gv), rt);
        maskBad += simd::lane<0>(masked) == (rt.gain0 ? 3.0f : 0.0f) ? 0 : 1;
        maskBad += simd::lane<1>(masked) == (rt.gain1 ? 4.0f : 0.0f) ? 0 : 1;
        maskBad += simd::lane<2>(masked) == 5.0f && simd::lane<3>(masked) == 6.0f ? 0 : 1;
        P.eq(k + ".sc.mismatches", scBad, 0);
        P.eq(k + ".mask.mismatches", maskBad, 0);
        P.eq(k + ".listen.mismatches", listenBad, 0);
    }

    // ---- key select (K2 #22) -----------------------------------------------------------------------------------------
    {
        float a[4] = { 1, 2, 3, 4 }, b[4] = { 5, 6, 7, 8 }, ka[4] = { 9, 10, 11, 12 }, kb[4] = { 13, 14, 15, 16 };
        const float* main2[2] = { a, b };
        const float* main1[2] = { a, nullptr };
        const float* key2[2] = { ka, kb };
        const float* key1[2] = { ka, nullptr };
        const float* keyNone[2] = { nullptr, nullptr };
        std::int64_t bad = 0;
        // keyExternal: extKey AND configured channels AND a delivered first channel.
        bad += host::keyExternal(true, 2, key2, 2) ? 0 : 1;
        bad += host::keyExternal(true, 1, key1, 1) ? 0 : 1;
        bad += !host::keyExternal(false, 2, key2, 2) ? 0 : 1;
        bad += !host::keyExternal(true, 0, key2, 2) ? 0 : 1;
        bad += !host::keyExternal(true, 2, key2, 0) ? 0 : 1;
        bad += !host::keyExternal(true, 2, keyNone, 2) ? 0 : 1;
        bad += !host::keyExternal(true, 2, nullptr, 2) ? 0 : 1;
        P.eq("router.key.external.mismatches", bad, 0);

        std::int64_t sel = 0;
        host::ScSource s = host::selectSc(main2, 2, key2, 2, true);
        sel += s.ch[0] == ka && s.ch[1] == kb && s.external ? 0 : 1;
        s = host::selectSc(main2, 2, key1, 1, true);
        sel += s.ch[0] == ka && s.ch[1] == ka ? 0 : 1;                  // a mono key feeds both
        s = host::selectSc(main2, 2, key2, 2, false);
        sel += s.ch[0] == a && s.ch[1] == b && !s.external ? 0 : 1;     // internal: the main input
        s = host::selectSc(main1, 1, key2, 2, false);
        sel += s.ch[0] == a && s.ch[1] == a ? 0 : 1;                    // a mono main feeds both
        std::array<simd::f32x4, 4> lanes{};
        host::scLanes(host::selectSc(main2, 2, key2, 2, false), lanes.data(), 4);
        for (std::size_t i = 0; i < 4; ++i)
            sel += simd::lane<0>(lanes[i]) == a[i] && simd::lane<1>(lanes[i]) == b[i] && simd::lane<2>(lanes[i]) == a[i]
                        && simd::lane<3>(lanes[i]) == b[i] ? 0 : 1;
        P.eq("router.key.select.mismatches", sel, 0);
    }

    // ---- end to end on the reference Mode ------------------------------------------------------------------------------
    {
        const ModeEntry* en = bySlot(0);
        if (en == nullptr || en->desc == nullptr)
        {
            P.harnessError("router: slot 0 (the reference Mode) is not registered");
            return P.finish();
        }
        // Which component each route compresses, for stimulus a (m loud) and b (s loud): {m, s}. Link 0, so M/S runs
        // its lanes independently; "ms_linked" is M/S at link 1 (LinkMax couples the lanes: both compress).
        struct Expect { const char* name; int code; float link; bool am, as, bm, bs; };
        constexpr Expect expect[] = {
            { "stereo", 0, 0.0f, true, true, true, true },         // STEREO: L and R both carry the loud component
            { "ms", 1, 0.0f, true, false, false, true },           // M/S
            { "mid", 2, 0.0f, true, false, false, false },         // MID
            { "side", 3, 0.0f, false, false, false, true },        // SIDE
            { "mks", 4, 0.0f, false, true, false, false },         // M>S: the mid keys the side
            { "skm", 5, 0.0f, false, false, true, false },         // S>M: the side keys the mid
            { "ms_linked", 1, 1.0f, true, true, true, true } };
        for (const Expect& x : expect)
        {
            const int c = x.code;
            const EngineParams e = e2eParams(*en, c, x.link);
            const std::string k = std::string("router.e2e.") + x.name;
            std::int64_t domA = -1, domB = -1;
            const Gains a = e2eRun(*en, e, 10.0, -30.0, domA);
            const Gains b = e2eRun(*en, e, -30.0, 10.0, domB);
            const auto row = [&P](const std::string& key, double got, bool compressed) {
                if (compressed)
                    P.le(key, got, -3.0);
                else
                    P.near(key, got, 0.0, 0.001);
                P.num(key, got, Tol::abs(0.01));
            };
            row(k + ".a.m_gain_db", a.m, x.am);
            row(k + ".a.s_gain_db", a.s, x.as);
            row(k + ".b.m_gain_db", b.m, x.bm);
            row(k + ".b.s_gain_db", b.s, x.bs);
            const auto want = static_cast<std::int64_t>(host::laneDomain(static_cast<std::uint8_t>(c)));
            P.eq(k + ".carry_domain", domA == want && domB == want ? 1 : 0, 1);
        }

        // Key routing: STEREO, main quiet, a loud stereo (or mono) key.
        const EngineParams e = e2eParams(*en, 0, 1.0f);
        const std::size_t hold = 24000, win = 4800, n2 = hold + win;
        const double t = static_cast<double>(e.thrDb - e.preGainDb);
        std::vector<float> main(n2), keyL(n2), keyR(n2), outL(n2), outR(n2);
        for (std::size_t i = 0; i < n2; ++i)
        {
            main[i] = sig::sineAt(static_cast<std::int64_t>(i), 250.0, kFs, measure::amplitudeFromDb(t - 30.0));
            keyL[i] = sig::sineAt(static_cast<std::int64_t>(i), 1000.0, kFs, measure::amplitudeFromDb(t + 10.0));
            keyR[i] = sig::sineAt(static_cast<std::int64_t>(i), 1500.0, kFs, measure::amplitudeFromDb(t + 10.0));
        }
        const float* key2[2] = { keyL.data(), keyR.data() };
        const measure::SingleBin bin(250.0, kFs, win);
        const std::span<const float> mi(main.data() + hold, win);
        struct KeyCase { const char* name; int numKey, keyChans; bool ext, compressed; };
        const KeyCase cases[] = { { "ext", 2, 2, true, true }, { "ext_inactive", 2, 0, true, false },
                                  { "int", 2, 2, false, false }, { "mono", 1, 1, true, true } };
        for (const KeyCase& kc : cases)
        {
            MiniHost h(*en, e);
            std::vector<simd::f32x4> gr;
            h.process(main.data(), main.data(), key2, kc.numKey, kc.keyChans, kc.ext, outL.data(), outR.data(), n2, &gr);
            const double g = bin.gainDb(mi, std::span<const float>(outL.data() + hold, win), static_cast<std::int64_t>(hold));
            const std::string k = std::string("router.e2e.key.") + kc.name + ".gain_db";
            if (kc.compressed)
                P.le(k, g, -3.0);
            else
                P.near(k, g, 0.0, 0.001);
            if (std::string(kc.name) == "mono")
            {
                std::int64_t diff = 0;
                for (const simd::f32x4& v : gr)
                    diff += same(simd::lane<0>(v), simd::lane<1>(v)) ? 0 : 1;
                P.eq("router.e2e.key.mono.lanes_equal", diff == 0 ? 1 : 0, 1);
            }
        }
    }

    // ---- delay lines ---------------------------------------------------------------------------------------------------
    delayRows<float>(P, "f32");
    delayRows<simd::f32x4>(P, "f32x4");
    {
        host::DelayLine<float> line;
        line.configure(300);
        std::int64_t bad = 0;
        line.setDelay(-5);
        bad += line.delay() == 0 ? 0 : 1;
        line.setDelay(10000);
        bad += line.delay() == 300 && line.maxDelay() == 300 ? 0 : 1;
        line.setTarget(-1);
        bad += line.target() == 0 ? 0 : 1;
        line.setTarget(301);
        bad += line.target() == 300 ? 0 : 1;
        P.eq("router.delay.clamp", bad, 0);

        std::vector<float> in(1024, 0.25f), out(1024);
        std::int64_t allocs = 0;
        {
            const fcmp::probe::alloc::Scope scope;
            line.setDelay(200);
            line.process(in.data(), out.data(), 1024, 0);
            line.setTarget(100);
            line.process(in.data(), out.data(), 1024, 1024);
            line.reset();
            allocs = static_cast<std::int64_t>(fcmp::probe::alloc::allocations());
        }
        P.eq("router.delay.allocs", allocs, 0);
        std::vector<float> zeros(1024, 0.0f);
        line.setDelay(300);
        line.process(zeros.data(), out.data(), 1024, 2048);
        std::int64_t nonzero = 0;
        for (const float v : out)
            nonzero += v != 0.0f ? 1 : 0;
        P.eq("router.delay.reset.nonzero", nonzero, 0);

        host::DelayLine<float> bare;                               // never configured: passes through
        std::vector<float> o2(1024);
        bare.process(in.data(), o2.data(), 1024, 0);
        std::int64_t pass = 0;
        for (std::size_t i = 0; i < in.size(); ++i)
            pass += same(o2[i], in[i]) ? 0 : 1;
        P.eq("router.delay.unconfigured.mismatches", pass, 0);
    }

    return P.finish();
}
