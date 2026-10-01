// Tools/web/enginecheck.cpp: fcmp_web_check's engine subcommands (ADR-93; Tools/web/WebCheck.h). They drive the
// engine module's C ABI (Source/web/engine/WebEngine.h) as the AudioWorklet does: WebProtocol records in, 128-frame
// quanta through fcmp_web_process. Built natively (the wrapper against the native goldens: a difference is the
// wrapper's) and for the web (the same rows under node: a difference is the wasm arithmetic's).
//
// FCMP_WEB_TEST name=web.engine.print timeout=600 on=all args=print,{golden}
// FCMP_WEB_TEST name=web.engine.selfcheck timeout=120 on=all args=selfcheck
// FCMP_WEB_TEST name=web.engine.tail timeout=900 on=all args=tail
// FCMP_WEB_TEST name=web.engine.speed timeout=900 on=web args=speed
//
//   print <golden>  every registered Mode's dsp.print material (Tools/probes/common/PrintProgram.h) through the C ABI
//                   at the plugin's default setup (STD, no lookahead, 48 kHz), each channel hashed as the Harness's
//                   hashFloats does, against tests/golden/base/modes/<key>/dsp.print.txt: 8 rows per Mode. A Mode
//                   with no print row at all is not blessed yet (dsp.print reports it, and --strict fails it there):
//                   a MISSING note here. A Mode with some of its rows, or an engine that could not be set up, fails.
//                   A NOTE per set gives the hash of its 22 raw values: lo, hi and mid pass through the host maps
//                   (libm), so a raw hash that differs from the native run's says the C library moved the set, not
//                   the DSP.
//   selfcheck       the module's own self-check hash against the constant below, and the ABI's contract rows (what a
//                   record changes, what is refused, any frame count, denormal input, the silence gate and records).
//   tail            per Mode, a burst then 10 s of silence with the silence gate off: no 2/3 s of the silence may cost
//                   2x the active signal's time per block (denormals: wasm cannot flush them; trivially true
//                   natively). Wall-clock time, so: the Mode is rendered once untimed first (node compiles while it
//                   runs), every 1/3 s window keeps its best time over three runs (a denormal's cost is there in
//                   every run, another process's is not), and the figure judged is the worst pair of adjacent windows
//                   (a state stuck in the denormal range stays there). Natively the limit takes FCMP_TIMING_SCALE
//                   (ADR-87) and a sanitizer build is not judged; the wasm build, the one this exists for, keeps x2.
//                   Then the gate itself, on the first Mode: it closes after the tail and opens on signal.
//   speed           the real-time factor per Mode at ECO, STD and HQ (48 kHz, 128-frame quanta, the print program);
//                   fails when the worst Mode at HQ is below 4x real time. A measurement of this machine under its
//                   current load: registered for the web only, and run alone (cmake/FcmpWeb.cmake).
#include "web/WebCheck.h"

#include "PrintProgram.h"
#include "Signals.h"
#include "Tolerances.h"

#include "web/engine/WebEngine.h"
#include "web/engine/WebProtocol.h"

