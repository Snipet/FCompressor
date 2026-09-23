// FCMP_PROBE layer=dsp name=os scope=global timeout=180
//
// dsp.os (F6, S2; 03 §3.4; 01 §5.6; E §2.9, §5.2-5.3; K2 #11, #12, #28): fcdsp's oversampler at each Quality, and the
// ADAA-1 residual shapers of stages/colour/Adaa.h.
//
//   1. contract   the kOs table; up() returns n * factor and writes exactly that; down() consumes whole frames; ECO
//                 is the identity at latency 0.
//   2. latency    up->down is LTI at base rate, so its impulse response g says everything. Measured latency (the DC
//                 group delay, i.e. g's centroid, rounded) == kOs[q].latency; HQ: g peaks at kHqLatency and is
//                 symmetric about it. STD: |tau_g - kStdLatency| <= 0.01 samples from DC to 1 kHz at every rate from
//                 44.1 to 192 kHz (K2 #11a); tau_g at 10 kHz is reported (golden rows). A 100 Hz sine comes out as the
//                 input delayed by kOs[q].latency (residual <= -60 dB; one sample off would be -38 dB).
//   3. response   passband deviation of the round trip to 20 kHz at 44.1 kHz <= 0.01 dB (01 §5.6's mix-0 rule, ADR-17);
//                 image rejection of up() and alias rejection of down(), the worst over tones 20 Hz..20 kHz at
//                 44.1 kHz (spec floors; golden abs:0.1), from the up impulse response at the OS rate and the down
//                 path's OS-rate response rebuilt from one impulse per input phase; stepped sines through the running
//                 round trip agree with the impulse-derived response.
//   4. identity   a control Oversampler reproduces the round trip bit for bit (the D8a reference of 01 §5.6); block
//                 sizes 1, 17, 64, 1000 and reset() change nothing; channels are independent lanes (1..4 channels give
//                 the same bits per channel); down() in place.
//   5. mix in OS  down(m*g*up(x) + (1 - m)*up(x)) against (m*g + 1 - m) * down(up(x)) <= -120 dB: dry and wet share
//                 the filters, so the IIR's non-linear phase cannot comb (E §5.3); the base-rate alternative is NOTEd.
//   6. hashes     xarch.os.{std,hq}.hash: the round trip of fixed stereo program material, bit-identical across arches.
//   7. ADAA-1     per shaper (Tanh, AsymTanh, SoftClip, HardClip): constant input gives exactly f(x), silence
//                 exactly 0, NaN propagates; tick() (lanes) == process() (time) bit for bit; block splits change
//                 nothing; error against a long-double ADAA-1 reference; a small signal passes undelayed (only the
//                 residual is ADAA'd); alias power against the naive shaper at base rate (spec floor; golden
//                 abs:0.5); xarch.adaa.<shaper>.hash.
#include "ProbeRegistry.h"
#include "Signals.h"

#include "fcdsp/core/Simd.h"
#include "fcdsp/engine/Oversampler.h"
#include "fcdsp/engine/stages/colour/Adaa.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <numbers>
#include <string>
#include <vector>

namespace
{
    using funkgui::test::Probe;
    using funkgui::test::Tol;
    using fcdsp::Oversampler;
    using fcdsp::Quality;
    namespace sig = fcmp::probe::sig;
    namespace simd = fcdsp::simd;

    using Signal = std::vector<float>;
    using Chans = std::vector<Signal>;
    using cplx = std::complex<double>;

    constexpr double kPi = std::numbers::pi;
    constexpr double kFs = 44100.0;                 // the binding rate for the passband and rejection rows

    int factorOf(Quality q) { return fcdsp::kOs[static_cast<int>(q)].factor; }
    int latencyOf(Quality q) { return fcdsp::kOs[static_cast<int>(q)].latency; }
    const char* nameOf(Quality q) { return q == Quality::eco ? "eco" : q == Quality::std ? "std" : "hq"; }
    std::string key(Quality q, const char* rest) { return std::string("os.") + nameOf(q) + "." + rest; }
    std::size_t sz(int n) { return static_cast<std::size_t>(n); }
    double db(double x) { return 20.0 * std::log10(x); }

    bool sameBits(const Signal& a, const Signal& b)
    {
        if (a.size() != b.size())
            return false;
        for (std::size_t i = 0; i < a.size(); ++i)
            if (std::bit_cast<std::uint32_t>(a[i]) != std::bit_cast<std::uint32_t>(b[i]))
                return false;
        return true;
    }

    // ---- driving an Oversampler ------------------------------------------------------------------------------------
    // Streams channels x samples through up() and/or down() in chunks of `chunk` base samples.
    class Rig
    {
    public:
        Rig(Quality q, int channels, int maxChunk) : ch_(channels), f_(factorOf(q))
        {
            os_.configure(q, maxChunk, channels);
            osBuf_.assign(sz(channels), Signal(sz(maxChunk * f_)));
        }

