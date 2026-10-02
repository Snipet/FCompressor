// Tools/web/port/portcheck.cpp: PortLink and a WebFacade as a node module (web Sprint D, ADR-93), for
// web/tests/port.mjs (web.ui.port). cmake/FcmpWeb.cmake builds fcmp-port-check.mjs from this directory with
// Source/web/ui/PortLink.cpp, the facade and the portable model code: the link is the browser module's own file, and
// the script plays the page (it connects the port) and the worklet (an engine behind the other end of the channel).
//
// main() makes the pair, wired as WebMain wires it (connected: setEngineSetup, then resync), and gives the script
//   Module.fcmpCheck.attach(on)       WebFacade::setUiAttached: what a host does as it comes and goes
//   Module.fcmpCheck.pull()           WebFacade::pull: the frame loop's call
//   Module.fcmpCheck.set(pid, v01)    the editor's write: port(pid).setValue01
//   Module.fcmpCheck.resetEngine()    WebFacade::resetEngine
//   Module.fcmpCheck.status()         a JSON text: the link's counters, the events it called, what the facade knows of
//                                     the engine, and the three parameter indices the script writes; after destroy(),
//                                     what was touched since (below); either way the second link's part
//   Module.fcmpCheck.destroy()        the facade, then the link: Module.fcmpPort does nothing afterwards
//   Module.fcmpCheck.second()         a second PortLink, bare, with a sink that counts: it takes the name
//                                     Module.fcmpPort and the first link is displaced
// beside PortLink's own Module.fcmpPort. JavaScript reaches all of it through function pointers, as it reaches
// PortLink (no export list, no EMSCRIPTEN_KEEPALIVE).
//
// What a destroyed link must not be: reachable. A call into it traps nowhere in wasm (a freed or a null `this` is an
// address like any other), so destroy() makes one visible instead, three ways, and status() counts the bytes that
// changed afterwards (`touched`):
//   link    the pair lives in static storage and is destroyed in place, and the storage is then filled with a pattern:
//           a callback with the dangling pointer counts in it (and, through the pattern as its sink, traps)
//   inbox   the link's inbox is freed; blocks of its size are allocated at once and filled with the pattern, until one
//           lies where the inbox was (`watching` says one does): a record copied to the old address lands in it
//   null    the first bytes of the memory, where a callback with link 0 would write its counter
//
// Web only: PortLink.cpp cannot compile natively, so this directory is outside the flat Tools/web glob.
#include "web/engine/WebProtocol.h"
#include "web/facade/EngineLink.h"
#include "web/facade/WebFacade.h"
#include "web/ui/PortLink.h"

#include "plugin/ProcessorFacade.h"

#include "fcdsp/params/Pid.h"
#include "fcdsp/telemetry/HistoryRing.h"
#include "fcdsp/telemetry/UiFrame.h"

#include <emscripten/em_js.h>
#include <emscripten/em_macros.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <span>

using FcmpCheckVoidFn = void (*)(void);
using FcmpCheckFlagFn = void (*)(int);
using FcmpCheckSetFn = void (*)(int, float);
using FcmpCheckTextFn = const char* (*)(void);

EM_JS_DEPS(fcmp_check_deps, "$UTF8ToString,$getWasmTableEntry")

EM_JS(void, fcmp_check_install,
      (FcmpCheckFlagFn attach, FcmpCheckVoidFn pull, FcmpCheckSetFn set, FcmpCheckVoidFn resetEngine,
       FcmpCheckTextFn status, FcmpCheckVoidFn destroy, FcmpCheckVoidFn second),
{
    const statusText = getWasmTableEntry(status);
    Module['fcmpCheck'] = {
        attach: getWasmTableEntry(attach),
        pull: getWasmTableEntry(pull),
        set: getWasmTableEntry(set),
        resetEngine: getWasmTableEntry(resetEngine),
        status: () => JSON.parse(UTF8ToString(statusText())),
        destroy: getWasmTableEntry(destroy),
        second: getWasmTableEntry(second)
    };
})