#include "fcdsp/engine/EngineHost.h"
#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/modes/Registry.h"
#include "fcdsp/params/HostParams.h"
#include "fcdsp/params/Pid.h"
#include "fcdsp/params/Resolve.h"
#include "fcdsp/params/Setup.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace
{
    using namespace fcdsp;
    namespace printprog = fcmp::probe::printprog;
    namespace sig = fcmp::probe::sig;
    namespace proto = fcmp::web;

    // The C ABI self-check's hash, recorded from the native arm64 build (fcmp_web_check selfcheck prints it). Every
    // target whose arithmetic meets fcdsp's contract reproduces it. web/tests/engine.mjs holds the same constant.
    constexpr std::uint64_t kSelfCheckHash = 0x5a96ce217d29ca6full;

    constexpr double kFs = 48000.0;
    constexpr int kQuantum = 128;                       // the AudioWorklet's render quantum

    // ---- the ABI, as the worklet uses it -----------------------------------------------------------------------------
    struct Engine
    {
        FcmpWebEngine* e = fcmp_web_create();
        Engine() = default;
        ~Engine() { fcmp_web_destroy(e); }
        Engine(const Engine&) = delete;
        Engine& operator=(const Engine&) = delete;
        operator FcmpWebEngine*() const noexcept { return e; }
    };

    using Plain = std::array<float, kNumParams>;

    // The 22 Mode-filtered values and the Mode of `raw`; the other globals at the host's defaults (STD, budget off).
    Plain plainOf(const RawParams& raw)
    {
        Plain p {};
        for (std::size_t i = 0; i < kNumParams; ++i)
            p[i] = i < kNumModeParams ? raw.v[i] : kHostParams[i].def;
        p[idx(Pid::mode)] = static_cast<float>(raw.modeSlot);
        return p;
    }

    template <class Msg>
    std::int32_t post(FcmpWebEngine* e, const Msg& m)
    {
        std::uint8_t bytes[sizeof(Msg)];
        std::memcpy(bytes, &m, sizeof(Msg));
        return fcmp_web_post(e, bytes, static_cast<std::int32_t>(sizeof(Msg)));
    }

    std::int32_t postParams(FcmpWebEngine* e, const Plain& plain, bool snap)
    {
        proto::ParamsMsg m {};
        m.h = proto::header(proto::Kind::params, sizeof m);
        std::copy(plain.begin(), plain.end(), m.plain);
        m.snap = snap ? 1u : 0u;
        return post(e, m);
    }

    std::int32_t postAttach(FcmpWebEngine* e, bool attached)
    {
        return post(e, proto::AttachMsg { proto::header(proto::Kind::attach, sizeof(proto::AttachMsg)), attached ? 1u : 0u });
    }

    std::int32_t postReset(FcmpWebEngine* e)
    {
        return post(e, proto::ResetMsg { proto::header(proto::Kind::reset, sizeof(proto::ResetMsg)) });
    }

    // A Pull and the reply's fixed part; false when the engine refused or the reply is malformed.
    bool pull(FcmpWebEngine* e, proto::ReplyHead& head, std::uint32_t tag = 0)
    {
        const std::int32_t bytes = post(e, proto::PullMsg { proto::header(proto::Kind::pull, sizeof(proto::PullMsg), tag) });
        if (bytes < static_cast<std::int32_t>(sizeof head))
            return false;
        std::memcpy(static_cast<void*>(&head), fcmp_web_reply(e), sizeof head);
        return head.h.magic == proto::kMagic && head.h.version == proto::kVersion
            && head.h.kind == static_cast<std::uint16_t>(proto::Kind::reply) && head.h.tag == tag
            && head.h.bytes == static_cast<std::uint32_t>(bytes)
            && head.h.bytes == proto::replyBytes(head.columnCount);
    }

    // `in` through the engine in calls of `frames` frames (the last one shorter).
    void render(FcmpWebEngine* e, std::span<const float> inL, std::span<const float> inR, std::vector<float>& l,
                std::vector<float>& r, int frames = kQuantum)
    {
        const std::size_t n = inL.size();
        l.assign(n, 0.0f);
        r.assign(n, 0.0f);
        for (std::size_t off = 0; off < n; off += static_cast<std::size_t>(frames))
        {
            const std::size_t len = std::min<std::size_t>(static_cast<std::size_t>(frames), n - off);
            fcmp_web_process(e, inL.data() + off, inR.data() + off, l.data() + off, r.data() + off,
                             static_cast<std::int32_t>(len));
        }
    }

    // FunkGui's Harness.h hashFloats: FNV-1a 64 (offset 1469598103934665603, prime 1099511628211) over the
    // little-endian bytes of each float's bit pattern.
    std::uint64_t hashFloats(std::span<const float> v) noexcept
    {
        std::uint64_t h = 1469598103934665603ull;
        for (const float x : v)
        {
            std::uint32_t bits = 0;
            std::memcpy(&bits, &x, sizeof bits);
            for (int k = 0; k < 4; ++k)
            {
                h ^= (bits >> (k * 8)) & 0xffu;
                h *= 1099511628211ull;
            }
        }
        return h;
    }

    std::string hex(std::uint64_t h)
    {
        char buf[17];
        std::snprintf(buf, sizeof buf, "%016llx", static_cast<unsigned long long>(h));
        return buf;
    }

    // The registered Modes, slot order.
    std::vector<const ModeSlot*> modes()
    {
        std::vector<const ModeSlot*> out;
        for (const ModeSlot& ms : modeSlots())
            if (ms.entry != nullptr && ms.entry->desc != nullptr)
                out.push_back(&ms);
        return out;
    }

    struct Rows                                         // PASS / FAIL lines, as the probes print them
    {
        const char* test;
        int passed = 0, failed = 0;

        void row(bool ok, const std::string& name, const std::string& detail = {})
        {
            std::printf("%s     %s %s%s%s\n", ok ? "PASS" : "FAIL", test, name.c_str(), detail.empty() ? "" : ": ",
                        detail.c_str());
            ++(ok ? passed : failed);
        }

        int finish() const
        {
            std::printf("%s     %s: %d row(s) passed, %d failed\n", failed == 0 ? "PASS" : "FAIL", test, passed, failed);
            return failed == 0 ? 0 : 1;
        }
    };

    double secondsSince(std::chrono::steady_clock::time_point t0)
    {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    }

    // ---- print -------------------------------------------------------------------------------------------------------
    // tests/golden/base/modes/<key>/dsp.print.txt: "key<TAB>value<TAB>tolerance" lines, '#' comments.
    bool readGolden(const std::string& path, std::map<std::string, std::string>& rows)
    {
        std::FILE* f = std::fopen(path.c_str(), "r");
        if (f == nullptr)
            return false;
        char line[512];
        while (std::fgets(line, sizeof line, f) != nullptr)
        {
            if (line[0] == '#' || line[0] == '\n' || line[0] == '\r')
                continue;
            const std::string s(line);
            const std::size_t a = s.find('\t');
            if (a == std::string::npos)
                continue;
            std::size_t b = s.find_first_of("\t\r\n", a + 1);
            if (b == std::string::npos)
                b = s.size();
            rows[s.substr(0, a)] = s.substr(a + 1, b - a - 1);
        }
        std::fclose(f);
        return true;
    }
} // namespace