        void reset() noexcept { os_.reset(); }
        Oversampler& os() noexcept { return os_; }

        Chans roundTrip(const Chans& x, int chunk)
        {
            const int n = static_cast<int>(x[0].size());
            Chans y(sz(ch_), Signal(sz(n)));
            for (int i = 0; i < n; i += chunk)
            {
                const int m = std::min(chunk, n - i);
                std::array<const float*, 4> in{};
                std::array<float*, 4> osOut{}, out{};
                std::array<const float*, 4> osIn{};
                for (int c = 0; c < ch_; ++c)
                {
                    in[sz(c)] = x[sz(c)].data() + i;
                    osOut[sz(c)] = osBuf_[sz(c)].data();
                    osIn[sz(c)] = osBuf_[sz(c)].data();
                    out[sz(c)] = y[sz(c)].data() + i;
                }
                const int nOs = os_.up(in.data(), m, osOut.data());
                os_.down(osIn.data(), nOs, out.data());
            }
            return y;
        }

        // up() only: channels x (n * factor) OS samples.
        Chans up(const Chans& x, int chunk)
        {
            const int n = static_cast<int>(x[0].size());
            Chans y(sz(ch_), Signal(sz(n * f_)));
            for (int i = 0; i < n; i += chunk)
            {
                const int m = std::min(chunk, n - i);
                std::array<const float*, 4> in{};
                std::array<float*, 4> out{};
                for (int c = 0; c < ch_; ++c)
                {
                    in[sz(c)] = x[sz(c)].data() + i;
                    out[sz(c)] = y[sz(c)].data() + i * f_;
                }
                os_.up(in.data(), m, out.data());
            }
            return y;
        }

        // down() only: channels x nOs OS samples in, channels x nOs / factor out, `chunk` base samples per call.
        Chans down(const Chans& xOs, int chunk)
        {
            const int n = static_cast<int>(xOs[0].size()) / f_;
            Chans y(sz(ch_), Signal(sz(n)));
            for (int i = 0; i < n; i += chunk)
            {
                const int m = std::min(chunk, n - i);
                std::array<const float*, 4> in{};
                std::array<float*, 4> out{};
                for (int c = 0; c < ch_; ++c)
                {
                    in[sz(c)] = xOs[sz(c)].data() + i * f_;
                    out[sz(c)] = y[sz(c)].data() + i;
                }
                os_.down(in.data(), m * f_, out.data());
            }
            return y;
        }

    private:
        Oversampler os_;
        int ch_, f_;
        Chans osBuf_;
    };

    Signal impulse(int n, int at = 0)
    {
        Signal x(sz(n), 0.0f);
        x[sz(at)] = 1.0f;
        return x;
    }

    // Program material: seeded noise bursts over a log sweep (mono), deterministic everywhere.
    Signal program(int n, std::uint64_t seed, double fs)
    {
        Signal x(sz(n));
        sig::logSweep(x, 20.0, 20000.0, fs, 0.5);
        sig::Pcg32 rng(seed, 0x51u);
        for (int i = 0; i < n; ++i)
            if ((i / 2048) % 2 == 1)
                x[sz(i)] += 0.3f * rng.bipolar();
        return x;
    }

    // ---- frequency analysis of an impulse response (double) -------------------------------------------------------
    // H(nu) = sum h[n] e^{-j 2 pi nu n}, nu in cycles per sample; the rotation is re-seeded every 512 samples.
    cplx dtft(const Signal& h, double nu, cplx* weighted = nullptr)
    {
        const cplx w = std::polar(1.0, -2.0 * kPi * nu);
        cplx z(1.0, 0.0), acc(0.0, 0.0), accN(0.0, 0.0);
        for (std::size_t n = 0; n < h.size(); ++n)
        {
            const double v = static_cast<double>(h[n]);
            if (v != 0.0)
            {
                acc += v * z;
                accN += (static_cast<double>(n) * v) * z;
            }
            z *= w;
            if ((n & 511u) == 511u)
                z = std::polar(1.0, -2.0 * kPi * std::fmod(nu * static_cast<double>(n + 1), 1.0));
        }
        if (weighted != nullptr)
            *weighted = accN;
        return acc;
    }

    // Group delay in samples: Re(sum n h e / sum h e).
    double groupDelay(const Signal& h, double nu)
    {
        cplx num;
        const cplx den = dtft(h, nu, &num);
        return (num / den).real();
    }

    // Tones 20 Hz..20 kHz in 10 Hz steps at kFs.
    std::vector<double> tones()
    {
        std::vector<double> t;
        for (int f = 20; f <= 20000; f += 10)
            t.push_back(static_cast<double>(f));
        return t;
    }

