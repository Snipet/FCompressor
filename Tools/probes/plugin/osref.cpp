// FCMP_PROBE layer=proc name=osref scope=global timeout=180
//
// proc.osref (F6, S2; 03 §3.5; ADR-60; K1 #29, K2 #28): fcdsp's Oversampler against juce::dsp::Oversampling as a
// REFERENCE, never a dependency: STD against JUCE's 2x polyphase IIR, HQ against JUCE's 4x equiripple FIR, both at
// maximum quality. Filters only: no latency comparison (latency is dsp.os's declared-equals-measured row; JUCE's own
// figure is only NOTEd). Both round trips are LTI at base rate, so each side is measured from impulse responses in
// double precision, the same way dsp.os measures fcdsp alone:
//   passband   the round trip's magnitude at tones 20 Hz..20 kHz (10 Hz steps) at 44.1 kHz: ours within 0.1 dB of
//              JUCE's at every tone, and ours within 0.1 dB of flat;
//   images     up(): the worst image-to-tone ratio over the same tones, from the up impulse response at the OS rate;
//              ours at least JUCE's, over the tones to 20 kHz and again over the tones to 18 kHz (where JUCE's images
//              are in its stopband too, so the comparison is not only about its wider transition band);
//   aliases    down(): the worst ratio of the components that fold onto a tone to the tone, from the down path's
//              OS-rate response (one impulse per input phase); ours at least JUCE's, both ways.
// Spec rows only (JUCE is a moving reference; fcdsp's own values are dsp.os's golden rows).
#include "ProbeRegistry.h"

#include "fcdsp/engine/Oversampler.h"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <functional>
#include <memory>
#include <numbers>
#include <string>
#include <tuple>
#include <vector>

namespace
{
    using funkgui::test::Probe;
    using fcdsp::Quality;
    using Signal = std::vector<float>;
    using cplx = std::complex<double>;

    constexpr double kPi = std::numbers::pi;
    constexpr double kFs = 44100.0;
    constexpr int kBlock = 256;                     // base samples per call
    constexpr int kLen = 4096;                      // impulse-response length, base samples (both IIRs decay by ~2000)

    std::size_t sz(int n) { return static_cast<std::size_t>(n); }
    double db(double x) { return 20.0 * std::log10(x); }

    // One oversampler's three impulse responses: the round trip at base rate, up() and down() at the OS rate.
    struct Measured
    {
        Signal g, hUp, hDown;
    };

    Signal trimmed(Signal h)
    {
        while (h.size() > 1 && h.back() == 0.0f)
            h.pop_back();
        return h;
    }

    cplx dtft(const Signal& h, double nu)
    {
        const cplx w = std::polar(1.0, -2.0 * kPi * nu);
        cplx z(1.0, 0.0), acc(0.0, 0.0);
        for (std::size_t n = 0; n < h.size(); ++n)
        {
            if (h[n] != 0.0f)
                acc += static_cast<double>(h[n]) * z;
            z *= w;
            if ((n & 511u) == 511u)
                z = std::polar(1.0, -2.0 * kPi * std::fmod(nu * static_cast<double>(n + 1), 1.0));
        }
        return acc;
    }

    std::vector<double> tones()
    {
        std::vector<double> t;
        for (int f = 20; f <= 20000; f += 10)
            t.push_back(static_cast<double>(f));
        return t;
    }

    std::vector<double> mirrors(double f, double fs, int F)
    {
        std::vector<double> m;
        for (int k = 1; k < F; ++k)
            for (const double g : { k * fs - f, k * fs + f })
                if (g > 0.0 && g < 0.5 * F * fs)
                    m.push_back(g);
        return m;
    }

    // Per tone: the rejection |H(f)| / max |H(mirror)| in dB (H at the OS rate).
    std::vector<double> rejection(const Signal& hOs, int F)
    {
        std::vector<double> r;
        for (const double f : tones())
        {
            const double pass = std::abs(dtft(hOs, f / (F * kFs)));
            double leak = 0.0;
            for (const double g : mirrors(f, kFs, F))
                leak = std::max(leak, std::abs(dtft(hOs, g / (F * kFs))));
            r.push_back(db(pass / leak));
        }
        return r;
    }