FCMP_WEB_COMMAND(print)
{
    if (argc != 2)
    {
        std::fprintf(stderr, "usage: fcmp_web_check print <tests/golden>\n");
        return 2;
    }
    const std::string goldenRoot = argv[1];
    const printprog::Program in = printprog::program();
    Rows rows { "web.engine.print" };
    int missing = 0, modesCompared = 0;
    std::uint64_t rawAll = 1469598103934665603ull;
    std::vector<float> l, r;
    for (const ModeSlot* ms : modes())
    {
        const std::string key(ms->key);
        std::map<std::string, std::string> golden;
        const bool haveGolden = readGolden(goldenRoot + "/base/modes/" + key + "/dsp.print.txt", golden);
        // Blessed: the Mode has print rows. Then every row this run makes must be among them.
        const bool blessed = std::any_of(golden.begin(), golden.end(),
                                         [](const auto& kv) { return kv.first.rfind("print.", 0) == 0; });
        modesCompared += blessed ? 1 : 0;
        for (const printprog::Set& set : printprog::sets(*ms->entry))
        {
            // As dsp.print configures its host: the values first, so the engine starts snapped at them.
            Engine e;
            const bool ok = e.e != nullptr && postParams(e, plainOf(set.raw), true) == 0
                         && fcmp_web_configure(e, kFs, kQuantum) >= 0;
            if (ok)
                render(e, in.l, in.r, l, r);
            const std::uint64_t rawHash = hashFloats(std::span<const float>(set.raw.v));
            rawAll = (rawAll ^ rawHash) * 1099511628211ull;
            std::printf("NOTE     %s print.%s: raw.hash %s\n", key.c_str(), set.name, hex(rawHash).c_str());
            const std::pair<const char*, const std::vector<float>*> channels[] = { { "l", &l }, { "r", &r } };
            for (const auto& [ch, out] : channels)
            {
                const std::string name = std::string("print.") + set.name + "." + ch + ".hash";
                const std::string got = ok ? hex(hashFloats(std::span<const float>(*out))) : std::string("(no engine)");
                const auto it = golden.find(name);
                if (it == golden.end())
                {
                    if (blessed || !ok)                 // a hole in a blessed Mode, or no engine: never silent
                    {
                        rows.row(false, key + " " + name, got + (blessed ? ": the Mode's golden has no such row" : ""));
                        continue;
                    }
                    // A Mode whose rows are not blessed yet: dsp.print reports them as candidates; not a failure here.
                    std::printf("MISSING  web.engine.print %s %s: %s (%s)\n", key.c_str(), name.c_str(), got.c_str(),
                                haveGolden ? "no golden row" : "no golden file");
                    ++missing;
                    continue;
                }
                rows.row(ok && got == it->second, key + " " + name,
                         got == it->second ? got : got + " golden " + it->second);
            }
        }
    }
    std::printf("NOTE     raw sets: hash of all raw hashes %s (the web run's differs from the native run's when the C "
                "library's pow or log moved a set)\n", hex(rawAll).c_str());
    std::printf("NOTE     %d row(s) compared, of %d Mode(s) with blessed print rows; %d row(s) of unblessed Modes\n",
                rows.passed + rows.failed, modesCompared, missing);
    if (rows.passed + rows.failed == 0)
    {
        std::printf("FAIL     web.engine.print: no golden row found under %s\n", goldenRoot.c_str());
        return 1;
    }
    return rows.finish();
}

// ---- selfcheck ---------------------------------------------------------------------------------------------------------
namespace
{
    // 1 s of signal for the contract rows: a 1 kHz sine at -6 dBFS plus noise at -12 dBFS peak.
    void burst(std::vector<float>& l, std::vector<float>& r, std::size_t n, std::uint64_t seed)
    {
        l.resize(n);
        r.resize(n);
        sig::Pcg32 rl(seed, 1), rr(seed, 2);
        for (std::size_t i = 0; i < n; ++i)
        {
            const float s = sig::sineAt(static_cast<std::int64_t>(i), 1000.0, kFs, 0.5);
            l[i] = s + 0.25f * rl.bipolar();
            r[i] = s + 0.25f * rr.bipolar();
        }
    }

    float fromBits(std::uint32_t bits) noexcept
    {
        float f;
        std::memcpy(&f, &bits, sizeof f);
        return f;
    }

    bool allZero(std::span<const float> v) noexcept
    {
        return std::all_of(v.begin(), v.end(), [](float x) { return x == 0.0f; });
    }

    bool gatedNow(FcmpWebEngine* e)
    {
        proto::ReplyHead head {};
        return pull(e, head) && (head.flags & proto::kReplyGated) != 0u;
    }

    // Silent quanta until a Pull says the gate is closed: the zero frames rendered before the first gated quantum, or
    // -1 when it has not closed after `limit` frames.
    long long silenceUntilGated(FcmpWebEngine* e, long long limit)
    {
        std::array<float, kQuantum> outL {}, outR {}, zeros {};
        for (long long silent = 0; silent < limit; silent += kQuantum)
        {
            fcmp_web_process(e, zeros.data(), zeros.data(), outL.data(), outR.data(), kQuantum);
            if (gatedNow(e))
                return silent;
        }
        return -1;
    }

    void silence(FcmpWebEngine* e, int quanta)
    {
        std::array<float, kQuantum> outL {}, outR {}, zeros {};
        for (int q = 0; q < quanta; ++q)
            fcmp_web_process(e, zeros.data(), zeros.data(), outL.data(), outR.data(), kQuantum);
    }