    // The frequencies (Hz, below the OS Nyquist F*fs/2) that fold onto the tone f after decimation by F, or that the
    // zero-stuffing of up() images f to: k*fs +- f for k = 1..F-1.
    std::vector<double> mirrors(double f, double fs, int F)
    {
        std::vector<double> m;
        for (int k = 1; k < F; ++k)
            for (const double g : { k * fs - f, k * fs + f })
                if (g > 0.0 && g < 0.5 * F * fs)
                    m.push_back(g);
        return m;
    }

    // The worst (smallest) rejection in dB over the tones: |H(f)| / max |H(mirror)|, H at the OS rate.
    double worstRejection(const Signal& hOs, int F, double fs, double* atHz)
    {
        double worst = std::numeric_limits<double>::infinity();
        for (const double f : tones())
        {
            const double pass = std::abs(dtft(hOs, f / (F * fs)));
            double leak = 0.0;
            for (const double g : mirrors(f, fs, F))
                leak = std::max(leak, std::abs(dtft(hOs, g / (F * fs))));
            const double r = db(pass / leak);
            if (r < worst)
            {
                worst = r;
                *atHz = f;
            }
        }
        return worst;
    }

    // The down path as an OS-rate filter: y[n] = sum_m h[F n - m] x[m], so an impulse at OS phase p gives h[F n - p].
    Signal downResponse(Quality q, int nBase)
    {
        const int F = factorOf(q);
        Signal h(sz(nBase * F), 0.0f);
        for (int p = 0; p < F; ++p)
        {
            Rig rig(q, 1, 256);
            const Chans y = rig.down({ impulse(nBase * F, p) }, 256);
            for (int n = 0; n < nBase; ++n)
                if (const int j = F * n - p; j >= 0)
                    h[sz(j)] = y[0][sz(n)];
        }
        return h;
    }

    // Single-bin DFT of x[from, from + len) at hz (len * hz / fs an integer).
    cplx bin(const Signal& x, int from, int len, double hz, double fs)
    {
        cplx acc(0.0, 0.0);
        for (int i = 0; i < len; ++i)
        {
            const double ph = sig::sinePhase(i, hz, fs);
            acc += static_cast<double>(x[sz(from + i)]) * cplx(sig::cosTurns(ph), -sig::sinTurns(ph));
        }
        return acc;
    }

    // ==== 1. contract ================================================================================================
    void contractProbe(Probe& P)
    {
        P.eq("os.kos.eco.factor", fcdsp::kOs[0].factor, 1);
        P.eq("os.kos.eco.latency", fcdsp::kOs[0].latency, 0);
        P.eq("os.kos.eco.dup", fcdsp::kOs[0].dUp, 0);
        P.eq("os.kos.std.factor", fcdsp::kOs[1].factor, 2);
        P.eq("os.kos.std.latency", fcdsp::kOs[1].latency, fcdsp::kStdLatency);
        P.eq("os.kos.std.dup", fcdsp::kOs[1].dUp, fcdsp::kStdUpDelay);
        P.eq("os.kos.hq.factor", fcdsp::kOs[2].factor, 4);
        P.eq("os.kos.hq.latency", fcdsp::kOs[2].latency, fcdsp::kHqLatency);
        P.eq("os.kos.hq.dup", fcdsp::kOs[2].dUp, fcdsp::kHqUpDelay);
        P.le("os.kos.std.latency_target", fcdsp::kStdLatency, 4);
        P.le("os.kos.hq.latency_target", fcdsp::kHqLatency, 64);

        for (const Quality q : { Quality::eco, Quality::std, Quality::hq })
        {
            const int F = factorOf(q);
            // up() returns n * factor and writes exactly n * factor samples (a sentinel after them survives)
            int bad = 0;
            for (const int n : { 0, 1, 7, 64 })
            {
                Oversampler os;
                os.configure(q, 64, 2);
                Signal in0(64, 0.25f), in1(64, -0.5f);
                Signal o0(sz(64 * F + 1), 7.0f), o1(sz(64 * F + 1), 7.0f);
                const float* in[2] = { in0.data(), in1.data() };
                float* out[2] = { o0.data(), o1.data() };
                if (os.up(in, n, out) != n * F || o0[sz(n * F)] != 7.0f || o1[sz(n * F)] != 7.0f)
                    ++bad;
                // down() of nOs = n*F + (F - 1) consumes n frames and writes n samples
                Signal d0(65, 9.0f), d1(65, 9.0f);
                float* dout[2] = { d0.data(), d1.data() };
                const float* din[2] = { o0.data(), o1.data() };
                os.down(din, n * F + (F - 1), dout);
                if (d0[sz(n)] != 9.0f || d1[sz(n)] != 9.0f || (n > 0 && d0[sz(n - 1)] == 9.0f))
                    ++bad;
            }
            P.eq(key(q, "contract.io_errors"), bad, 0);
        }

        // ECO is the identity (in and out of place), at latency 0
        {
            const Signal x = program(4096, 11, 48000.0);
            Rig rig(Quality::eco, 1, 64);
            P.eq("os.eco.identity.bitexact", sameBits(rig.roundTrip({ x }, 64)[0], x) ? 1 : 0, 1);
            Signal y = x;
            float* io[1] = { y.data() };
            const float* ioc[1] = { y.data() };
            Oversampler os;
            os.configure(Quality::eco, 4096, 1);
            os.down(ioc, os.up(ioc, 4096, io), io);
            P.eq("os.eco.inplace.bitexact", sameBits(y, x) ? 1 : 0, 1);
        }
        // A default-constructed Oversampler is ECO with 0 channels: returns n, writes nothing
        {
            Oversampler os;
            Signal in0(8, 1.0f), o0(8, 3.0f);
            const float* in[1] = { in0.data() };
            float* out[1] = { o0.data() };
            P.eq("os.unconfigured.up", os.up(in, 8, out) * 10 + (o0[0] == 3.0f ? 1 : 0), 81);
        }
    }