// The first bytes of the memory: copied out, and later counted where they differ from the copy. JavaScript reads them:
// address 0 is no object to C++.
EM_JS(void, fcmp_check_low_copy, (unsigned char* out, int bytes), {
    HEAPU8.copyWithin(out, 0, bytes);
})

EM_JS(int, fcmp_check_low_changed, (const unsigned char* was, int bytes), {
    let changed = 0;
    for (let i = 0; i < bytes; i += 1) if (HEAPU8[i] !== HEAPU8[was + i]) changed += 1;
    return changed;
})

namespace
{
    // The facade's link: the PortLink behind a pass-through that notes where a reply lay. That is the link's inbox,
    // whose address nothing else tells: destroy() watches that place once the block is freed.
    struct Tap final : fcmp::web::EngineLink, fcmp::web::ReplySink
    {
        explicit Tap(fcmp::web::EngineLink& to) : link(to) {}

        void post(std::span<const std::uint8_t> record) override { link.post(record); }
        void setSink(fcmp::web::ReplySink* to) override
        {
            sink = to;
            link.setSink(to != nullptr ? this : nullptr);
        }
        void reply(std::span<const std::uint8_t> record) override
        {
            inbox = record.data();
            if (sink != nullptr)
                sink->reply(record);
        }

        fcmp::web::EngineLink& link;
        fcmp::web::ReplySink*  sink = nullptr;
        const std::uint8_t*    inbox = nullptr;          // null until a record was delivered
    };

    struct Wiring final : fcmp::web::PortLink::Events
    {
        fcmp::web::PortLink  link{ this };
        Tap                  tap{ link };
        fcmp::web::WebFacade facade{ tap };
        int    connects = 0, disconnects = 0;
        double eventRate = 0.0;                          // the last connected()'s arguments
        int    eventBlock = 0;

        void connected(double sampleRate, int maxBlock) override
        {
            ++connects;
            eventRate = sampleRate;
            eventBlock = maxBlock;
            facade.setEngineSetup(sampleRate, maxBlock);
            facade.resync();
        }
        void disconnected() override { ++disconnects; }
    };

    // The second link of the displaced rows: no facade, a sink that counts what it is given.
    struct Second final : fcmp::web::PortLink::Events, fcmp::web::ReplySink
    {
        Second() { link.setSink(this); }

        void connected(double, int) override { ++connects; }
        void disconnected() override {}
        void reply(std::span<const std::uint8_t>) override { ++records; }

        fcmp::web::PortLink link{ this };
        int connects = 0, records = 0;
    };

    constexpr std::size_t   kInboxBytes = sizeof(fcmp::web::Reply);      // PortLink's inbox
    constexpr std::uint8_t  kPattern = 0xA5;             // as a pointer it lies outside the memory: a call traps
    constexpr int           kCanaries = 4;               // blocks tried until one lies where the inbox was
    constexpr int           kLowBytes = 256;             // more than a PortLink: where a null link's fields would be

    // The pair, in static storage and destroyed in place: its bytes stay where JavaScript knew them.
    alignas(Wiring) unsigned char storage[sizeof(Wiring)];
    Wiring* wiring = nullptr;                            // the runtime outlives main(); null after destroy()
    std::unique_ptr<Second> second;

    // What destroy() left to be watched (the header has the three ways).
    std::unique_ptr<std::uint8_t[]> canaries[kCanaries];
    bool          watching = false;                      // a canary lies where the inbox was
    unsigned char low[kLowBytes];

    char statusJson[1024];

    void attach(int on)
    {
        if (wiring != nullptr)
            wiring->facade.setUiAttached(on != 0);
    }
    void pull()
    {
        if (wiring != nullptr)
            wiring->facade.pull();
    }
    void set(int pid, float v01)
    {
        if (wiring != nullptr)
            wiring->facade.port(static_cast<fcdsp::Pid>(pid)).setValue01(v01);
    }
    void resetEngine()
    {
        if (wiring != nullptr)
            wiring->facade.resetEngine();
    }
    void makeSecond()
    {
        if (second == nullptr)
            second = std::make_unique<Second>();
    }