    // The gate's threshold for a Mode's defaults, as the wrapper computes it: the descriptor's tail plus the latency,
    // at least 100 ms (WebEngine.h).
    long long gateFrames(const ModeEntry& en, std::int32_t latency)
    {
        const RawParams raw = printprog::defaults(en);
        Resolution res;
        resolve(en, raw, res);
        const double tailS = static_cast<double>(en.desc->tailSeconds != nullptr ? en.desc->tailSeconds(res.eng) : 0.0f)
                           + static_cast<double>(latency) / kFs;
        return static_cast<long long>(std::max(tailS, 0.1) * kFs);
    }

    bool sameBits(const std::vector<float>& a, const std::vector<float>& b)
    {
        return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0;
    }

    // The silence gate against records (WebEngine.h): the plugin's engine runs through silence, so whatever is edited
    // there is settled when signal returns. The gated engine has to give the same audio.
    void gateRecordRows(Rows& rows, const std::vector<const ModeSlot*>& all, const std::vector<float>& inL,
                        const std::vector<float>& inR)
    {
        constexpr std::size_t kBurst = 94 * kQuantum;   // 0.25 s
        constexpr int kHoldQuanta = 75;                 // 0.2 s
        const std::span<const float> a = std::span<const float>(inL).first(kBurst), b = std::span<const float>(inR).first(kBurst);
        const std::span<const float> a2 = std::span<const float>(inL).subspan(kBurst, kBurst),
                                     b2 = std::span<const float>(inR).subspan(kBurst, kBurst);
        std::vector<float> l, r, l2, r2;

        // The longest and the shortest default tail: a Mode change between them is also the largest change of threshold.
        const ModeEntry* longest = all.front()->entry;
        const ModeEntry* shortest = all.front()->entry;
        for (const ModeSlot* ms : all)
        {
            if (gateFrames(*ms->entry, 0) > gateFrames(*longest, 0))
                longest = ms->entry;
            if (gateFrames(*ms->entry, 0) < gateFrames(*shortest, 0))
                shortest = ms->entry;
        }
        if (longest == shortest)
        {
            std::printf("NOTE     abi.gate: one Mode (or equal tails): the Mode-change rows need two\n");
            return;
        }
        const Plain x = plainOf(printprog::defaults(*longest)), y = plainOf(printprog::defaults(*shortest));

        // The reference for "the engine is in Mode y and settled": a fresh engine, `quanta` of silence, the signal.
        const auto fresh = [&](const Plain& p, int quanta, std::vector<float>& outL, std::vector<float>& outR)
        {
            Engine e;
            (void) postParams(e, p, true);
            (void) fcmp_web_configure(e, kFs, kQuantum);
            silence(e, quanta);
            render(e, a2, b2, outL, outR);
        };

        // -- a Mode change while the gate is closed: the record opens it, the engine crossfades on the silence
        {
            Engine e;
            (void) postParams(e, x, true);
            const std::int32_t latency = fcmp_web_configure(e, kFs, kQuantum);
            render(e, a, b, l, r);
            const long long closed = silenceUntilGated(e, gateFrames(*longest, latency) + 100 * kQuantum);
            const bool posted = postParams(e, y, false) == 0;
            silence(e, 1);
            rows.row(closed >= 0 && posted && !gatedNow(e), "abi.gate.record_opens",
                     "closed after " + std::to_string(closed) + " silent frames");
            silence(e, kHoldQuanta - 1);
            render(e, a2, b2, l, r);
            fresh(y, kHoldQuanta, l2, r2);
            rows.row(sameBits(l, l2) && sameBits(r, r2) && !allZero(l), "abi.gate.mode_change_while_closed");
        }

        // -- a record that shortens the tail does not close the gate before the engine has run the new values
        {
            Engine e;
            (void) postParams(e, x, true);
            const std::int32_t latency = fcmp_web_configure(e, kFs, kQuantum);
            const long long before = gateFrames(*longest, latency), after = gateFrames(*shortest, latency);
            render(e, a, b, l, r);
            // silent for longer than the new threshold, and still inside the old one
            const long long quanta = (before + after) / (2 * kQuantum);
            silence(e, static_cast<int>(quanta));
            const bool open = !gatedNow(e) && quanta * kQuantum > after + kQuantum && quanta * kQuantum < before;
            (void) postParams(e, y, false);
            const long long closed = silenceUntilGated(e, after + 100 * kQuantum);
            rows.row(open && closed > after && closed <= after + 2 * kQuantum, "abi.gate.shorter_tail_runs_first",
                     "threshold " + std::to_string(before) + " -> " + std::to_string(after) + " frames, " + std::to_string(quanta * kQuantum)
                         + " silent at the record, closed " + std::to_string(closed) + " after it");
            render(e, a2, b2, l, r);
            fresh(y, 0, l2, r2);
            rows.row(sameBits(l, l2) && sameBits(r, r2) && !allZero(l), "abi.gate.reopens_as_a_fresh_engine");
        }

        // -- values posted while closed, then Reset, then signal at once: they start settled (nothing ramps)
        {
            Plain quieter = x;
            quieter[idx(Pid::output)] = -12.0f;
            Engine e;
            (void) postParams(e, x, true);
            const std::int32_t latency = fcmp_web_configure(e, kFs, kQuantum);
            render(e, a, b, l, r);
            const long long closed = silenceUntilGated(e, gateFrames(*longest, latency) + 100 * kQuantum);
            const bool posted = postParams(e, quieter, false) == 0 && postReset(e) == 0;
            render(e, a2, b2, l, r);
            fresh(quieter, 0, l2, r2);
            rows.row(closed >= 0 && posted && sameBits(l, l2) && sameBits(r, r2) && !allZero(l), "abi.gate.reset_keeps_new_values");
        }
    }