    // ==== 2 + 3. latency and response ================================================================================
    struct Response
    {
        Signal g;            // round trip, base rate
        Signal hUp;          // up(), OS rate
        Signal hDown;        // down(), OS rate
    };

    // Without its trailing zeros (FTZ ends the IIR tails; the FIRs are finite).
    Signal trimmed(Signal h)
    {
        while (h.size() > 1 && h.back() == 0.0f)
            h.pop_back();
        return h;
    }

    Response responseOf(Quality q)
    {
        constexpr int kN = 4096;                    // the IIRs decay below 1e-40 within ~2000 base samples
        Response r;
        {
            Rig rig(q, 1, 256);
            r.g = trimmed(rig.roundTrip({ impulse(kN) }, 256)[0]);
        }
        {
            Rig rig(q, 1, 256);
            r.hUp = trimmed(rig.up({ impulse(kN) }, 256)[0]);
        }
        r.hDown = trimmed(downResponse(q, kN));
        std::printf("NOTE     dsp.os %s: impulse responses end at %zu (round trip), %zu (up, OS rate), %zu (down, "
                    "OS rate)\n", nameOf(q), r.g.size(), r.hUp.size(), r.hDown.size());
        return r;
    }

    void latencyProbe(Probe& P, Quality q, const Response& r)
    {
        const int L = latencyOf(q);
        const int dUp = fcdsp::kOs[static_cast<int>(q)].dUp;
        const double tau0 = groupDelay(r.g, 0.0), tauUp = groupDelay(r.hUp, 0.0) / factorOf(q);
        P.eq(key(q, "latency.measured"), std::lround(tau0), L);
        std::printf("NOTE     dsp.os %s: DC group delay %.6f samples (declared %d); up stage %.4f (dUp %d)\n",
                    nameOf(q), tau0, L, tauUp, dUp);
        P.le(key(q, "dup.misalignment"), std::fabs(tauUp - dUp), 0.5);

        std::size_t peak = 0;
        for (std::size_t i = 1; i < r.g.size(); ++i)
            if (std::fabs(r.g[i]) > std::fabs(r.g[peak]))
                peak = i;
        if (q == Quality::std)          // for the later latency probes (D7): the IIR's peak is not its LF group delay
            std::printf("NOTE     dsp.os std: the round trip's impulse response peaks at sample %zu (%.4f); latency is "
                        "its group delay, not its peak\n", peak, static_cast<double>(r.g[peak]));
        if (q == Quality::hq)
        {
            // linear phase: the peak at L, symmetric about it
            P.eq("os.hq.latency.peak", static_cast<std::int64_t>(peak), L);
            double asym = 0.0;
            for (int j = 1; j <= L; ++j)
                asym = std::max(asym, std::fabs(static_cast<double>(r.g[sz(L + j)]) - r.g[sz(L - j)]));
            P.le("os.hq.latency.asymmetry", asym / std::fabs(static_cast<double>(r.g[sz(L)])), 1e-6);
        }

        // |tau_g - L| from DC to 1 kHz at every supported rate
        double worst = 0.0, worstFs = 0.0, worstHz = 0.0;
        for (const double fs : { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 })
            for (int f = 0; f <= 1000; f += 10)
            {
                const double e = std::fabs(groupDelay(r.g, f / fs) - L);
                if (e > worst)
                {
                    worst = e;
                    worstFs = fs;
                    worstHz = f;
                }
            }
        std::printf("NOTE     dsp.os %s: max |tau_g - %d| to 1 kHz = %.6f samples (at %g Hz, fs %g)\n", nameOf(q), L,
                    worst, worstHz, worstFs);
        P.le(key(q, "gd.max_err_to_1k"), worst, q == Quality::std ? 0.01 : 1e-4);

        // tau_g at 10 kHz, reported (golden)
        const double gd441 = groupDelay(r.g, 10000.0 / 44100.0), gd48 = groupDelay(r.g, 10000.0 / 48000.0);
        std::printf("NOTE     dsp.os %s: tau_g at 10 kHz = %.4f samples at 44.1 kHz, %.4f at 48 kHz\n", nameOf(q),
                    gd441, gd48);
        P.num(key(q, "gd.at_10k.fs44k1"), gd441, Tol::abs(0.01));
        P.num(key(q, "gd.at_10k.fs48k"), gd48, Tol::abs(0.01));

        // time domain: a 100 Hz sine comes out delayed by L
        {
            const int n = 48000;
            Signal x(sz(n));
            sig::sine(x, 100.0, 48000.0, 0.5);
            Rig rig(q, 1, 64);
            const Signal y = rig.roundTrip({ x }, 64)[0];
            double err = 0.0;
            for (int i = 4096; i < n; ++i)
                err = std::max(err, std::fabs(static_cast<double>(y[sz(i)]) - x[sz(i - L)]));
            P.le(key(q, "align.lf100.residual_db"), db(err / 0.5), -60.0);
        }
    }