    void destroy()
    {
        if (wiring == nullptr)
            return;
        const auto inbox = reinterpret_cast<std::uintptr_t>(wiring->tap.inbox);
        std::destroy_at(wiring);                         // the facade, then the link, which frees its inbox
        wiring = nullptr;
        std::memset(storage, kPattern, sizeof storage);
        for (int i = 0; i < kCanaries && !watching; ++i)
        {
            canaries[i] = std::make_unique<std::uint8_t[]>(kInboxBytes);
            std::memset(canaries[i].get(), kPattern, kInboxBytes);
            const auto block = reinterpret_cast<std::uintptr_t>(canaries[i].get());
            watching = inbox != 0 && block <= inbox && inbox < block + kInboxBytes;
        }
        fcmp_check_low_copy(low, kLowBytes);
    }

    int changed(const unsigned char* bytes, std::size_t count)
    {
        int n = 0;
        for (std::size_t i = 0; i < count; ++i)
            n += bytes[i] != kPattern ? 1 : 0;
        return n;
    }

    const char* status()
    {
        int at = 0;
        if (wiring == nullptr)
        {
            int inbox = 0;
            for (const auto& canary : canaries)
                if (canary != nullptr)
                    inbox += changed(canary.get(), kInboxBytes);
            at = std::snprintf(statusJson, sizeof statusJson,
                               "{\"alive\":0,\"watching\":%d,\"touched\":{\"link\":%d,\"inbox\":%d,\"null\":%d}",
                               watching ? 1 : 0, changed(storage, sizeof storage), inbox,
                               fcmp_check_low_changed(low, kLowBytes));
        }
        else
        {
            const fcmp::web::PortLink::Counters& c = wiring->link.counters();
            const fcmp::web::WebFacade& f = wiring->facade;
            const fcmp::Diagnostics d = f.diagnostics();
            fcdsp::UiFrame frame;
            f.readUiFrame(frame);
            at = std::snprintf(
                statusJson, sizeof statusJson,
                "{\"alive\":1,\"posted\":%u,\"dropped\":%u,\"delivered\":%u,\"ignored\":%u,\"carriers\":%u,"
                "\"connected\":%d,\"connects\":%d,\"disconnects\":%d,\"eventRate\":%.0f,\"eventBlock\":%d,"
                "\"replies\":%u,\"refused\":%u,\"flags\":%u,\"latency\":%d,\"prepared\":%d,\"rate\":%.0f,"
                "\"block\":%d,\"quality\":%d,\"budget\":%d,\"attached\":%d,\"written\":%.0f,\"publish\":%u,"
                "\"pid\":{\"thr\":%d,\"quality\":%d,\"labudget\":%d}",
                c.posted, c.dropped, c.delivered, c.ignored, c.carriers, wiring->link.connected() ? 1 : 0,
                wiring->connects, wiring->disconnects, wiring->eventRate, wiring->eventBlock, f.replies(),
                f.repliesRefused(), f.replyFlags(), d.latencySamples, d.prepared ? 1 : 0, d.sampleRate, d.maxBlock,
                d.quality, d.budget, f.attachCount(), static_cast<double>(f.history().written()), frame.publishCount,
                static_cast<int>(fcdsp::Pid::thr), static_cast<int>(fcdsp::Pid::quality),
                static_cast<int>(fcdsp::Pid::labudget));
        }
        std::snprintf(statusJson + at, sizeof statusJson - static_cast<std::size_t>(at),
                      ",\"second\":{\"alive\":%d,\"connected\":%d,\"connects\":%d,\"records\":%d}}",
                      second != nullptr ? 1 : 0, second != nullptr && second->link.connected() ? 1 : 0,
                      second != nullptr ? second->connects : 0, second != nullptr ? second->records : 0);
        return statusJson;
    }
}

int main()
{
    wiring = ::new (static_cast<void*>(storage)) Wiring;
    fcmp_check_install(&attach, &pull, &set, &resetEngine, &status, &destroy, &makeSecond);
    return 0;
}