    void contractRows(Rows& rows)
    {
        const std::vector<const ModeSlot*> all = modes();
        if (all.empty())
        {
            rows.row(false, "abi.modes", "no Mode is registered");
            return;
        }
        const ModeEntry& en = *all.front()->entry;
        const Plain defaults = plainOf(printprog::defaults(en));
        std::vector<float> inL, inR, l, r, l2, r2;
        burst(inL, inR, 48000, 0x616269);

        // -- records the engine refuses change nothing
        {
            Engine e;
            proto::ParamsMsg m {};
            m.h = proto::header(proto::Kind::params, sizeof m);
            std::copy(defaults.begin(), defaults.end(), m.plain);
            m.plain[idx(Pid::quality)] = 2.0f;          // would change the latency if it were applied
            const std::int32_t before = fcmp_web_latency(e);
            std::uint8_t bytes[sizeof m];
            const auto send = [&](const proto::ParamsMsg& msg, std::int32_t n) {
                std::memcpy(bytes, &msg, sizeof msg);
                return fcmp_web_post(e, bytes, n);
            };
            const auto full = static_cast<std::int32_t>(sizeof m);
            proto::ParamsMsg bad = m;
            bad.h.magic ^= 1u;
            rows.row(send(bad, full) == proto::kPostBadMagic, "abi.refuses.magic");
            bad = m;
            bad.h.version = static_cast<std::uint16_t>(proto::kVersion + 1);
            rows.row(send(bad, full) == proto::kPostBadVersion, "abi.refuses.version");
            bad = m;
            bad.h.kind = 77;
            rows.row(send(bad, full) == proto::kPostBadKind, "abi.refuses.kind");
            rows.row(send(m, full - 4) == proto::kPostBadSize, "abi.refuses.size");
            rows.row(send(m, 8) == proto::kPostBadArgument && fcmp_web_post(e, nullptr, full) == proto::kPostBadArgument
                         && fcmp_web_post(nullptr, bytes, full) == proto::kPostBadArgument,
                     "abi.refuses.arguments");
            rows.row(fcmp_web_latency(e) == before, "abi.refuses.unchanged");
            rows.row(fcmp_web_abi_version() == static_cast<std::int32_t>(proto::kVersion), "abi.version");
        }

        // -- setup: quality and the lookahead budget, before configure and in the message handler
        {
            Engine e;
            HostConfig cfg;                             // STD, budget off, 48 kHz: the defaults
            std::vector<float> zl(256, 0.5f), zr(256, 0.5f);
            fcmp_web_process(e, zl.data(), zl.data(), zl.data(), zr.data(), 256);
            rows.row(allZero(zl) && allZero(zr), "abi.unconfigured.silence");
            rows.row(fcmp_web_latency(e) == EngineHost::latencyFor(cfg), "abi.latency.default",
                     std::to_string(fcmp_web_latency(e)) + " samples");
            Plain p = defaults;
            p[idx(Pid::quality)] = 2.0f;
            const bool applied = postParams(e, p, true) == 0;
            cfg.quality = Quality::hq;
            const std::int32_t foretold = fcmp_web_latency(e);  // before configure: what the values will give
            const std::int32_t configured = fcmp_web_configure(e, kFs, kQuantum);
            rows.row(applied && foretold == configured && configured == EngineHost::latencyFor(cfg)
                         && fcmp_web_latency(e) == configured,
                     "abi.latency.configure_hq", std::to_string(configured) + " samples");
            p[idx(Pid::quality)] = 0.0f;
            p[idx(Pid::labudget)] = 2.0f;
            const bool reconfigured = postParams(e, p, false) == 0;
            cfg.quality = Quality::eco;
            cfg.budget = LookaheadBudget::ms20;
            proto::ReplyHead head {};
            const bool pulled = pull(e, head, 7);
            rows.row(reconfigured && fcmp_web_latency(e) == EngineHost::latencyFor(cfg) && pulled
                         && head.latencySamples == static_cast<std::uint32_t>(EngineHost::latencyFor(cfg))
                         && (head.flags & proto::kReplyConfigured) != 0u,
                     "abi.latency.reconfigure_in_handler", std::to_string(fcmp_web_latency(e)) + " samples");
        }

        // -- telemetry: nothing before Attach; then one UiFrame per call and one history column per millisecond
        {
            Engine e;
            (void) postParams(e, defaults, true);
            (void) fcmp_web_configure(e, kFs, kQuantum);
            render(e, std::span<const float>(inL).first(4800), std::span<const float>(inR).first(4800), l, r);
            proto::ReplyHead head {};
            bool ok = pull(e, head, 1);
            rows.row(ok && head.columnCount == 0u && head.frame.publishCount == 0u
                         && (head.flags & proto::kReplyAttached) == 0u,
                     "abi.telemetry.detached");
            (void) postAttach(e, true);
            (void) postAttach(e, true);                 // a repeat is not a second attach
            const int quanta = 75;                      // 9600 frames: 200 ms
            render(e, std::span<const float>(inL).subspan(4800, 9600), std::span<const float>(inR).subspan(4800, 9600), l, r);
            ok = pull(e, head, 2);
            rows.row(ok && head.frame.publishCount == static_cast<std::uint32_t>(quanta)
                         && head.frame.sampleRate == static_cast<float>(kFs)
                         && head.frame.latencySamples == head.latencySamples
                         && (head.flags & (proto::kReplyAttached | proto::kReplyFrame)) == (proto::kReplyAttached | proto::kReplyFrame),
                     "abi.telemetry.frame", "publishCount " + std::to_string(head.frame.publishCount));
            rows.row(ok && head.columnCount >= 199u && head.columnCount <= 200u && (head.flags & proto::kReplyGap) == 0u,
                     "abi.telemetry.columns", std::to_string(head.columnCount) + " columns for 200 ms");
            const std::uint32_t next = head.firstColumn + head.columnCount;
            ok = pull(e, head, 3);
            rows.row(ok && head.columnCount == 0u && head.firstColumn == next, "abi.telemetry.drained");
            (void) postAttach(e, false);
            render(e, std::span<const float>(inL).first(1280), std::span<const float>(inR).first(1280), l, r);
            ok = pull(e, head, 4);
            rows.row(ok && head.frame.publishCount == static_cast<std::uint32_t>(quanta) && head.columnCount == 0u,
                     "abi.telemetry.detach_stops");
        }

        // -- any frame count: the same stream in calls of 1000, 1 and 37 frames is the stream in 128-frame quanta
        {
            const std::span<const float> a = std::span<const float>(inL).first(24000), b = std::span<const float>(inR).first(24000);
            Engine ref;
            (void) postParams(ref, defaults, true);
            (void) fcmp_web_configure(ref, kFs, kQuantum);
            render(ref, a, b, l, r);
            for (const int frames : { 1000, 1, 37 })
            {
                Engine e;
                (void) postParams(e, defaults, true);
                (void) fcmp_web_configure(e, kFs, kQuantum);
                render(e, a, b, l2, r2, frames);
                rows.row(l == l2 && r == r2 && !allZero(l), "abi.frames." + std::to_string(frames));
            }
            // in place, and mono (inR null: inL feeds both channels)
            Engine e;
            (void) postParams(e, defaults, true);
            (void) fcmp_web_configure(e, kFs, kQuantum);
            l2.assign(a.begin(), a.end());
            r2.assign(b.begin(), b.end());
            for (std::size_t off = 0; off < l2.size(); off += kQuantum)
                fcmp_web_process(e, l2.data() + off, r2.data() + off, l2.data() + off, r2.data() + off,
                                 static_cast<std::int32_t>(std::min<std::size_t>(kQuantum, l2.size() - off)));
            rows.row(l == l2 && r == r2, "abi.frames.in_place");
        }

        // -- denormal input is read as zero: the output is the output of silence, bit for bit
        {
            std::vector<float> dl(4800), dr(4800), zl(4800, 0.0f);
            for (std::size_t i = 0; i < dl.size(); ++i)
            {
                dl[i] = fromBits(i % 2 == 0 ? 0x00011e3du : 0x80011e3du);   // +-1e-40: denormal (below FLT_MIN)
                dr[i] = fromBits(0x007fffffu - static_cast<std::uint32_t>(i));   // the largest denormals
            }
            Engine a, b;
            for (FcmpWebEngine* e : { a.e, b.e })
            {
                (void) postParams(e, defaults, true);
                (void) fcmp_web_configure(e, kFs, kQuantum);
                fcmp_web_set_gate(e, 0);
                render(e, std::span<const float>(inL).first(4800), std::span<const float>(inR).first(4800), l, r);
            }
            render(a, dl, dr, l, r);
            render(b, zl, zl, l2, r2);
            rows.row(std::memcmp(l.data(), l2.data(), l.size() * sizeof(float)) == 0
                         && std::memcmp(r.data(), r2.data(), r.size() * sizeof(float)) == 0,
                     "abi.denormal_input_is_zero");
        }

        gateRecordRows(rows, all, inL, inR);
    }
} // namespace

