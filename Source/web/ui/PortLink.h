// PROTOTYPE (scout-d): Source/web/ui/PortLink.h
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
            ~Events() = default;
        };

        struct Counters
        {
            std::uint32_t posted = 0, dropped = 0, delivered = 0;
        };

        explicit PortLink(Events* events = nullptr);     // installs Module.fcmpPort
        ~PortLink() override;

        PortLink(const PortLink&) = delete;
        PortLink& operator=(const PortLink&) = delete;

        void post(std::span<const std::uint8_t> record) override;
        void setSink(ReplySink* sink) override;
        void setEvents(Events* events) noexcept { events_ = events; }
        bool connected() const noexcept { return connected_; }
        const Counters& counters() const noexcept { return counters_; }

    private:
        static void onRecord(void* self, int bytes);
        static void onConnected(void* self, double sampleRate, int maxBlock);
        static void onDisconnected(void* self);

        ReplySink* sink_ = nullptr;
        Events*    events_ = nullptr;
        bool       connected_ = false;
        Counters   counters_{};
        std::unique_ptr<std::uint8_t[]> inbox_;          // sizeof(Reply) bytes at a stable address: JS copies into it
    };
} // namespace fcmp::web