    std::vector<double> passbandDb(const Signal& g)
    {
        std::vector<double> m;
        for (const double f : tones())
            m.push_back(db(std::abs(dtft(g, f / kFs))));
        return m;
    }

    using BlockFn = std::function<void(const float*, int, float*)>;

    // Impulse responses from block-processing callbacks. makeUp/makeDown/makeRoundTrip return a fresh (reset) chain.
    Measured measure(int F, const std::function<BlockFn()>& makeUp, const std::function<BlockFn()>& makeDown,
                     const std::function<BlockFn()>& makeRoundTrip)
    {
        Measured m;
        Signal x(sz(kLen), 0.0f);
        x[0] = 1.0f;
        {
            const BlockFn rt = makeRoundTrip();
            m.g.assign(sz(kLen), 0.0f);
            for (int i = 0; i < kLen; i += kBlock)
                rt(x.data() + i, kBlock, m.g.data() + i);
        }
        {
            const BlockFn up = makeUp();
            m.hUp.assign(sz(kLen * F), 0.0f);
            for (int i = 0; i < kLen; i += kBlock)
                up(x.data() + i, kBlock, m.hUp.data() + i * F);
        }
        m.hDown.assign(sz(kLen * F), 0.0f);
        // y[n] = sum_m h[F n - m] x[m], so an impulse at OS phase p gives h[F n - p]
        for (int p = 0; p < F; ++p)
        {
            const BlockFn down = makeDown();
            Signal xo(sz(kLen * F), 0.0f), y(sz(kLen), 0.0f);
            xo[sz(p)] = 1.0f;
            for (int i = 0; i < kLen; i += kBlock)
                down(xo.data() + i * F, kBlock, y.data() + i);
            for (int n = 0; n < kLen; ++n)
                if (const int j = F * n - p; j >= 0)
                    m.hDown[sz(j)] = y[sz(n)];
        }
        m.g = trimmed(m.g);
        m.hUp = trimmed(m.hUp);
        m.hDown = trimmed(m.hDown);
        return m;
    }

    // ---- fcdsp ------------------------------------------------------------------------------------------------------
    struct Ours
    {
        fcdsp::Oversampler os;
        Signal buf;
    };

    Measured measureOurs(Quality q)
    {
        const int F = fcdsp::kOs[static_cast<int>(q)].factor;
        auto make = [q, F](int mode) {
            auto s = std::make_shared<Ours>();
            s->os.configure(q, kBlock, 1);
            s->buf.assign(sz(kBlock * F), 0.0f);
            return BlockFn([s, mode, F](const float* in, int n, float* out) {
                if (mode == 0)                                               // up
                {
                    float* o[1] = { out };
                    s->os.up(&in, n, o);
                }
                else if (mode == 1)                                          // down
                {
                    float* o[1] = { out };
                    s->os.down(&in, n * F, o);
                }
                else                                                         // round trip
                {
                    float* os[1] = { s->buf.data() };
                    const float* osc[1] = { s->buf.data() };
                    float* o[1] = { out };
                    s->os.down(osc, s->os.up(&in, n, os), o);
                }
            });
        };
        return measure(F, [&] { return make(0); }, [&] { return make(1); }, [&] { return make(2); });
    }

    // ---- JUCE -------------------------------------------------------------------------------------------------------
    using JuceOs = juce::dsp::Oversampling<float>;

    std::shared_ptr<JuceOs> makeJuce(Quality q)
    {
        const auto type = q == Quality::std ? JuceOs::filterHalfBandPolyphaseIIR : JuceOs::filterHalfBandFIREquiripple;
        auto j = std::make_shared<JuceOs>(1u, q == Quality::std ? 1u : 2u, type, true, false);   // 2x / 4x, max quality
        j->initProcessing(sz(kBlock));
        j->reset();
        return j;
    }