FCMP_WEB_COMMAND(selfcheck)
{
    Rows rows { "web.engine.selfcheck" };
    std::uint32_t halves[2] = { 0, 0 };
    const std::int32_t rc = fcmp_web_selfcheck(halves);
    const std::uint64_t got = static_cast<std::uint64_t>(halves[1]) << 32 | halves[0];
    // web/tests/engine.mjs reads this line.
    std::printf("NOTE     selfcheck hash %s constant %s\n", hex(got).c_str(), hex(kSelfCheckHash).c_str());
    rows.row(rc == 0 && got == kSelfCheckHash, "selfcheck.hash",
             rc != 0 ? std::string("the self-check failed to run") : got == kSelfCheckHash ? hex(got)
                     : hex(got) + " is not the native build's " + hex(kSelfCheckHash));
    std::uint32_t again[2] = { 0, 0 };
    rows.row(fcmp_web_selfcheck(again) == 0 && again[0] == halves[0] && again[1] == halves[1], "selfcheck.repeatable");
    rows.row(fcmp_web_selfcheck(nullptr) == -1, "selfcheck.null");
    contractRows(rows);
    return rows.finish();
}

// ---- tail --------------------------------------------------------------------------------------------------------------
namespace
{
    constexpr int kWindowQuanta = 125;                  // 1/3 s
    constexpr int kActiveWindows = 6;                   // 2 s of signal
    constexpr int kSilentWindows = 30;                  // 10 s of silence
    constexpr int kWarmSilentWindows = 3;
    constexpr int kTailRuns = 3;

#if defined(__has_feature)
  #if __has_feature(thread_sanitizer) || __has_feature(address_sanitizer)
    #define FCMP_WEB_TAIL_SANITIZED 1
  #endif
#endif

