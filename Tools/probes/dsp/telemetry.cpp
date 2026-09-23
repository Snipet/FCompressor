// FCMP_PROBE layer=dsp name=telemetry scope=global timeout=120
//
// dsp.telemetry (F4, S3; 03 §3.4; 01 §6.1-6.3; K2 #8; E §7): the lock-free telemetry channels, stressed with a writer
// and a reader thread (also run under the tsan preset: the acceptance runs ctest --preset tsan -L probe:dsp.telemetry),
// and EngineHost's use of them.
//
// A. Seqlock<UiFrame>: a writer thread publishes frames whose every word is a function of publishCount, with pauses
//    of a random length; the reader makes 5 x 10^6 read() calls and checks every word of every frame it gets against
//    word 0, until 5 x 10^6 frames were read (a read that loses 8 attempts to the writer returns false: counted and
//    printed, not a read).
//      telemetry.uiframe.torn          frames whose words disagree: 0
//      telemetry.uiframe.nonmonotone   successive frames going back in publishCount: 0
//      telemetry.uiframe.distinct      distinct frames seen: >= 2 (the two threads really overlapped)
// B. HistoryRing (the claim-word protocol): a writer thread pushes columns whose every word is a function of the column
//    index, as fast as it can (it laps the 4096-column ring now and then); the reader reads up to 8 columns from where
//    its previous read ended, until 5 x 10^6 reads delivered data.
//      telemetry.history.torn         delivered columns whose words disagree with their index: 0
//      telemetry.history.order        a read that started before the requested index, or indices out of sequence: 0
//      telemetry.history.delivered    columns delivered: >= 1; laps (gaps) are printed
//    Together A and B are 10^7 reads.
// C. EngineHost columns (01 §6.3): the editor attached from the start, a 2 s program (Clean's defaults: sine and noise
//    bursts above threshold, silence), the ring drained after every block, at 48 kHz and at 44.1 kHz (columns of 44 or
//    45 samples), block sizes {1, 17, 64, 128, 512, 4096}; for every registered Mode:
//      telemetry.columns.<key>.<fs>.mismatches  columns (all 32 bytes) differing from the bs = 512 run: 0
//      telemetry.columns.<key>.<fs>.count       columns delivered == floor(n * 1000 / fs): only complete columns
//      telemetry.columns.<key>.<fs>.invalid     columns with grMin > grMax, a slot other than the Mode's, a gap bit
//                                               after the first column, or a non-finite value: 0
//      telemetry.columns.<key>.<fs>.gap_first   the first column carries b5: 1
// D. EngineHost UiFrame and the attach count (48 kHz, blocks of 256, a -6 dBFS 1 kHz sine):
//      telemetry.frame.count           publishCount == the number of process() calls while attached
//      telemetry.frame.history_written  UiFrame::historyWritten == HistoryRing::written() (low 32 bits)
//      telemetry.frame.in_peak_db      the input peak meter within 0.05 dB of -6.02 dBFS (a sine at 1 kHz)
//      telemetry.frame.count_semantics  attach twice, detach once: still publishing; detach again: publishCount stops
//      telemetry.frame.regap           the first column after a re-attach carries b5 (none is pushed while detached)
//      telemetry.frame.sane            flags carry kUiLive, sampleRate/modeSlot/latency right, every float word finite
// E. EngineHost across threads: the audio thread (this one) processes 2 s in 64-sample blocks while a reader thread
//    reads UiFrames and drains the history, toggles the attach count and requests snaps.
//      telemetry.host.nonmonotone      successive frames going back in publishCount: 0
//      telemetry.host.invalid          delivered columns failing the checks of C, or out of sequence: 0
#include "ProbeRegistry.h"

#include "EngineRig.h"
#include "Signals.h"

