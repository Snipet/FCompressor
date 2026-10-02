// Source/web/ui/PortLink.cpp: see PortLink.h. Three JavaScript functions and the C++ they call back into through
// function pointers (the wasm table), so the module needs no export list.
//
// EM_JS bodies are C string literals to the preprocessor: no trailing semicolon after the macro (-Wextra-semi), no
// apostrophe in a comment, no backslash.
#include "web/ui/PortLink.h"

#include "web/engine/WebProtocol.h"

#include <emscripten/em_js.h>
#include <emscripten/em_macros.h>

#include <cstddef>
#include <cstring>

using FcmpPortRecordFn = void (*)(void*, int);
using FcmpPortIgnoredFn = void (*)(void*);
using FcmpPortConnectedFn = void (*)(void*, double, int);
using FcmpPortDisconnectedFn = void (*)(void*);

EM_JS_DEPS(fcmp_port_deps, "$getWasmTableEntry")

// Module.fcmpPort: connect and disconnect for the page, and the link's own state beside them (link: the PortLink, 0
// once it is gone; port: the connected MessagePort; carrier: the reply-sized buffer waiting for the next Pull).
// A handler stays on the port it was set on and knows that port: when it is no longer the connected one, a record it
// receives is another engine's. A record is told by its class name, not by instanceof: a port made in another realm (a
// frame) delivers that realm's ArrayBuffer.
EM_JS(void, fcmp_port_install,
      (void* link, unsigned char* inbox, int capacity, FcmpPortRecordFn onRecord, FcmpPortIgnoredFn onIgnored,
       FcmpPortConnectedFn onConnected, FcmpPortDisconnectedFn onDisconnected),
{
    const record = getWasmTableEntry(onRecord);
    const ignored = getWasmTableEntry(onIgnored);
    const connected = getWasmTableEntry(onConnected);
    const disconnected = getWasmTableEntry(onDisconnected);
    const state = { link: link, port: null, carrier: null, capacity: capacity };
    const isBuffer = (data) => Object.prototype.toString.call(data) === "[object ArrayBuffer]";
    state.disconnect = () => {
        if (!state.port) return;
        state.port = null;
        state.carrier = null;
        if (state.link) disconnected(state.link);
    };
    state.connect = (port, sampleRate, maxBlock) => {
        if (!state.link) return;
        state.disconnect();
        state.port = port;
        port.onmessage = (event) => {
            const data = event.data;
            if (!isBuffer(data) || !state.link) return;
            if (state.port !== port) {
                ignored(state.link);
                return;
            }
            const n = Math.min(data.byteLength, capacity);
            HEAPU8.set(new Uint8Array(data, 0, n), inbox);
            if (data.byteLength === capacity && !state.carrier) state.carrier = data;
            record(state.link, n);
        };
        connected(state.link, +sampleRate, maxBlock | 0);
    };
    Module['fcmpPort'] = state;
})

EM_JS(void, fcmp_port_remove, (void* link), {
    const state = Module['fcmpPort'];
    if (!state || state.link !== link) return;
    state.link = 0;
    state.port = null;
    state.carrier = null;
})

// 0: nothing was posted (no port, another link owns the name, or the port refused). 1: posted. 2: posted in a carrier
// made for it.
EM_JS(int, fcmp_port_post, (void* link, const unsigned char* bytes, int n, int inCarrier), {
    const state = Module['fcmpPort'];
    if (!state || state.link !== link || !state.port) return 0;
    let buffer = null;
    let made = 0;
    if (inCarrier) {
        buffer = state.carrier;
        state.carrier = null;
        if (!buffer) {
            buffer = new ArrayBuffer(state.capacity);
            made = 1;
        }
        new Uint8Array(buffer, 0, n).set(HEAPU8.subarray(bytes, bytes + n));
    } else {
        buffer = HEAPU8.slice(bytes, bytes + n).buffer;
    }
    try {
        state.port.postMessage(buffer, [buffer]);
    } catch (error) {
        return 0;
    }
    return 1 + made;
})

namespace fcmp::web
{
    namespace
    {
        constexpr int kInboxBytes = static_cast<int>(sizeof(Reply));     // the carrier's size: the largest reply
        constexpr int kPostedInNewCarrier = 2;                           // fcmp_port_post
    }

    PortLink::PortLink(Events* events) : events_(events), inbox_(std::make_unique<std::uint8_t[]>(sizeof(Reply)))
    {
        fcmp_port_install(this, inbox_.get(), kInboxBytes, &PortLink::onRecord, &PortLink::onIgnored,
                          &PortLink::onConnected, &PortLink::onDisconnected);
    }

    PortLink::~PortLink() { fcmp_port_remove(this); }

    void PortLink::setSink(ReplySink* sink) { sink_ = sink; }

    void PortLink::post(std::span<const std::uint8_t> record)
    {
        // The one field a link reads: a Pull travels in the carrier, the buffer its reply comes back in. The size
        // alone would not do: a Reset is 16 bytes too.
        std::uint16_t kind = 0;
        if (record.size() >= sizeof(Header))
            std::memcpy(&kind, record.data() + offsetof(Header, kind), sizeof kind);
        const bool pull = kind == static_cast<std::uint16_t>(Kind::pull) && record.size() == sizeof(PullMsg);
        const int sent = connected_ && !record.empty()
                             ? fcmp_port_post(this, record.data(), static_cast<int>(record.size()), pull ? 1 : 0)
                             : 0;
        if (sent == 0)
        {
            ++counters_.dropped;
            return;
        }
        ++counters_.posted;
        if (sent == kPostedInNewCarrier)
            ++counters_.carriers;
    }

    void PortLink::onRecord(void* self, int bytes)
    {
        auto& link = *static_cast<PortLink*>(self);
        if (link.sink_ == nullptr || bytes <= 0)
            return;
        ++link.counters_.delivered;
        link.sink_->reply({ link.inbox_.get(), static_cast<std::size_t>(bytes) });
    }

    void PortLink::onIgnored(void* self) { ++static_cast<PortLink*>(self)->counters_.ignored; }

    void PortLink::onConnected(void* self, double sampleRate, int maxBlock)
    {
        auto& link = *static_cast<PortLink*>(self);
        link.connected_ = true;
        if (link.events_ != nullptr)
            link.events_->connected(sampleRate, maxBlock);
    }

    void PortLink::onDisconnected(void* self)
    {
        auto& link = *static_cast<PortLink*>(self);
        link.connected_ = false;
        if (link.events_ != nullptr)
            link.events_->disconnected();
    }
} // namespace fcmp::web
