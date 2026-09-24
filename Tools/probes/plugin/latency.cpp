// FCMP_PROBE layer=proc name=latency scope=mode timeout=60
//
// proc.latency.<key> (P1, S7; 03 §3.5; 01 §3.1 rules, §5.6; K2 #6, #18; ADR-15): the latency the processor reports
// against the lag its audio has, for every Quality x lookahead budget, and the K2 #6 path that changes it while audio
// runs: the setup written from a non-message thread, applied by the message-thread SetupWatcher, with the audio thread
// allocation-, lock- and syscall-free throughout.
//
// Static, <cfg> = <eco|std|hq>.la<0|5|20>.fs<44100|48000|96000>: a fresh processor at this Mode's defaults with
// quality/labudget set BEFORE prepareToPlay(fs, 512) (so prepareToPlay reads them), 2 in 2 out:
//   latency.<cfg>.reported_err         getLatencySamples() - EngineHost::latencyFor(config): 0
//   latency.<cfg>.bypass.err_samples   bypass ON: the index of a 1e-3 impulse in the output (an integer delay) minus
//                                      the reported latency: 0
//   latency.<cfg>.dry.err_samples      mix 0 (where the Mode lets mix reach 0; else a NOTE): round(centroid) minus
//                                      the reported latency: 0 (01 §5.6: STD by its LF group delay, never its peak)
//   latency.<cfg>.dry.frac_samples     |centroid - reported|: <= 0.01 (STD's Thiran section; K2 #11a)
//   latency.mode_independent           configurations where this Mode's processor reports another latency than one
//                                      at slot 0: 0 (E §5.2)
//   latency.unprepared.reported_err    before any prepareToPlay, a quality write (from another thread) is reported by
//                                      SetupWatcher's next ticks: 0
// Live (48 kHz, 128-sample blocks, 2 in 2 out; an emulated host: the audio thread takes the processor's callback lock
// and outputs silence while it is suspended, as JUCE's wrappers do, and paces itself at about real time; this thread
// is the message thread and runs JUCE's timers through Timer::callPendingTimersSynchronously every millisecond):
//   latency.live.<step>_ms             the setup written by a separate thread (quality STD -> HQ; labudget OFF ->
//                                      20 MS; both back to ECO/OFF) until getLatencySamples() equals latencyFor of
//                                      the new setup: <= 100 ms
//   latency.live.reported_err          the final latency against latencyFor: 0
//   latency.live.mode_switch_changes   latency changes while the Mode switches (from a separate thread) and the
//                                      SetupWatcher ticks for 200 ms: 0 (a Mode never changes latency)
//   latency.live.audio_allocs          operator new calls on the audio thread, counted only inside processBlock: 0
//   latency.live.audio_rt_calls        malloc/free/pthread_mutex_lock/os_unfair_lock_lock/write/mach_msg calls on the
//                                      audio thread inside processBlock: 0 (the rtsan preset's RtInterposer; other
//                                      builds print a NOTE instead: the counters are not linked)
//   latency.live.rt_self_test          (RtInterposer builds) an armed malloc/free and a std::mutex lock on this thread
//                                      are counted: >= 1 each, so the 0 above is not a dead counter
//   latency.live.nonfinite             non-finite output samples: 0
//   latency.live.blocks                processed blocks: >= 50 (the audio kept running)
#include "ProbeRegistry.h"

#include "EngineRig.h"

#include "plugin/Processor.h"