    void responseProbe(Probe& P, Quality q, const Response& r)
    {
        const int F = factorOf(q);
        // passband of the round trip to 20 kHz at 44.1 kHz (01 §5.6: <= 0.01 dB)
        double dev = 0.0;
        for (const double f : tones())
            dev = std::max(dev, std::fabs(db(std::abs(dtft(r.g, f / kFs)))));
        dev = std::max(dev, std::fabs(db(std::abs(dtft(r.g, 0.0)))));
        std::printf("NOTE     dsp.os %s: passband deviation to 20 kHz at 44.1 kHz %.3g dB\n", nameOf(q), dev);
        P.le(key(q, "passband.dev_db"), dev, 0.01);
        P.num(key(q, "passband.dev_db"), dev, Tol::abs(0.1));

        double atUp = 0.0, atDown = 0.0;
        const double img = worstRejection(r.hUp, F, kFs, &atUp);
        const double ali = worstRejection(r.hDown, F, kFs, &atDown);
        std::printf("NOTE     dsp.os %s: image rejection %.2f dB (worst at %g Hz), alias rejection %.2f dB (worst at "
                    "%g Hz)\n", nameOf(q), img, atUp, ali, atDown);
        // floors a few dB under the design values (Oversampler.h): STD up 103.7 / down 90.8; HQ min(95.5, 94.3) / 79.9
        P.ge(key(q, "image_rejection_db"), img, q == Quality::std ? 100.0 : 92.0);
        P.ge(key(q, "alias_rejection_db"), ali, q == Quality::std ? 88.0 : 78.0);
        P.num(key(q, "image_rejection_db"), img, Tol::abs(0.1));
        P.num(key(q, "alias_rejection_db"), ali, Tol::abs(0.1));

        // stepped sines through the running round trip == the impulse-derived response
        {
            constexpr int kLen = 4410;              // 10 Hz bins at 44.1 kHz
            double magErr = 0.0, phErr = 0.0;
            for (const double hz : { 100.0, 1000.0, 5000.0, 10000.0, 15000.0, 19000.0, 20000.0 })
            {
                Signal x(sz(4 * kLen));
                sig::sine(x, hz, kFs, 0.5);
                Rig rig(q, 1, 64);
                const Signal y = rig.roundTrip({ x }, 64)[0];
                const cplx hSine = bin(y, 2 * kLen, kLen, hz, kFs) / bin(x, 2 * kLen, kLen, hz, kFs);
                const cplx hImp = dtft(r.g, hz / kFs);
                magErr = std::max(magErr, std::fabs(db(std::abs(hSine)) - db(std::abs(hImp))));
                phErr = std::max(phErr, std::fabs(std::arg(hSine / hImp)));
            }
            P.le(key(q, "sine_vs_impulse.mag_db"), magErr, 1e-4);
            P.le(key(q, "sine_vs_impulse.phase_rad"), phErr, 1e-4);
        }
    }

