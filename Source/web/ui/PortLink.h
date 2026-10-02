// Source/web/ui/PortLink.h: the EngineLink over a MessagePort (web Sprint D, ADR-93): how the editor module's WebFacade
// (web/facade/WebFacade.h) reaches the engine module in the AudioWorklet. The page makes the AudioWorkletNode and hands
// its port to the module; from then on every WebProtocol record crosses as one ArrayBuffer, copied out of this module's
// memory on the way out and into it on the way back. JavaScript moves the bytes and reads none of them.
//
// What the page sees (docs/sprints/web-d.md, "The seam between the module and the page"):
//   Module.fcmpPort.connect(port, sampleRate, maxBlock)   the worklet's port and what its engine was configured with.
//                                 A port already connected is disconnected first. The port's onmessage is the link's
//                                 from here on: a page that wants the port's other messages uses addEventListener.
//   Module.fcmpPort.disconnect()  no port any more (the node was closed). Nothing on the port itself is changed.
// Both are there from the constructor to the destructor; afterwards they do nothing.
//
// The wire rule, this side of it:
//   post()   never blocks and never throws. A Pull travels in the carrier, an ArrayBuffer of sizeof(Reply) = 16,704
//            bytes with the record in its first 16: the worklet writes the reply into the same buffer and sends it
//            back, and the buffer that came back carries the next Pull, so a running page allocates one carrier and
//            no more (a new one is made only when none is at hand: the first Pull, and a Pull the facade repeats after
//            its patience while the last one is still out). Every other record travels in a buffer of exactly its
//            size. Both are transferred, and what is posted is always a buffer of its own, never a view on the wasm
//            heap: a view would be cloned with the whole memory under it. To choose between the two the link reads
//            Header::kind, and nothing else of a record.
//   replies  arrive in a message task of the page's main thread, never inside a facade call. A message whose data is
//            an ArrayBuffer is one record: its bytes (at most sizeof(Reply)) are copied into the inbox, a fixed block
//            of this link's own, and given to the sink, valid for that call. Anything else on the port is private to
//            the page and the worklet and is not looked at.
//   no port  before connect() and after disconnect() a record is dropped and counted: EngineLink's "not connected"
//            (WebFacade::resync() is what the owner calls when a port arrives).
//   a replaced port   a record that still arrives from a port that is no longer the connected one (the old worklet's
//            late reply) is ignored and counted: its columns are another engine's.
//
// Events::connected runs inside connect(), with the port already usable: WebMain answers with
// WebFacade::setEngineSetup and resync(). Events::disconnected runs inside disconnect() and when connect() replaces a
// port.
//
// One PortLink per module (it owns the name Module.fcmpPort; a second one takes the name and the first then drops
// everything). Main thread only. It assumes no DOM: the same file is the node check's link (Tools/web/port,
// web/tests/port.mjs), where the ports are a node MessageChannel's.
#pragma once

#include "web/facade/EngineLink.h"

#include <cstdint>
#include <memory>
#include <span>

namespace fcmp::web
{
    class PortLink final : public EngineLink
    {
    public:
        struct Events                                    // what the page tells the module through Module.fcmpPort
        {
            virtual void connected(double sampleRate, int maxBlock) = 0;
            virtual void disconnected() = 0;

        protected:
            ~Events() = default;                         // a link never owns its events
        };

        struct Counters
        {
            std::uint32_t posted = 0;                    // records handed to a port
            std::uint32_t dropped = 0;                   // records with no port to take them (or one that refused)
            std::uint32_t delivered = 0;                 // records given to the sink
            std::uint32_t ignored = 0;                   // records from a port that had been replaced
            std::uint32_t carriers = 0;                  // carriers allocated: 1 on a page whose worklet answers
        };

        explicit PortLink(Events* events = nullptr);     // installs Module.fcmpPort
        ~PortLink() override;                            // Module.fcmpPort does nothing from here on

        PortLink(const PortLink&) = delete;
        PortLink& operator=(const PortLink&) = delete;

        void post(std::span<const std::uint8_t> record) override;
        void setSink(ReplySink* sink) override;
        void setEvents(Events* events) noexcept { events_ = events; }
        bool connected() const noexcept { return connected_; }
        const Counters& counters() const noexcept { return counters_; }

    private:
        // JavaScript's way in: function pointers given to the installer, called with this link.
        static void onRecord(void* self, int bytes);
        static void onIgnored(void* self);
        static void onConnected(void* self, double sampleRate, int maxBlock);
        static void onDisconnected(void* self);

        ReplySink* sink_ = nullptr;
        Events*    events_ = nullptr;
        bool       connected_ = false;
        Counters   counters_{};
        std::unique_ptr<std::uint8_t[]> inbox_;          // sizeof(Reply) bytes at a stable address: JS copies into it
    };
} // namespace fcmp::web