#include "fcdsp/core/Simd.h"
#include "fcdsp/engine/EngineHost.h"
#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/modes/Registry.h"
#include "fcdsp/params/EngineParams.h"
#include "fcdsp/params/Resolve.h"
#include "fcdsp/params/Setup.h"
#include "fcdsp/telemetry/HistoryRing.h"
#include "fcdsp/telemetry/Seqlock.h"
#include "fcdsp/telemetry/UiFrame.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{
    using namespace fcdsp;
    using funkgui::test::Probe;
    namespace sig = fcmp::probe::sig;

    constexpr int kReads = 5000000;                     // per structure: 10^7 in total

    constexpr std::uint32_t wordOf(std::uint64_t index, std::uint32_t k) noexcept
    {
        return static_cast<std::uint32_t>(index * 2654435761u) ^ (k * 0x9e3779b9u)
             ^ static_cast<std::uint32_t>(index >> 32);
    }

    // ---- A. Seqlock<UiFrame> ----------------------------------------------------------------------------------------
    struct SeqResult
    {
        std::int64_t torn = 0, nonmonotone = 0, ok = 0, failed = 0, distinct = 0;
        std::uint64_t published = 0;
    };

    SeqResult stressSeqlock()
    {
        constexpr std::uint32_t kWords = sizeof(UiFrame) / 4;
        auto lock = std::make_unique<Seqlock<UiFrame>>();
        std::atomic<bool> stop{ false };
        std::uint64_t published = 0;
        std::thread writer([&] {
            sig::Pcg32 rng(0x7365716c, 1);
            std::uint32_t w[kWords];
            for (std::uint64_t c = 1; !stop.load(std::memory_order_relaxed); ++c)
            {
                w[0] = static_cast<std::uint32_t>(c);
                for (std::uint32_t k = 1; k < kWords; ++k)
                    w[k] = wordOf(c, k);
                UiFrame f;
                std::memcpy(&f, w, sizeof f);
                lock->publish(f);
                published = c;
                // A pause of a random length (relaxed loads the compiler cannot drop): the audio thread publishes once
                // per block, so most reads find a settled frame while many still race a publish.
                const std::uint32_t pause = rng.bounded(2048);
                for (std::uint32_t spin = 0; spin < pause && !stop.load(std::memory_order_relaxed); ++spin)
                {
                }
            }
        });
        SeqResult r;
        std::uint32_t last = 0;
        while (r.ok < kReads)
        {
            UiFrame f;
            if (!lock->read(f))
            {
                ++r.failed;
                continue;
            }
            std::uint32_t w[kWords];
            std::memcpy(w, &f, sizeof w);
            if (w[0] == 0)
                continue;                               // nothing published yet: the zero-initialised frame
            ++r.ok;
            bool good = true;
            for (std::uint32_t k = 1; k < kWords; ++k)
                good = good && w[k] == wordOf(w[0], k);
            r.torn += good ? 0 : 1;
            r.nonmonotone += w[0] < last ? 1 : 0;
            r.distinct += w[0] != last ? 1 : 0;
            last = w[0];
        }
        stop.store(true, std::memory_order_relaxed);
        writer.join();
        r.published = published;
        return r;
    }

    // ---- B. HistoryRing ---------------------------------------------------------------------------------------------
    struct RingResult
    {
        std::int64_t torn = 0, order = 0, delivered = 0, gaps = 0, reads = 0;
        std::uint64_t pushed = 0;
    };

    HistoryColumn columnOf(std::uint64_t index)
    {
        std::uint32_t w[8];
        for (std::uint32_t k = 0; k < 8; ++k)
            w[k] = wordOf(index, k + 100);
        HistoryColumn c;
        std::memcpy(&c, w, sizeof c);
        return c;
    }

    RingResult stressRing()
    {
        auto ring = std::make_unique<HistoryRing>();
        std::atomic<bool> stop{ false };
        std::uint64_t pushed = 0;
        std::thread writer([&] {
            std::uint64_t i = 0;
            for (; !stop.load(std::memory_order_relaxed); ++i)
                ring->push(columnOf(i));
            pushed = i;
        });
        RingResult r;
        std::array<HistoryColumn, 8> buf{};
        std::uint64_t from = 0;
        while (r.reads < kReads)
        {
            std::uint32_t count = 0;
            const std::uint64_t first = ring->read(from, buf, count);
            r.reads += count > 0 ? 1 : 0;
            if (first < from)
                ++r.order;
            if (first > from)
                ++r.gaps;
            for (std::uint32_t k = 0; k < count; ++k)
            {
                const HistoryColumn want = columnOf(first + k);
                r.torn += std::memcmp(&want, &buf[k], sizeof want) == 0 ? 0 : 1;
            }
            r.delivered += count;
            from = first + count;
        }
        stop.store(true, std::memory_order_relaxed);
        writer.join();
        r.pushed = pushed;
        return r;
    }

    // ---- C-E. EngineHost --------------------------------------------------------------------------------------------
    HostConfig ecoConfig(double fs, int maxBlock)
    {
        HostConfig c;
        c.fs = fs;
        c.maxBlock = maxBlock;
        c.quality = Quality::eco;
        c.budget = LookaheadBudget::off;
        return c;
    }

    BlockParams defaultsOf(const ModeEntry& en)
    {
        BlockParams bp;
        bp.slot = static_cast<std::uint8_t>(slotOf(en));
        bp.eng = fcmp::probe::resolveRaw(en, fcmp::probe::modeRaw(en)).eng;
        return bp;
    }

    struct Program
    {
        std::vector<float> l, r;
    };

    Program program(double fs, double seconds)
    {
        const auto n = static_cast<std::size_t>(seconds * fs);
        Program p;
        p.l.resize(n);
        p.r.resize(n);
        sig::Pcg32 rng(0x74656c65, 5);
        for (std::size_t i = 0; i < n; ++i)
        {
            const std::size_t seg = static_cast<std::size_t>(static_cast<double>(i) * 8.0 / fs);   // 125 ms segments
            p.l[i] = seg % 4 == 3 ? 0.0f : sig::sineAt(static_cast<std::int64_t>(i), 997.0, fs, 0.7);
            p.r[i] = seg % 3 == 1 ? 0.5f * rng.bipolar() : 0.1f * p.l[i];
        }
        return p;
    }

    bool finiteColumn(const HistoryColumn& c)
    {
        return std::isfinite(c.inPeakDb) && std::isfinite(c.outPeakDb) && std::isfinite(c.detMaxDb)
            && std::isfinite(c.grMaxDb) && std::isfinite(c.grMinDb) && std::isfinite(c.tgtMaxDb)
            && std::isfinite(c.internal0);
    }

    // Column checks of C (index = the column's position in the delivered sequence).
    bool validColumn(const HistoryColumn& c, std::size_t index, int slot)
    {
        const bool gap = (c.bits & (1u << 5)) != 0;
        return finiteColumn(c) && c.grMinDb <= c.grMaxDb && static_cast<int>((c.bits >> 8) & 0xffu) == slot
            && (index == 0 || !gap);
    }

    std::vector<HistoryColumn> hostColumns(const ModeEntry& en, double fs, int bs, std::size_t& expected)
    {
        const Program in = program(fs, 2.0);
        const std::size_t n = in.l.size();
        auto host = std::make_unique<EngineHost>();
        const BlockParams bp = defaultsOf(en);
        host->configure(ecoConfig(fs, 512), bp);
        host->setUiAttached(true);
        std::vector<float> ol(n), orr(n);
        std::vector<HistoryColumn> cols;
        std::array<HistoryColumn, 64> buf{};
        std::uint64_t from = host->history().written();
        for (std::size_t off = 0; off < n;)
        {
            const std::size_t len = std::min<std::size_t>(static_cast<std::size_t>(bs), n - off);
            const float* ins[2] = { in.l.data() + off, in.r.data() + off };
            float* outs[2] = { ol.data() + off, orr.data() + off };
            ProcessIo io;
            io.in = ins;
            io.numIn = 2;
            io.out = outs;
            io.numOut = 2;
            io.n = static_cast<int>(len);
            host->process(io, bp);
            for (;;)
            {
                std::uint32_t count = 0;
                const std::uint64_t first = host->history().read(from, buf, count);
                if (count == 0)
                    break;
                if (first != from)
                    throw std::runtime_error("telemetry: the host history lapped the reader in a single-threaded run");
                cols.insert(cols.end(), buf.begin(), buf.begin() + count);
                from = first + count;
            }
            off += len;
        }
        expected = static_cast<std::size_t>(std::floor(static_cast<double>(n) * 1000.0 / fs));
        return cols;
    }

    void processBlock(EngineHost& h, const BlockParams& bp, const float* l, const float* r, float* ol, float* orr,
                      int n)
    {
        const float* ins[2] = { l, r };
        float* outs[2] = { ol, orr };
        ProcessIo io;
        io.in = ins;
        io.numIn = 2;
        io.out = outs;
        io.numOut = 2;
        io.n = n;
        h.process(io, bp);
    }

    bool frameFinite(const UiFrame& f)
    {
        float v[sizeof(UiFrame) / 4];
        std::memcpy(v, &f, sizeof v);
        // the float words: sampleRate (3), fadeProgress (5), bypassAmt (6), and everything from word 8 except the two
        // uint32 words tags (54) and discrete (55)
        bool ok = std::isfinite(v[3]) && std::isfinite(v[5]) && std::isfinite(v[6]);
        for (std::size_t k = 8; k < sizeof(UiFrame) / 4; ++k)
            if (k != 54 && k != 55)
                ok = ok && std::isfinite(v[k]);
        return ok;
    }
} // namespace