    // ==== 4. identity ================================================================================================
    void identityProbe(Probe& P, Quality q)
    {
        const Signal x = program(8192, 3, 48000.0);
        Rig ref(q, 1, 1000);
        const Signal y = ref.roundTrip({ x }, 64)[0];
        {
            Rig control(q, 1, 64);
            P.eq(key(q, "control.bitexact"), sameBits(control.roundTrip({ x }, 64)[0], y) ? 1 : 0, 1);
        }
        int blockBad = 0;
        for (const int chunk : { 1, 17, 64, 1000 })
        {
            Rig rig(q, 1, 1000);
            if (!sameBits(rig.roundTrip({ x }, chunk)[0], y))
                ++blockBad;
        }
        P.eq(key(q, "blocksize.mismatches"), blockBad, 0);
        {
            Rig rig(q, 1, 64);
            (void) rig.roundTrip({ program(3000, 99, 48000.0) }, 64);
            rig.reset();
            P.eq(key(q, "reset.bitexact"), sameBits(rig.roundTrip({ x }, 64)[0], y) ? 1 : 0, 1);
        }
        // lanes: channel c of a C-channel run == the 1-channel run of the same signal
        int laneBad = 0;
        for (int C = 1; C <= 4; ++C)
            for (int c = 0; c < C; ++c)
            {
                Chans in(sz(C));
                for (int k = 0; k < C; ++k)
                    in[sz(k)] = k == c ? x : program(8192, 50u + static_cast<std::uint64_t>(k), 48000.0);
                Rig rig(q, C, 64);
                if (!sameBits(rig.roundTrip(in, 64)[sz(c)], y))
                    ++laneBad;
            }
        P.eq(key(q, "lanes.mismatches"), laneBad, 0);
        // down() in place (out aliases osIn)
        {
            Rig upRig(q, 1, 64);
            Signal buf = upRig.up({ x }, 64)[0];
            Oversampler os;
            os.configure(q, 64, 1);
            const int F = factorOf(q);
            for (int i = 0; i < 8192; i += 64)
            {
                const float* in[1] = { buf.data() + i * F };
                float* out[1] = { buf.data() + i };         // at i = 0 the same pointer; never ahead of the reads
                os.down(in, 64 * F, out);
            }
            buf.resize(8192);
            P.eq(key(q, "down.inplace.bitexact"), sameBits(buf, y) ? 1 : 0, 1);
        }
    }

    // ==== 5. mix inside the OS domain ================================================================================
    void mixProbe(Probe& P, Quality q)
    {
        constexpr int kN = 16384;
        constexpr float kMix = 0.7f, kGain = 0.5f;
        const Signal x = program(kN, 5, 48000.0);
        const int F = factorOf(q);

        Rig rig(q, 1, 64);
        Signal y(sz(kN));
        Signal osb(sz(64 * F));
        for (int i = 0; i < kN; i += 64)
        {
            const float* in[1] = { x.data() + i };
            float* os[1] = { osb.data() };
            const int nOs = rig.os().up(in, 64, os);
            for (int k = 0; k < nOs; ++k)                           // wet = gain * dry, mixed at the OS rate
                osb[sz(k)] = kMix * (kGain * osb[sz(k)]) + (1.0f - kMix) * osb[sz(k)];
            const float* osc[1] = { osb.data() };
            float* out[1] = { y.data() + i };
            rig.os().down(osc, nOs, out);
        }
        Rig control(q, 1, 64);
        const Signal r = control.roundTrip({ x }, 64)[0];
        const double c = static_cast<double>(kMix) * kGain + (1.0 - static_cast<double>(kMix));
        double err = 0.0, errBase = 0.0, rms = 0.0;
        const int L = latencyOf(q);
        for (int i = 0; i < kN; ++i)
        {
            const double want = c * r[sz(i)];
            err = std::max(err, std::fabs(y[sz(i)] - want));
            // the alternative: the wet path through the filters, the dry path as an integer delay at base rate
            const double dry = i >= L ? static_cast<double>(x[sz(i - L)]) : 0.0;
            const double base = kMix * kGain * static_cast<double>(r[sz(i)]) + (1.0 - static_cast<double>(kMix)) * dry;
            errBase = std::max(errBase, std::fabs(base - want));
            rms += static_cast<double>(x[sz(i)]) * x[sz(i)];
        }
        rms = std::sqrt(rms / kN);
        std::printf("NOTE     dsp.os %s: mix in the OS domain nulls at %.1f dB; mixing the dry path at base rate "
                    "instead would leave %.1f dB (the filters' phase against an integer delay)\n",
                    nameOf(q), db(err / rms), db(errBase / rms));
        P.le(key(q, "mix_in_os.null_db"), db(std::max(err, 1e-30) / rms), -120.0);
    }

    // ==== 6. hashes ==================================================================================================
    void hashProbe(Probe& P, Quality q)
    {
        const Chans x = { program(24000, 21, 48000.0), program(24000, 22, 48000.0) };
        Rig rig(q, 2, 64);
        const Chans y = rig.roundTrip(x, 64);
        const Chans u = Rig(q, 2, 64).up(x, 64);
        std::uint64_t h = funkgui::test::hashFloats(y[0]);
        h = funkgui::test::fnv1a(&h, sizeof h, funkgui::test::hashFloats(y[1]));
        h = funkgui::test::fnv1a(&h, sizeof h, funkgui::test::hashFloats(u[0]));
        P.hash(std::string("xarch.os.") + nameOf(q) + ".hash", h);
    }

