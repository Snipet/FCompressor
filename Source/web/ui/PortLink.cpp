// PROTOTYPE (scout-d): Source/web/ui/PortLink.cpp
#include "web/ui/PortLink.h"

#include "web/engine/WebProtocol.h"

#include <emscripten/em_js.h>
#include <emscripten/em_macros.h>

#include <cstring>

using FcmpPortRecordFn = void (*)(void*, int);
using FcmpPortConnectedFn = void (*)(void*, double, int);
using FcmpPortDisconnectedFn = void (*)(void*);

EM_JS_DEPS(fcmp_port_deps, "$getWasmTableEntry")

EM_JS(void, fcmp_port_install,
      (void* link, unsigned char* inbox, int capacity, FcmpPortRecordFn onRecord, FcmpPortConnectedFn onConnected,
       FcmpPortDisconnectedFn onDisconnected),
{
    const record = getWasmTableEntry(onRecord);
    const connected = getWasmTableEntry(onConnected);
    const disconnected = getWasmTableEntry(onDisconnected);
    const state = { port: null, carrier: null, capacity: capacity, carriersMade: 0 };
    state.disconnect = () => {
        const port = state.port;
        if (!port) return;
        state.port = null;
        state.carrier = null;
        port.onmessage = null;
        disconnected(link);
    };
    state.connect = (port, sampleRate, maxBlock) => {
        state.disconnect();
        state.port = port;
        port.onmessage = (event) => {
            const data = event.data;
            if (state.port !== port || !(data instanceof ArrayBuffer)) return;
            const n = Math.min(data.byteLength, capacity);
            HEAPU8.set(new Uint8Array(data, 0, n), inbox);
            if (data.byteLength === capacity && !state.carrier) state.carrier = data;
            record(link, n);
        };
        connected(link, sampleRate, maxBlock);
    };
    Module['fcmpPort'] = state;
})

EM_JS(void, fcmp_port_remove, (), {
    const state = Module['fcmpPort'];
    if (!state) return;
    if (state.port) state.port.onmessage = null;
    state.port = null;
    state.carrier = null;
    Module['fcmpPort'] = null;
})

EM_JS(int, fcmp_port_post, (const unsigned char* bytes, int n, int carrier), {
    const state = Module['fcmpPort'];
    const port = state && state.port;
    if (!port) return 0;
    let buffer;
    if (carrier) {
        buffer = state.carrier;
        state.carrier = null;
        if (!buffer) {
            buffer = new ArrayBuffer(state.capacity);
            state.carriersMade += 1;
        }
        new Uint8Array(buffer, 0, n).set(HEAPU8.subarray(bytes, bytes + n));
    } else {
        buffer = HEAPU8.slice(bytes, bytes + n).buffer;
    }
    port.postMessage(buffer, [buffer]);
    return 1;
})

namespace fcmp::web
{
    namespace
    {
        constexpr int kInboxBytes = static_cast<int>(sizeof(Reply));
    }

    PortLink::PortLink(Events* events) : events_(events), inbox_(std::make_unique<std::uint8_t[]>(sizeof(Reply)))
    {
        fcmp_port_install(this, inbox_.get(), kInboxBytes, &PortLink::onRecord, &PortLink::onConnected,
                          &PortLink::onDisconnected);
    }

    PortLink::~PortLink() { fcmp_port_remove(); }

    void PortLink::setSink(ReplySink* sink) { sink_ = sink; }

    void PortLink::post(std::span<const std::uint8_t> record)
    {
        // The one field a link looks at: a Pull travels in the carrier, the buffer its reply comes back in.
        Header h{};
        if (record.size() >= sizeof(Header))
            std::memcpy(&h, record.data(), sizeof h);
        const bool pull = record.size() == sizeof(PullMsg) && h.kind == static_cast<std::uint16_t>(Kind::pull);
        if (connected_ && fcmp_port_post(record.data(), static_cast<int>(record.size()), pull ? 1 : 0) != 0)
            ++counters_.posted;
        else
            ++counters_.dropped;
    }

    void PortLink::onRecord(void* self, int bytes)
    {
        auto& link = *static_cast<PortLink*>(self);
        ++link.counters_.delivered;
        if (link.sink_ != nullptr && bytes > 0)
            link.sink_->reply({ link.inbox_.get(), static_cast<std::size_t>(bytes) });
    }

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