FCMP_PROBE(dsp, telemetry)
{
    static_assert(offsetof(UiFrame, tags) == 54 * 4 && offsetof(UiFrame, discrete) == 55 * 4, "frameFinite's word map");

    // ---- A ----------------------------------------------------------------------------------------------------------
    {
        const SeqResult r = stressSeqlock();
        std::printf("NOTE     telemetry.uiframe: %lld frames read, %lld reads lost to the writer (false), %lld "
                    "distinct; writer published %llu\n",
                    static_cast<long long>(r.ok), static_cast<long long>(r.failed),
                    static_cast<long long>(r.distinct), static_cast<unsigned long long>(r.published));
        P.eq("telemetry.uiframe.torn", r.torn, 0);
        P.eq("telemetry.uiframe.nonmonotone", r.nonmonotone, 0);
        P.ge("telemetry.uiframe.distinct", static_cast<double>(r.distinct), 2.0);
    }

    // ---- B ----------------------------------------------------------------------------------------------------------
    {
        const RingResult r = stressRing();
        std::printf("NOTE     telemetry.history: %lld reads with data: %lld columns delivered, %lld gaps (laps and "
                    "dropped columns); writer pushed %llu\n",
                    static_cast<long long>(r.reads), static_cast<long long>(r.delivered),
                    static_cast<long long>(r.gaps),
                    static_cast<unsigned long long>(r.pushed));
        P.eq("telemetry.history.torn", r.torn, 0);
        P.eq("telemetry.history.order", r.order, 0);
        P.ge("telemetry.history.delivered", static_cast<double>(r.delivered), 1.0);
    }

    // ---- C ----------------------------------------------------------------------------------------------------------
    for (const ModeSlot& ms : modeSlots())
    {
        const ModeEntry& en = *ms.entry;
        const std::string key(ms.key);
        for (const double fs : { 48000.0, 44100.0 })
        {
            const std::string k = "telemetry.columns." + key + (fs == 48000.0 ? ".48k" : ".44k1");
            std::size_t expected = 0;
            const std::vector<HistoryColumn> ref = hostColumns(en, fs, 512, expected);
            std::int64_t mism = 0, invalid = 0;
            for (const int bs : { 1, 17, 64, 128, 4096 })
            {
                std::size_t e2 = 0;
                const std::vector<HistoryColumn> c = hostColumns(en, fs, bs, e2);
                const std::size_t extra = c.size() > ref.size() ? c.size() - ref.size() : ref.size() - c.size();
                mism += static_cast<std::int64_t>(extra);
                for (std::size_t i = 0; i < c.size() && i < ref.size(); ++i)
                    mism += std::memcmp(&c[i], &ref[i], sizeof(HistoryColumn)) == 0 ? 0 : 1;
            }
            for (std::size_t i = 0; i < ref.size(); ++i)
                invalid += validColumn(ref[i], i, ms.slot) ? 0 : 1;
            std::printf("NOTE     %s: %zu columns; column 100: in %.3f out %.3f det %.3f gr [%.3f, %.3f] tgt %.3f "
                        "internal0 %.4g bits 0x%x\n",
                        k.c_str(), ref.size(), static_cast<double>(ref[100].inPeakDb),
                        static_cast<double>(ref[100].outPeakDb), static_cast<double>(ref[100].detMaxDb),
                        static_cast<double>(ref[100].grMinDb), static_cast<double>(ref[100].grMaxDb),
                        static_cast<double>(ref[100].tgtMaxDb), static_cast<double>(ref[100].internal0),
                        ref[100].bits);
            P.eq(k + ".mismatches", mism, 0);
            P.eq(k + ".count", static_cast<std::int64_t>(ref.size()), static_cast<std::int64_t>(expected));
            P.eq(k + ".invalid", invalid, 0);
            P.eq(k + ".gap_first", !ref.empty() && (ref[0].bits & (1u << 5)) != 0 ? 1 : 0, 1);
        }
    }

    const std::span<const ModeSlot> slots = modeSlots();
    if (slots.empty())
    {
        P.harnessError("telemetry: no registered Mode");
        return P.finish();
    }
    const ModeEntry& en = *slots.front().entry;
    const BlockParams bp = defaultsOf(en);

    // ---- D ----------------------------------------------------------------------------------------------------------
    {
        constexpr int kB = 256;
        const std::size_t n = 48000;
        std::vector<float> l(n), r(n), ol(n), orr(n);
        for (std::size_t i = 0; i < n; ++i)
            l[i] = r[i] = sig::sineAt(static_cast<std::int64_t>(i), 1000.0, 48000.0, 0.5011872336272722);
        auto host = std::make_unique<EngineHost>();
        host->configure(ecoConfig(48000.0, kB), bp);
        std::uint32_t calls = 0;
        std::size_t off = 0;
        const auto run = [&](int blocks, bool counted) {
            for (int b = 0; b < blocks && off + kB <= n; ++b, off += kB)
            {
                processBlock(*host, bp, l.data() + off, r.data() + off, ol.data() + off, orr.data() + off, kB);
                calls += counted ? 1u : 0u;
            }
        };
        run(4, false);                                  // detached: nothing published
        UiFrame f{};
        const bool before = host->readUiFrame(f) && f.publishCount != 0;
        host->setUiAttached(true);
        run(40, true);
        UiFrame a{};
        (void) host->readUiFrame(a);
        P.eq("telemetry.frame.count", before ? -1 : static_cast<std::int64_t>(a.publishCount), calls);
        const auto written = static_cast<std::uint32_t>(host->history().written());
        P.eq("telemetry.frame.history_written", a.historyWritten == written ? 1 : 0, 1);
        P.near("telemetry.frame.in_peak_db", static_cast<double>(a.inPeakDb[0]), -6.0206, 0.05);
        const bool sane = (a.flags & kUiLive) != 0 && a.sampleRate == 48000.0f && a.modeSlot == bp.slot
                       && a.fadeFromSlot == bp.slot
                       && a.latencySamples == static_cast<std::uint32_t>(host->latencySamples())
                       && a.fadeProgress == 1.0f && frameFinite(a);
        P.eq("telemetry.frame.sane", sane ? 1 : 0, 1);

        host->setUiAttached(true);                      // a second editor
        host->setUiAttached(false);                     // one closes: still attached (a count)
        run(4, true);
        UiFrame b{};
        (void) host->readUiFrame(b);
        const bool stillPublishing = b.publishCount == calls;
        host->setUiAttached(false);                     // the last one closes
        const std::uint64_t writtenAtDetach = host->history().written();
        run(20, false);
        UiFrame c{};
        (void) host->readUiFrame(c);
        const bool stopped = c.publishCount == b.publishCount && host->history().written() == writtenAtDetach;
        P.eq("telemetry.frame.count_semantics", stillPublishing && stopped ? 1 : 0, 1);

        host->setUiAttached(true);                      // re-attach: the next column carries b5
        run(8, true);
        std::array<HistoryColumn, 16> buf{};
        std::uint32_t count = 0;
        const std::uint64_t first = host->history().read(writtenAtDetach, buf, count);
        bool regap = count >= 2 && first == writtenAtDetach && (buf[0].bits & (1u << 5)) != 0;
        for (std::uint32_t i = 1; i < count; ++i)
            regap = regap && (buf[i].bits & (1u << 5)) == 0;
        P.eq("telemetry.frame.regap", regap ? 1 : 0, 1);
    }

    // ---- E ----------------------------------------------------------------------------------------------------------
    {
        const Program in = program(48000.0, 2.0);
        const std::size_t n = in.l.size();
        std::vector<float> ol(n), orr(n);
        auto host = std::make_unique<EngineHost>();
        host->configure(ecoConfig(48000.0, 64), bp);
        host->setUiAttached(true);
        std::atomic<bool> done{ false };
        std::int64_t nonmonotone = 0, invalid = 0, frames = 0, columns = 0;
        std::thread reader([&] {
            std::uint32_t last = 0;
            std::uint64_t from = host->history().written();
            std::array<HistoryColumn, 32> buf{};
            for (int i = 0; !done.load(std::memory_order_acquire); ++i)
            {
                UiFrame f{};
                if (host->readUiFrame(f))
                {
                    nonmonotone += f.publishCount < last ? 1 : 0;
                    last = f.publishCount;
                    ++frames;
                }
                std::uint32_t count = 0;
                const std::uint64_t first = host->history().read(from, buf, count);
                invalid += first < from ? 1 : 0;
                for (std::uint32_t k = 0; k < count; ++k)
                {
                    const HistoryColumn& c = buf[k];
                    const bool ok = finiteColumn(c) && c.grMinDb <= c.grMaxDb
                                 && static_cast<int>((c.bits >> 8) & 0xffu) == bp.slot;
                    invalid += ok ? 0 : 1;
                }
                columns += count;
                from = first + count;
                if (i % 5000 == 1000)                   // an editor opening and closing, a state recall
                {
                    host->setUiAttached(false);             // 1 -> 0: detached for a while
                    host->requestSnap();
                    host->setUiAttached(true);              // 0 -> 1: the next column is a gap
                }
            }
        });
        for (std::size_t off = 0; off + 64 <= n; off += 64)
            processBlock(*host, bp, in.l.data() + off, in.r.data() + off, ol.data() + off, orr.data() + off, 64);
        done.store(true, std::memory_order_release);
        reader.join();
        std::printf("NOTE     telemetry.host: the reader saw %lld frames and %lld columns\n",
                    static_cast<long long>(frames), static_cast<long long>(columns));
        P.eq("telemetry.host.nonmonotone", nonmonotone, 0);
        P.eq("telemetry.host.invalid", invalid, 0);
    }

    return P.finish();
}