    Measured measureJuce(Quality q, double* latency)
    {
        const int F = q == Quality::std ? 2 : 4;
        *latency = static_cast<double>(makeJuce(q)->getLatencyInSamples());
        auto make = [q, F](int mode) {
            auto j = makeJuce(q);
            return BlockFn([j, mode, F](const float* in, int n, float* out) {
                const float* inCh[1] = { in };
                float* outCh[1] = { out };
                if (mode == 0)                                               // up: copy the OS block out
                {
                    const auto os = j->processSamplesUp(juce::dsp::AudioBlock<const float>(inCh, 1, sz(n)));
                    for (int i = 0; i < n * F; ++i)
                        out[i] = os.getSample(0, i);
                }
                else if (mode == 1)                                          // down: our samples in JUCE's OS block
                {
                    Signal zeros(sz(n), 0.0f);
                    const float* zCh[1] = { zeros.data() };
                    auto os = j->processSamplesUp(juce::dsp::AudioBlock<const float>(zCh, 1, sz(n)));
                    for (int i = 0; i < n * F; ++i)
                        os.setSample(0, i, in[i]);
                    juce::dsp::AudioBlock<float> outBlock(outCh, 1, sz(n));
                    j->processSamplesDown(outBlock);
                }
                else                                                         // round trip
                {
                    j->processSamplesUp(juce::dsp::AudioBlock<const float>(inCh, 1, sz(n)));
                    juce::dsp::AudioBlock<float> outBlock(outCh, 1, sz(n));
                    j->processSamplesDown(outBlock);
                }
            });
        };
        return measure(F, [&] { return make(0); }, [&] { return make(1); }, [&] { return make(2); });
    }

    double minOf(const std::vector<double>& v) { return *std::min_element(v.begin(), v.end()); }

    void compare(Probe& P, Quality q)
    {
        const char* name = q == Quality::std ? "std" : "hq";
        const int F = fcdsp::kOs[static_cast<int>(q)].factor;
        const std::string k = std::string("osref.") + name + ".";
        double juceLatency = 0.0;
        const Measured ours = measureOurs(q), ref = measureJuce(q, &juceLatency);
        std::printf("NOTE     proc.osref %s: JUCE reports %.3f samples of latency (fcdsp declares %d; not compared, "
                    "ADR-60)\n", name, juceLatency, fcdsp::kOs[static_cast<int>(q)].latency);

        // passband
        const std::vector<double> po = passbandDb(ours.g), pj = passbandDb(ref.g);
        double diff = 0.0, devOurs = 0.0, devJuce = 0.0;
        for (std::size_t i = 0; i < po.size(); ++i)
        {
            diff = std::max(diff, std::fabs(po[i] - pj[i]));
            devOurs = std::max(devOurs, std::fabs(po[i]));
            devJuce = std::max(devJuce, std::fabs(pj[i]));
        }
        std::printf("NOTE     proc.osref %s: passband to 20 kHz at 44.1 kHz: fcdsp within %.3g dB, JUCE within %.3g "
                    "dB\n", name, devOurs, devJuce);
        P.le(k + "passband.diff_db", diff, 0.1);
        P.le(k + "passband.dev_db", devOurs, 0.1);

        // images (up) and aliases (down): the worst tone of each side over 20 Hz..20 kHz (at the top, JUCE's wider
        // transition band decides), and over 20 Hz..18 kHz, where both sides' mirrors lie in their stopbands. A per-tone
        // comparison would compare where each equiripple design happens to put its zeros, so it is not a row.
        for (const auto& [what, ho, hj] : { std::tuple{ "image", &ours.hUp, &ref.hUp },
                                            std::tuple{ "alias", &ours.hDown, &ref.hDown } })
        {
            const std::vector<double> ro = rejection(*ho, F), rj = rejection(*hj, F);
            const auto to18k = [](const std::vector<double>& v) {     // tones() runs from 20 Hz in 10 Hz steps
                return std::vector<double>(v.begin(), v.begin() + (18000 - 20) / 10 + 1);
            };
            const double o18 = minOf(to18k(ro)), j18 = minOf(to18k(rj));
            std::printf("NOTE     proc.osref %s: %s rejection, worst tone to 20 kHz at 44.1 kHz: fcdsp %.2f dB, JUCE "
                        "%.2f dB; to 18 kHz: fcdsp %.2f dB, JUCE %.2f dB\n", name, what, minOf(ro), minOf(rj), o18, j18);
            P.ge(k + what + ".margin_db", minOf(ro) - minOf(rj), 0.0);
            P.ge(k + what + ".margin_18k_db", o18 - j18, 0.0);
        }
    }
} // namespace

FCMP_PROBE(proc, osref)
{
    compare(P, Quality::std);
    compare(P, Quality::hq);
    return P.finish();
}
