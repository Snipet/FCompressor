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
//                                     the engine, and the three parameter indices the script writes
//   Module.fcmpCheck.destroy()        the facade, then the link: Module.fcmpPort does nothing afterwards
// beside PortLink's own Module.fcmpPort. JavaScript reaches all of it through function pointers, as it reaches
// PortLink (no export list, no EMSCRIPTEN_KEEPALIVE).
//
// Web only: PortLink.cpp cannot compile natively, so this directory is outside the flat Tools/web glob.
#include "web/facade/WebFacade.h"
#include "web/ui/PortLink.h"

#include "plugin/ProcessorFacade.h"

#include "fcdsp/params/Pid.h"
#include "fcdsp/telemetry/HistoryRing.h"
#include "fcdsp/telemetry/UiFrame.h"

#include <emscripten/em_js.h>
#include <emscripten/em_macros.h>

#include <cstdio>
#include <memory>

using FcmpCheckVoidFn = void (*)(void);
using FcmpCheckFlagFn = void (*)(int);
using FcmpCheckSetFn = void (*)(int, float);
using FcmpCheckTextFn = const char* (*)(void);

EM_JS_DEPS(fcmp_check_deps, "$UTF8ToString,$getWasmTableEntry")

EM_JS(void, fcmp_check_install,
      (FcmpCheckFlagFn attach, FcmpCheckVoidFn pull, FcmpCheckSetFn set, FcmpCheckVoidFn resetEngine,
       FcmpCheckTextFn status, FcmpCheckVoidFn destroy),
{
    const statusText = getWasmTableEntry(status);
    Module['fcmpCheck'] = {
        attach: getWasmTableEntry(attach),
        pull: getWasmTableEntry(pull),
        set: getWasmTableEntry(set),
        resetEngine: getWasmTableEntry(resetEngine),
        status: () => JSON.parse(UTF8ToString(statusText())),
        destroy: getWasmTableEntry(destroy)
    };
})

namespace
{
    struct Wiring final : fcmp::web::PortLink::Events
    {
        fcmp::web::PortLink  link{ this };
        fcmp::web::WebFacade facade{ link };
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

    std::unique_ptr<Wiring> wiring;                      // the runtime outlives main(); null after destroy()
    char statusJson[768];

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
    void destroy() { wiring.reset(); }

    const char* status()
    {
        if (wiring == nullptr)
        {
            std::snprintf(statusJson, sizeof statusJson, "{\"alive\":0}");
            return statusJson;
        }
        const fcmp::web::PortLink::Counters& c = wiring->link.counters();
        const fcmp::web::WebFacade& f = wiring->facade;
        const fcmp::Diagnostics d = f.diagnostics();
        fcdsp::UiFrame frame;
        f.readUiFrame(frame);
        std::snprintf(statusJson, sizeof statusJson,
                      "{\"alive\":1,\"posted\":%u,\"dropped\":%u,\"delivered\":%u,\"ignored\":%u,\"carriers\":%u,"
                      "\"connected\":%d,\"connects\":%d,\"disconnects\":%d,\"eventRate\":%.0f,\"eventBlock\":%d,"
                      "\"replies\":%u,\"refused\":%u,\"flags\":%u,\"latency\":%d,\"prepared\":%d,\"rate\":%.0f,"
                      "\"block\":%d,\"quality\":%d,\"budget\":%d,\"attached\":%d,\"written\":%.0f,\"publish\":%u,"
                      "\"pid\":{\"thr\":%d,\"quality\":%d,\"labudget\":%d}}",
                      c.posted, c.dropped, c.delivered, c.ignored, c.carriers, wiring->link.connected() ? 1 : 0,
                      wiring->connects, wiring->disconnects, wiring->eventRate, wiring->eventBlock, f.replies(),
                      f.repliesRefused(), f.replyFlags(), d.latencySamples, d.prepared ? 1 : 0, d.sampleRate,
                      d.maxBlock, d.quality, d.budget, f.attachCount(), static_cast<double>(f.history().written()),
                      frame.publishCount, static_cast<int>(fcdsp::Pid::thr), static_cast<int>(fcdsp::Pid::quality),
                      static_cast<int>(fcdsp::Pid::labudget));
        return statusJson;
    }
}

int main()
{
    wiring = std::make_unique<Wiring>();
    fcmp_check_install(&attach, &pull, &set, &resetEngine, &status, &destroy);
    return 0;
}