    // ==== 7. ADAA-1 ==================================================================================================
    // Long-double reference shapers: f and F exactly.
    struct RefTanh
    {
        long double f(long double x) const { return std::tanh(x); }
        long double F(long double x) const { return std::log(std::cosh(x)); }
    };
    struct RefAsym
    {
        long double b;
        long double f(long double x) const
        {
            const long double t = std::tanh(b);
            return (std::tanh(x + b) - t) / (1 - t * t);
        }
        long double F(long double x) const
        {
            const long double t = std::tanh(b);
            return (std::log(std::cosh(x + b)) - std::log(std::cosh(b)) - x * t) / (1 - t * t);
        }
    };
    struct RefSoft
    {
        long double f(long double x) const
        {
            return std::fabs(x) > 1.5L ? (x > 0 ? 1.0L : -1.0L) : x - 4.0L * x * x * x / 27.0L;
        }
        long double F(long double x) const
        {
            return std::fabs(x) > 1.5L ? std::fabs(x) - 0.5625L : x * x / 2 - x * x * x * x / 27.0L;
        }
    };
    struct RefHard
    {
        long double f(long double x) const { return std::clamp(x, -1.0L, 1.0L); }
        long double F(long double x) const { return std::fabs(x) > 1.0L ? std::fabs(x) - 0.5L : x * x / 2; }
    };

    // Exact ADAA-1 of the residual: x1 + mean of (f - identity) over [x0, x1] = Q + (x1 - x0)/2.
    template <class Ref>
    Signal adaaReference(const Ref& ref, const Signal& x)
    {
        Signal y(x.size());
        long double x0 = 0;
        for (std::size_t i = 0; i < x.size(); ++i)
        {
            const long double x1 = x[i], d = x1 - x0;
            const long double q = std::fabs(d) > 1e-10L ? (ref.F(x1) - ref.F(x0)) / d : ref.f(x0 + d / 2);
            y[i] = static_cast<float>(q + d / 2);
            x0 = x1;
        }
        return y;
    }

    template <class S>
    Signal adaaRun(const S& s, const Signal& x, int chunk)
    {
        Signal y = x;
        fcdsp::adaa::Channel ch;
        for (std::size_t i = 0; i < y.size(); i += sz(chunk))
            fcdsp::adaa::process(s, ch, y.data() + i, static_cast<int>(std::min(sz(chunk), y.size() - i)));
        return y;
    }

    template <class S>
    Signal naiveRun(const S& s, const Signal& x)
    {
        Signal y(x.size());
        for (std::size_t i = 0; i < x.size(); ++i)
            y[i] = fcdsp::adaa::transfer(s, x[i]);
        return y;
    }

    // Alias power (non-harmonic bins) of a shaper's output for a bin-centred sine: 10 log10(sum |X|^2) over the bins
    // that only folded harmonics reach, relative to the fundamental.
    double aliasDb(const Signal& y, int len, int fundamentalBin)
    {
        const int from = static_cast<int>(y.size()) - len;
        std::vector<char> harmonic(sz(len / 2 + 1), 0), alias(sz(len / 2 + 1), 0);
        for (int k = 1; k * fundamentalBin <= len / 2; ++k)
            harmonic[sz(k * fundamentalBin)] = 1;
        for (int k = 1; k <= 400; ++k)
        {
            int b = (k * fundamentalBin) % len;
            if (b > len / 2)
                b = len - b;
            if (b != 0 && harmonic[sz(b)] == 0)
                alias[sz(b)] = 1;
        }
        const double fs = static_cast<double>(len);                    // bins as Hz with fs = len
        double pa = 0.0;
        for (int b = 1; b <= len / 2; ++b)
            if (alias[sz(b)] != 0)
                pa += std::norm(bin(y, from, len, b, fs));
        const double p1 = std::norm(bin(y, from, len, fundamentalBin, fs));
        return 10.0 * std::log10(std::max(pa, 1e-300) / p1);
    }