#include "fcdsp/engine/EngineHost.h"
#include "fcdsp/modes/Registry.h"
#include "fcdsp/params/Pid.h"
#include "fcdsp/params/Resolve.h"
#include "fcdsp/params/Setup.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace
{
    using fcdsp::LookaheadBudget;
    using fcdsp::Pid;
    using fcdsp::Quality;
    using funkgui::test::Probe;
    using Clock = std::chrono::steady_clock;

    constexpr int kBlock = 512;
    constexpr std::size_t kLen = 8192, kAt = 256;
    constexpr float kImpulse = 1e-3f;

    const char* nameOf(Quality q) { return q == Quality::eco ? "eco" : q == Quality::std ? "std" : "hq"; }

    void setPlain(fcmp::Processor& proc, Pid pid, float plain)
    {
        juce::RangedAudioParameter& p = proc.parameter(pid);
        p.setValueNotifyingHost(p.convertTo0to1(plain));
    }

    // A fresh processor at the Mode's defaults (one batch), 2 in 2 out, with the setup written before any prepare.
    std::unique_ptr<fcmp::Processor> makeProcessor(const fcdsp::ModeEntry& en, Quality q, LookaheadBudget b)
    {
        auto proc = std::make_unique<fcmp::Processor>();
        const fcdsp::RawParams raw = fcmp::probe::modeRaw(en, b);
        proc->beginBatch();
        setPlain(*proc, Pid::mode, static_cast<float>(fcdsp::slotOf(en)));
        for (std::size_t i = 0; i < fcdsp::kNumModeParams; ++i)
            setPlain(*proc, static_cast<Pid>(i), raw.v[i]);
        setPlain(*proc, Pid::quality, static_cast<float>(q));
        setPlain(*proc, Pid::labudget, static_cast<float>(b));
        proc->endBatch();
        return proc;
    }

    void prepare(fcmp::Processor& proc, double fs, int block)
    {
        proc.setRateAndBufferSizeDetails(fs, block);
        proc.prepareToPlay(fs, block);
    }

    // The left output for a 1e-3 impulse at kAt on both inputs, in kBlock blocks.
    std::vector<float> impulseResponse(fcmp::Processor& proc)
    {
        juce::AudioBuffer<float> buf(2, kBlock);
        juce::MidiBuffer midi;
        std::vector<float> out(kLen, 0.0f);
        for (std::size_t off = 0; off < kLen; off += kBlock)
        {
            buf.clear();
            if (kAt >= off && kAt < off + kBlock)
                for (int c = 0; c < 2; ++c)
                    buf.setSample(c, static_cast<int>(kAt - off), kImpulse);
            proc.processBlock(buf, midi);
            std::copy_n(buf.getReadPointer(0), kBlock, out.data() + off);
        }
        return out;
    }

    double centroid(const std::vector<float>& h)
    {
        double s = 0.0, sn = 0.0;
        for (std::size_t i = 0; i < h.size(); ++i)
        {
            s += static_cast<double>(h[i]);
            sn += static_cast<double>(i) * static_cast<double>(h[i]);
        }
        return sn / s - static_cast<double>(kAt);
    }

    std::int64_t peakIndex(const std::vector<float>& h)
    {
        std::size_t best = 0;
        for (std::size_t i = 0; i < h.size(); ++i)
            if (std::fabs(h[i]) > std::fabs(h[best]))
                best = i;
        return static_cast<std::int64_t>(best) - static_cast<std::int64_t>(kAt);
    }

    fcdsp::HostConfig configOf(double fs, Quality q, LookaheadBudget b)
    {
        fcdsp::HostConfig c;
        c.fs = fs;
        c.quality = q;
        c.budget = b;
        return c;
    }

    // Runs JUCE's timers on this (the message) thread until pred() holds or `limitMs` passes; the elapsed ms from
    // `since`, or -1 on a timeout.
    double pumpUntil(const std::function<bool()>& pred, Clock::time_point since, double limitMs)
    {
        for (;;)
        {
            juce::Timer::callPendingTimersSynchronously();
            const double ms = std::chrono::duration<double, std::milli>(Clock::now() - since).count();
            if (pred())
                return ms;
            if (ms > limitMs)
                return -1.0;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    // A host write from a thread that is not the message thread; returns the instant it started.
    using Writes = std::vector<std::pair<Pid, float>>;
    Clock::time_point writeFromHostThread(fcmp::Processor& proc, const Writes& writes)
    {
        Clock::time_point t0;
        std::thread host([&] {
            t0 = Clock::now();
            for (const auto& [pid, plain] : writes)
                setPlain(proc, pid, plain);
        });
        host.join();
        return t0;
    }

    // The emulated host's audio thread (JUCE's wrappers: the callback lock around each block; silence while
    // suspended). The counters are armed only around processBlock itself.
    class AudioThread
    {
    public:
        AudioThread(fcmp::Processor& proc, double fs, int block) : proc_(proc), fs_(fs), block_(block)
        {
            buf_.setSize(2, block);
            thread_ = std::thread([this] { run(); });
        }
        ~AudioThread() { stop(); }
        AudioThread(const AudioThread&) = delete;
        AudioThread& operator=(const AudioThread&) = delete;

        void stop()
        {
            stop_.store(true, std::memory_order_release);
            if (thread_.joinable())
                thread_.join();
        }
        std::int64_t blocks() const noexcept { return blocks_.load(std::memory_order_acquire); }
        std::int64_t silent() const noexcept { return silent_.load(std::memory_order_acquire); }
        std::int64_t nonfinite() const noexcept { return nonfinite_.load(std::memory_order_acquire); }

    private:
        void run()
        {
            juce::MidiBuffer midi;
            std::int64_t n0 = 0;
            const auto period = std::chrono::microseconds(static_cast<std::int64_t>(1e6 * block_ / fs_));
            auto next = Clock::now();
            while (!stop_.load(std::memory_order_acquire))
            {
                for (int c = 0; c < 2; ++c)
                    for (int i = 0; i < block_; ++i)
                        buf_.setSample(c, i, 0.25f * static_cast<float>(std::sin(
                                                 6.283185307179586 * 1000.0 * static_cast<double>(n0 + i) / fs_)));
                {
                    const juce::ScopedLock lock(proc_.getCallbackLock());
                    if (proc_.isSuspended())
                    {
                        buf_.clear();
                        silent_.fetch_add(1, std::memory_order_relaxed);
                    }
                    else
                    {
                        fcmp::probe::alloc::arm();
                        fcmp::probe::rt::arm();
                        proc_.processBlock(buf_, midi);
                        fcmp::probe::rt::disarm();
                        fcmp::probe::alloc::disarm();
                    }
                }
                std::int64_t bad = 0;
                for (int c = 0; c < 2; ++c)
                    for (int i = 0; i < block_; ++i)
                        bad += std::isfinite(buf_.getSample(c, i)) ? 0 : 1;
                nonfinite_.fetch_add(bad, std::memory_order_relaxed);
                blocks_.fetch_add(1, std::memory_order_release);
                n0 += block_;
                next += period;
                std::this_thread::sleep_until(next);
            }
        }

        fcmp::Processor& proc_;
        double fs_;
        int block_;
        juce::AudioBuffer<float> buf_;
        std::atomic<bool> stop_{ false };
        std::atomic<std::int64_t> blocks_{ 0 }, silent_{ 0 }, nonfinite_{ 0 };
        std::thread thread_;
    };
} // namespace

FCMP_PROBE(proc, latency)
{
    const juce::ScopedJuceInitialiser_GUI juceInit;              // this thread is the message thread
    const fcdsp::ModeEntry& en = fcmp::probe::modeEntry(C.key);
    const fcdsp::ModeEntry& first = *fcdsp::modeSlots().front().entry;

    // ---- static: every Quality x budget x rate ----------------------------------------------------------------------
    std::int64_t dependent = 0;
    bool mixNote = false;
    for (const double fs : { 44100.0, 48000.0, 96000.0 })
        for (const Quality q : { Quality::eco, Quality::std, Quality::hq })
            for (const LookaheadBudget b : { LookaheadBudget::off, LookaheadBudget::ms5, LookaheadBudget::ms20 })
            {
                const std::string k = std::string("latency.") + nameOf(q) + ".la"
                                    + std::to_string(static_cast<int>(fcdsp::budgetMs(b))) + ".fs"
                                    + std::to_string(static_cast<int>(fs));
                const int want = fcdsp::EngineHost::latencyFor(configOf(fs, q, b));

                auto byp = makeProcessor(en, q, b);
                setPlain(*byp, Pid::bypass, 1.0f);
                prepare(*byp, fs, kBlock);
                const int reported = byp->getLatencySamples();
                P.eq(k + ".reported_err", reported - want, 0);
                P.eq(k + ".bypass.err_samples", peakIndex(impulseResponse(*byp)) - reported, 0);

                auto dry = makeProcessor(en, q, b);
                setPlain(*dry, Pid::mix, 0.0f);
                fcdsp::ParamView view;
                fcdsp::resolveView(*en.desc, dry->currentRaw(), view);
                if (view[Pid::mix].plain == 0.0f)
                {
                    prepare(*dry, fs, kBlock);
                    const double cd = centroid(impulseResponse(*dry));
                    P.eq(k + ".dry.err_samples", std::llround(cd) - reported, 0);
                    P.le(k + ".dry.frac_samples", std::fabs(cd - static_cast<double>(reported)), 0.01);
                }
                else if (!mixNote)
                {
                    std::printf("NOTE     latency: %s's mix resolves to %.3f at raw 0 (not a mix-0 Mode): the dry rows "
                                "are skipped; bypass measures the latency-aligned path\n",
                                std::string(C.key).c_str(), static_cast<double>(view[Pid::mix].plain));
                    mixNote = true;
                }

                auto zero = makeProcessor(first, q, b);
                prepare(*zero, fs, kBlock);
                dependent += zero->getLatencySamples() == reported ? 0 : 1;
            }
    P.eq("latency.mode_independent", dependent, 0);

    // ---- before any prepareToPlay: SetupWatcher reports a setup change too -----------------------------------------
    {
        auto proc = makeProcessor(en, Quality::std, LookaheadBudget::off);
        const Clock::time_point t0 = writeFromHostThread(*proc, { { Pid::quality, 2.0f } });
        const int want = fcdsp::EngineHost::latencyFor(configOf(48000.0, Quality::hq, LookaheadBudget::off));
        pumpUntil([&] { return proc->getLatencySamples() == want; }, t0, 500.0);
        P.eq("latency.unprepared.reported_err", proc->getLatencySamples() - want, 0);
    }

    // ---- live: setup changes from a host thread while the audio thread runs ------------------------------------------
    {
        constexpr double kFs = 48000.0;
        constexpr int kLiveBlock = 128;
        const bool rtCounters = fcmp::probe::rt::available();     // resolves the interposer before any arming
        if (rtCounters)
        {
            // Positive control: the interposer sees an armed malloc/free and a mutex lock, so a 0 below is real.
            std::mutex m;
            fcmp::probe::rt::reset();
            fcmp::probe::rt::arm();
            void* volatile block = std::malloc(64);
            std::free(block);
            m.lock();
            m.unlock();
            fcmp::probe::rt::disarm();
            const fcmp::probe::rt::Counts c = fcmp::probe::rt::counts();
            P.ge("latency.live.rt_self_test", static_cast<double>(std::min(c.mallocs, c.mutexLocks)), 1.0);
        }
        auto proc = makeProcessor(en, Quality::std, LookaheadBudget::off);
        prepare(*proc, kFs, kLiveBlock);
        fcmp::probe::alloc::reset();
        fcmp::probe::rt::reset();
        AudioThread audio(*proc, kFs, kLiveBlock);
        const auto idle = [&](double ms) { pumpUntil([] { return false; }, Clock::now(), ms); };
        idle(60.0);                                              // the message loop runs all along, as in a host

        struct Step
        {
            const char* name;
            Writes writes;
            Quality q;
            LookaheadBudget b;
        };
        const Step steps[] = {
            { "quality", { { Pid::quality, 2.0f } }, Quality::hq, LookaheadBudget::off },
            { "budget", { { Pid::labudget, 2.0f } }, Quality::hq, LookaheadBudget::ms20 },
            { "back", { { Pid::quality, 0.0f }, { Pid::labudget, 0.0f } }, Quality::eco, LookaheadBudget::off },
        };
        int want = 0;
        for (const Step& s : steps)
        {
            want = fcdsp::EngineHost::latencyFor(configOf(kFs, s.q, s.b));
            const Clock::time_point t0 = writeFromHostThread(*proc, s.writes);
            double ms = pumpUntil([&] { return proc->getLatencySamples() == want; }, t0, 1000.0);
            if (ms < 0.0)
                ms = 1e9;
            P.le(std::string("latency.live.") + s.name + "_ms", ms, 100.0);
            std::printf("NOTE     latency.live.%s: %d samples after %.1f ms\n", s.name, want, ms);
            idle(37.0);                                          // the next write lands at another timer phase
        }
        P.eq("latency.live.reported_err", proc->getLatencySamples() - want, 0);

        std::int64_t changes = 0;
        int last = proc->getLatencySamples();
        for (const fcdsp::ModeSlot& s : fcdsp::modeSlots())
        {
            const Clock::time_point t0 = writeFromHostThread(*proc, { { Pid::mode, static_cast<float>(s.slot) } });
            pumpUntil([&] {
                const int now = proc->getLatencySamples();
                changes += now != last ? 1 : 0;
                last = now;
                return false;
            }, t0, 25.0);
        }
        idle(60.0);
        P.eq("latency.live.mode_switch_changes", changes, 0);

        audio.stop();
        P.eq("latency.live.audio_allocs", static_cast<std::int64_t>(fcmp::probe::alloc::allocations()), 0);
        if (rtCounters)
        {
            const fcmp::probe::rt::Counts c = fcmp::probe::rt::counts();
            std::printf("NOTE     latency.live.rt: malloc %llu free %llu mutex %llu unfair %llu write %llu mach_msg %llu\n",
                        static_cast<unsigned long long>(c.mallocs), static_cast<unsigned long long>(c.frees),
                        static_cast<unsigned long long>(c.mutexLocks), static_cast<unsigned long long>(c.unfairLocks),
                        static_cast<unsigned long long>(c.writes), static_cast<unsigned long long>(c.machMsgs));
            P.eq("latency.live.audio_rt_calls", static_cast<std::int64_t>(c.total()), 0);
        }
        else
            std::printf("NOTE     latency.live.audio_rt_calls: no RtInterposer in this build (the rtsan preset links it); "
                        "operator new is still counted\n");
        P.eq("latency.live.nonfinite", audio.nonfinite(), 0);
        P.ge("latency.live.blocks", static_cast<double>(audio.blocks()), 50.0);
        std::printf("NOTE     latency.live: %lld blocks, %lld silent (suspended), %u reconfigures, %u Mode announcements\n",
                    static_cast<long long>(audio.blocks()), static_cast<long long>(audio.silent()),
                    proc->setupWatcher().reconfigures(), proc->setupWatcher().announcements());
    }
    return P.finish();
}
