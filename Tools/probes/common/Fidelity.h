// Tools/probes/common/Fidelity.h: fidelity spec rows, judged or noted by the Mode's `provisional` flag (SPRINTS §7 D12;
// 01 §4.3 `provisional`; K3 #9).
//
// A provisional Mode runs generic traits (the S2 walking-skeleton Clean, every descriptor-wave Mode until its Mode task
// installs the real policies), so its FIDELITY rows (curve error against the declared curve, measured ratio, threshold,
// textbook formula, time constants, link law, THD) cannot be expected to pass yet. Fidelity prints them as NOTE lines
// with their verdict instead of failing the run; STRUCTURAL rows (finite output, GR >= 0, tap against audio,
// resolver and registry rules, block-size and bit-exactness invariants) stay blocking and go straight to Probe. Once
// the Mode task clears `provisional`, the same calls become Probe spec rows, unchanged.
//
//   fcmp::probe::Fidelity F(P, desc.provisional);
//   F.near("static.r4.t-30.k6.err_outside_db", err, 0.0, tol.curveOutsideKneeDb);
//
// Keys obey the spec-row rules either way (^[a-z0-9][a-z0-9._:+-]{0,119}$, unique per run; a violation is a harness
// error) and --only selects them, so turning a Mode non-provisional can never surface a latent key error. A provisional
// run ends with summary(): one NOTE with the pass/miss counts.
#pragma once

#include <funkgui/test/Harness.h>

#include <cmath>
#include <cstdio>
#include <set>
#include <string>
#include <string_view>

namespace fcmp::probe
{
    class Fidelity
    {
    public:
        Fidelity(funkgui::test::Probe& probe, bool provisional) : P_(probe), provisional_(provisional) {}

        bool provisional() const noexcept { return provisional_; }
        int passed() const noexcept { return pass_; }       // provisional rows that would have passed
        int missed() const noexcept { return miss_; }       // provisional rows that would have failed

        bool near(std::string_view key, double got, double want, double absTol, double relTol = 0.0)
        {
            if (!provisional_)
                return P_.near(key, got, want, absTol, relTol);
            const double tol = std::fmax(absTol, relTol * std::fabs(want));
            return note(key, std::fabs(got - want) <= tol,
                        "got " + num(got) + "  want " + num(want) + " +/- " + num(tol));
        }

        bool le(std::string_view key, double got, double bound)
        {
            if (!provisional_)
                return P_.le(key, got, bound);
            return note(key, got <= bound, "got " + num(got) + "  <= " + num(bound));
        }

        bool ge(std::string_view key, double got, double bound)
        {
            if (!provisional_)
                return P_.ge(key, got, bound);
            return note(key, got >= bound, "got " + num(got) + "  >= " + num(bound));
        }

        bool in(std::string_view key, double got, double lo, double hi)
        {
            if (!provisional_)
                return P_.in(key, got, lo, hi);
            return note(key, lo <= got && got <= hi, "got " + num(got) + "  in [" + num(lo) + ", " + num(hi) + "]");
        }

        // One NOTE with the counts (provisional runs only).
        void summary() const
        {
            if (provisional_)
                std::printf("NOTE     fidelity: provisional Mode, %d fidelity row(s) printed, not judged: "
                            "%d pass, %d miss (SPRINTS §7 D12)\n",
                            pass_ + miss_, pass_, miss_);
        }

    private:
        static std::string num(double v)
        {
            char b[40];
            std::snprintf(b, sizeof b, "%.9g", v);
            return b;
        }

        static bool validKey(std::string_view k) noexcept
        {
            if (k.empty() || k.size() > 120)
                return false;
            const auto alnum = [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); };
            if (!alnum(k[0]))
                return false;
            for (const char c : k)
                if (!alnum(c) && c != '.' && c != '_' && c != ':' && c != '+' && c != '-')
                    return false;
            return true;
        }

        bool note(std::string_view key, bool ok, const std::string& what)
        {
            if (!validKey(key))
            {
                P_.harnessError("invalid fidelity key '" + std::string(key) + "'");
                return false;
            }
            if (!keys_.insert(std::string(key)).second)
            {
                P_.harnessError("duplicate fidelity key '" + std::string(key) + "' in this run");
                return false;
            }
            if (!P_.wants(key))
                return true;
            ok ? ++pass_ : ++miss_;
            std::printf("NOTE     fidelity %s  %.*s  %s  (provisional: not judged)\n", ok ? "PASS" : "MISS",
                        static_cast<int>(key.size()), key.data(), what.c_str());
            return ok;
        }

        funkgui::test::Probe& P_;
        bool provisional_;
        std::set<std::string, std::less<>> keys_;
        int pass_ = 0, miss_ = 0;
    };
} // namespace fcmp::probe
