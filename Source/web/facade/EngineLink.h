// Source/web/facade/EngineLink.h: how the web facade reaches the engine module (ADR-93). The editor and the engine are
// two wasm modules on two threads; everything between them is a WebProtocol record (web/engine/WebProtocol.h), and a
// link only carries records: it knows no parameter and reads nothing of a record but its header (the kind, to choose a
// Pull's carrier; the byte count, to copy it). In the browser Source/web/ui/PortLink implements it over the worklet's
// MessagePort, moving bytes from one module's memory to the other's; in the native probes
// Tools/probes/plugin/LoopbackLink hands them to an engine in the same process. Nothing here knows either.
//
// The contract, both ways:
//   post(record)   one whole record, to the engine. Never blocks and has no result: a record the engine refuses, and a
//                  link that is not connected yet, look the same from here (WebFacade::resync() is for the second).
//                  The bytes are read before post() returns; the caller keeps them.
//   reply(record)  one whole record from the engine, given to the sink the facade set. Replies arrive on the facade's
//                  thread and in the engine's order, at most one for each Pull posted: inside that post() (a loopback),
//                  later (a port), or never (a worklet that went away). The bytes are valid only during the call, at
//                  any alignment: the sink copies what it keeps and reads nothing afterwards.
// One thread: the facade's (the page's main thread; a probe's own).
//
// Portable C++ (lint web.facade): no JUCE, no Emscripten header, nothing of the engine's but its protocol.
#pragma once

#include <cstdint>
#include <span>

namespace fcmp::web
{
    struct ReplySink
    {
        virtual void reply(std::span<const std::uint8_t> record) = 0;

    protected:
        ~ReplySink() = default;                          // a link never owns its sink
    };

    class EngineLink
    {
    public:
        virtual ~EngineLink() = default;
        virtual void post(std::span<const std::uint8_t> record) = 0;
        virtual void setSink(ReplySink* sink) = 0;       // nullptr: replies are dropped
    };
} // namespace fcmp::web