    struct TailTimes
    {
        double active = 0.0;                            // seconds per block: the lower median active window
        std::array<double, kSilentWindows> silent {};   // seconds per block, per window of the silence
    };

    // One run: the burst, then `silentWindows` of silence, timed per window. false: the engine could not be set up.
    bool tailRun(const ModeEntry& en, const std::vector<float>& inL, const std::vector<float>& inR, int silentWindows,
                 TailTimes& t, bool& stillRunning)
    {
        Engine e;
        if (e.e == nullptr || postParams(e, plainOf(printprog::defaults(en)), true) != 0
            || fcmp_web_configure(e, kFs, kQuantum) < 0)
            return false;
        fcmp_web_set_gate(e, 0);
        (void) postAttach(e, true);                     // as in the browser while the editor is open
        std::array<float, kQuantum> outL {}, outR {}, zeros {};
        std::array<double, kActiveWindows> active {};
        std::size_t off = 0;
        for (int w = 0; w < kActiveWindows; ++w)
        {
            const auto t0 = std::chrono::steady_clock::now();
            for (int q = 0; q < kWindowQuanta; ++q, off += kQuantum)
                fcmp_web_process(e, inL.data() + off, inR.data() + off, outL.data(), outR.data(), kQuantum);
            active[static_cast<std::size_t>(w)] = secondsSince(t0) / kWindowQuanta;
        }
        std::sort(active.begin(), active.end());
        t.active = active[(kActiveWindows - 1) / 2];
        for (int w = 0; w < silentWindows; ++w)
        {
            const auto t0 = std::chrono::steady_clock::now();
            for (int q = 0; q < kWindowQuanta; ++q)
                fcmp_web_process(e, zeros.data(), zeros.data(), outL.data(), outR.data(), kQuantum);
            t.silent[static_cast<std::size_t>(w)] = secondsSince(t0) / kWindowQuanta;
        }
        proto::ReplyHead head {};
        stillRunning = pull(e, head) && (head.flags & proto::kReplyGated) == 0u
                    && head.frame.publishCount == static_cast<std::uint32_t>((kActiveWindows + silentWindows) * kWindowQuanta);
        return true;
    }

    // The gate on the first Mode: closed once the tail has been rendered, zeros while closed, open on signal.
    void gateRows(Rows& rows, const ModeEntry& en, const std::vector<float>& inL, const std::vector<float>& inR)
    {
        const RawParams raw = printprog::defaults(en);
        Engine e;
        (void) postParams(e, plainOf(raw), true);
        const std::int32_t latency = fcmp_web_configure(e, kFs, kQuantum);
        Resolution res;
        resolve(en, raw, res);
        const double tailS = static_cast<double>(en.desc->tailSeconds != nullptr ? en.desc->tailSeconds(res.eng) : 0.0f)
                           + static_cast<double>(latency) / kFs;
        const auto tailFrames = static_cast<long long>(std::max(tailS, 0.1) * kFs);     // WebEngine.h: at least 100 ms

        std::array<float, kQuantum> outL {}, outR {}, zeros {};
        for (std::size_t off = 0; off + kQuantum <= 24000; off += kQuantum)
            fcmp_web_process(e, inL.data() + off, inR.data() + off, outL.data(), outR.data(), kQuantum);
        proto::ReplyHead head {};
        long long silent = 0, closedAt = -1;
        const long long limit = tailFrames + 100 * kQuantum;
        while (silent < limit)
        {
            fcmp_web_process(e, zeros.data(), zeros.data(), outL.data(), outR.data(), kQuantum);
            if (pull(e, head) && (head.flags & proto::kReplyGated) != 0u)
            {
                closedAt = silent;                      // zero frames rendered before the first gated quantum
                break;
            }
            silent += kQuantum;
        }
        rows.row(closedAt > tailFrames && closedAt <= tailFrames + 2 * kQuantum, "gate.closes_after_tail",
                 "tail " + std::to_string(tailFrames) + " frames, closed after " + std::to_string(closedAt));
        outL.fill(1.0f);
        outR.fill(1.0f);
        fcmp_web_process(e, zeros.data(), zeros.data(), outL.data(), outR.data(), kQuantum);
        rows.row(allZero(outL) && allZero(outR), "gate.closed_is_silence");
        fcmp_web_process(e, inL.data(), inR.data(), outL.data(), outR.data(), kQuantum);
        fcmp_web_process(e, inL.data() + kQuantum, inR.data() + kQuantum, outL.data(), outR.data(), kQuantum);
        rows.row(pull(e, head) && (head.flags & proto::kReplyGated) == 0u && !allZero(outL) && !allZero(outR),
                 "gate.opens_on_signal");
    }
} // namespace