    template <class S, class Ref>
    void adaaShaperProbe(Probe& P, const char* name, const S& s, const Ref& ref, double errBound, double aliasFloorDb)
    {
        namespace adaa = fcdsp::adaa;
        const std::string k = std::string("adaa.") + name + ".";

        // constant input: exactly f(c) from the second sample on; silence: exactly 0
        int staticBad = 0;
        for (int i = -400; i <= 400; ++i)
        {
            const float c = static_cast<float>(i) * 0.01f;
            const Signal y = adaaRun(s, Signal(3, c), 3);
            const float want = adaa::transfer(s, c);
            if (std::bit_cast<std::uint32_t>(y[1]) != std::bit_cast<std::uint32_t>(want) ||
                std::bit_cast<std::uint32_t>(y[2]) != std::bit_cast<std::uint32_t>(want))
                ++staticBad;
        }
        P.eq(k + "static.mismatches", staticBad, 0);
        {
            const Signal z(1000, 0.0f);
            const Signal y = adaaRun(s, z, 1000);
            P.eq(k + "silence.bitexact", sameBits(y, z) ? 1 : 0, 1);
        }
        {
            Signal x(8, 0.5f);
            x[3] = std::numeric_limits<float>::quiet_NaN();
            const Signal y = adaaRun(s, x, 8);
            P.eq(k + "nan.propagates", std::isnan(y[3]) && std::isfinite(y[2]) ? 1 : 0, 1);
        }

        // program: a driven sweep plus noise (amplitude up to ~3), and a slow ramp over the corners
        Signal x(48000);
        sig::logSweep(x, 20.0, 20000.0, 48000.0, 2.5);
        sig::Pcg32 rng(77, 3);
        for (std::size_t i = 0; i < x.size(); ++i)
            x[i] += 0.5f * rng.bipolar();
        for (int i = 0; i < 6000; ++i)                                   // steps of 1e-3 and 1e-4 across the corners:
            x.push_back(-3.0f + 1e-3f * static_cast<float>(i));         // the midpoint fallback of every kEps
        for (int i = 0; i < 30000; ++i)
            x.push_back(-1.5f + 1e-4f * static_cast<float>(i));

        // tick (lanes) == process (time), bit for bit
        {
            const Signal y = adaaRun(s, x, 4096);
            adaa::State st;
            int bad = 0;
            for (std::size_t i = 0; i < x.size(); ++i)
            {
                alignas(16) float lanes[4] = { x[i], -x[i], 0.5f * x[i], x[x.size() - 1 - i] };
                alignas(16) float out[4];
                simd::store(out, adaa::tick(s, st, simd::load(lanes)));
                if (std::bit_cast<std::uint32_t>(out[0]) != std::bit_cast<std::uint32_t>(y[i]))
                    ++bad;
            }
            P.eq(k + "tick_vs_process.mismatches", bad, 0);
            int splitBad = 0;
            for (const int chunk : { 1, 3, 5, 64, 1000 })
                if (!sameBits(adaaRun(s, x, chunk), y))
                    ++splitBad;
            P.eq(k + "blocksplit.mismatches", splitBad, 0);

            const Signal yr = adaaReference(ref, x);
            double err = 0.0;
            for (std::size_t i = 0; i < x.size(); ++i)
                err = std::max(err, std::fabs(static_cast<double>(y[i]) - yr[i]));
            std::printf("NOTE     dsp.os ADAA %s: max |err| against the long-double reference %.3g\n", name, err);
            P.le(k + "err_vs_reference", err, errBound);
            P.hash("xarch.adaa." + std::string(name) + ".hash", funkgui::test::hashFloats(y));
        }

        // a small signal passes undelayed: only the residual goes through the half-sample kernel. At 15 kHz and
        // amplitude 1e-3 a half-sample delay of the linear part would leave 9.6e-4; the residual itself is below 4e-7
        // (AsymTanh's is second order: tanh(b) x^2)
        {
            Signal xs(4800);
            sig::sine(xs, 15000.0, 48000.0, 1e-3);
            const Signal y = adaaRun(s, xs, 4800);
            double e = 0.0;
            for (std::size_t i = 1; i < xs.size(); ++i)
                e = std::max(e, std::fabs(static_cast<double>(y[i]) - xs[i]));
            P.le(k + "small_signal.max_err", e, 1e-6);
        }

        // aliasing at base rate: a bin-centred 5010 Hz sine at 48 kHz, drive 2 (the ECO case)
        {
            constexpr int kLen = 4800, kBin = 501;
            Signal xs(3 * kLen);
            sig::sine(xs, kBin, kLen, 2.0);
            const double naive = aliasDb(naiveRun(s, xs), kLen, kBin);
            const double ours = aliasDb(adaaRun(s, xs, 64), kLen, kBin);
            std::printf("NOTE     dsp.os ADAA %s: alias power %.1f dB (naive %.1f dB): %.1f dB less\n", name, ours,
                        naive, naive - ours);
            P.ge(k + "alias_reduction_db", naive - ours, aliasFloorDb);
            P.num(k + "alias_reduction_db", naive - ours, Tol::abs(0.5));
        }
    }

    void adaaProbe(Probe& P)
    {
        adaaShaperProbe(P, "tanh", fcdsp::adaa::Tanh{}, RefTanh{}, 1e-4, 3.0);
        const fcdsp::adaa::AsymTanh asym = fcdsp::adaa::AsymTanh::make(0.4f);
        adaaShaperProbe(P, "asymtanh", asym, RefAsym{ 0.4L }, 1e-4, 3.0);
        adaaShaperProbe(P, "softclip", fcdsp::adaa::SoftClip{}, RefSoft{}, 1e-4, 3.0);
        adaaShaperProbe(P, "hardclip", fcdsp::adaa::HardClip{}, RefHard{}, 5e-4, 3.0);
    }
} // namespace

FCMP_PROBE(dsp, os)
{
    contractProbe(P);
    for (const Quality q : { Quality::std, Quality::hq })
    {
        const Response r = responseOf(q);
        latencyProbe(P, q, r);
        responseProbe(P, q, r);
        identityProbe(P, q);
        mixProbe(P, q);
        hashProbe(P, q);
    }
    adaaProbe(P);
    return P.finish();
}