FCMP_WEB_COMMAND(tail)
{
    Rows rows { "web.engine.tail" };
    std::vector<float> inL, inR;
    burst(inL, inR, static_cast<std::size_t>(kActiveWindows * kWindowQuanta * kQuantum), 0x7461696c);
    const std::vector<const ModeSlot*> all = modes();
#if defined(FCMP_WEB_TAIL_SANITIZED)
    const bool judged = false;                          // a CPU-cost ratio means nothing under a sanitizer
    const double limit = 2.0;
#elif defined(__wasm__)
    const bool judged = true;
    const double limit = 2.0;                           // the run this check exists for: never loosened
#else
    const bool judged = true;
    const double limit = 2.0 * fcmp::probe::tol::timingScale();     // ADR-87
#endif
    for (const ModeSlot* ms : all)
    {
        const std::string key(ms->key);
        TailTimes best, t;
        bool running = false;
        // Untimed first: under node the Mode's code is compiled while this runs, so the timed baseline is warm.
        bool ran = tailRun(*ms->entry, inL, inR, kWarmSilentWindows, t, running), stillRunning = true;
        for (int run = 0; run < kTailRuns && ran; ++run)
        {
            ran = tailRun(*ms->entry, inL, inR, kSilentWindows, t, running);
            if (!ran)
                break;
            stillRunning = stillRunning && running;
            if (run == 0)
                best = t;
            else
            {
                best.active = std::min(best.active, t.active);
                for (std::size_t w = 0; w < best.silent.size(); ++w)
                    best.silent[w] = std::min(best.silent[w], t.silent[w]);
            }
        }
        // The worst pair of adjacent windows: both of them slow in every run.
        double worst = 0.0;
        int worstWindow = 0;
        for (std::size_t w = 0; w + 1 < best.silent.size(); ++w)
        {
            const double pair = std::min(best.silent[w], best.silent[w + 1]);
            if (pair > worst)
            {
                worst = pair;
                worstWindow = static_cast<int>(w);
            }
        }
        const bool pass = worst < limit * best.active;
        char detail[220];
        std::snprintf(detail, sizeof detail,
                      "active %.2f us/block, silence worst %.2f us/block over 2/3 s from %.1f s (x%.2f, limit x%.3g; best of %d "
                      "runs)", best.active * 1e6, worst * 1e6, static_cast<double>(worstWindow) / 3.0,
                      best.active > 0.0 ? worst / best.active : 0.0, limit, kTailRuns);
        if (!ran)
            rows.row(false, key + " tail.cost", "the engine could not be configured");
        else if (judged)
            rows.row(pass, key + " tail.cost", detail);
        else
            std::printf("NOTE     web.engine.tail %s tail.cost: %s: not judged in a sanitizer build\n", key.c_str(), detail);
        if (ran && !stillRunning)
            rows.row(false, key + " tail.gate_off", "the engine stopped running with the gate off");
    }
    if (!all.empty())
        gateRows(rows, *all.front()->entry, inL, inR);
    return rows.finish();
}

// ---- speed -------------------------------------------------------------------------------------------------------------
FCMP_WEB_COMMAND(speed)
{
    constexpr double kGate = 4.0;                       // the worst Mode at HQ must run at least 4x real time
    const printprog::Program in = printprog::program();
    const double audioS = static_cast<double>(in.l.size()) / kFs;
    const char* const qualityName[3] = { "ECO", "STD", "HQ" };
    std::printf("NOTE     speed: real-time factor (audio time / processing time), 48 kHz, %d-frame quanta, %.0f s of the "
                "print program at each Mode's defaults, best of 3\n", kQuantum, audioS);
    std::printf("NOTE     speed: %-14s %8s %8s %8s\n", "Mode", qualityName[0], qualityName[1], qualityName[2]);
    double worstHq = 0.0;
    std::string worstKey;
    bool ran = true;
    std::array<float, kQuantum> outL {}, outR {};
    for (const ModeSlot* ms : modes())
    {
        double factor[3] = { 0.0, 0.0, 0.0 };
        for (int q = 0; q < 3; ++q)
        {
            Plain p = plainOf(printprog::defaults(*ms->entry));
            p[idx(Pid::quality)] = static_cast<float>(q);
            double bestS = 0.0;
            for (int rep = 0; rep < 3; ++rep)
            {
                Engine e;
                if (e.e == nullptr || postParams(e, p, true) != 0 || fcmp_web_configure(e, kFs, kQuantum) < 0)
                {
                    ran = false;
                    break;
                }
                const auto t0 = std::chrono::steady_clock::now();
                for (std::size_t off = 0; off + kQuantum <= in.l.size(); off += kQuantum)
                    fcmp_web_process(e, in.l.data() + off, in.r.data() + off, outL.data(), outR.data(), kQuantum);
                const double s = secondsSince(t0);
                bestS = rep == 0 ? s : std::min(bestS, s);
            }
            factor[q] = bestS > 0.0 ? audioS / bestS : 0.0;
        }
        std::printf("NOTE     speed: %-14s %7.1fx %7.1fx %7.1fx\n", std::string(ms->key).c_str(), factor[0], factor[1],
                    factor[2]);
        if (worstKey.empty() || factor[2] < worstHq)
        {
            worstHq = factor[2];
            worstKey = std::string(ms->key);
        }
    }
    Rows rows { "web.engine.speed" };
    char detail[160];
    std::snprintf(detail, sizeof detail, "worst Mode at HQ: %s, %.1fx real time (gate %.0fx)", worstKey.c_str(), worstHq,
                  kGate);
    rows.row(ran && !worstKey.empty() && worstHq >= kGate, "speed.hq_worst", detail);
    return rows.finish();
}
