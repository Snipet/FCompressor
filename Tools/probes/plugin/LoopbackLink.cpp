// Tools/probes/plugin/LoopbackLink.cpp — see LoopbackLink.h. A helper of fcmp_probe_plugin: no FCMP_PROBE line, so it
// registers no test.
#include "LoopbackLink.h"

#include <cstddef>
#include <cstring>
#include <new>

namespace fcmp::probe
{
    int LoopbackLink::Log::snapped() const noexcept
    {
        int n = 0;
        for (const std::uint8_t s : snaps)
            n += s != 0 ? 1 : 0;
        return n;
    }

    LoopbackLink::LoopbackLink() : engine_(fcmp_web_create())
    {
        if (engine_ == nullptr)
            throw std::bad_alloc();
    }

    LoopbackLink::~LoopbackLink()
    {
        fcmp_web_destroy(engine_);
    }

    void LoopbackLink::setSink(fcmp::web::ReplySink* sink) { sink_ = sink; }

    void LoopbackLink::clearLog() noexcept
    {
        const auto last = log_.lastParams;
        log_ = Log{};
        log_.lastParams = last;
    }

    void LoopbackLink::post(std::span<const std::uint8_t> record)
    {
        using namespace fcmp::web;

        // The log reads the record as the engine will (a copy: the bytes may lie at any alignment).
        Header h{};
        if (record.size() >= sizeof(Header))
            std::memcpy(&h, record.data(), sizeof h);
        const auto is = [&](Kind k) {
            return record.size() >= sizeof(Header) && h.kind == static_cast<std::uint16_t>(k);
        };
        if (is(Kind::params) && record.size() == sizeof(ParamsMsg))
        {
            ParamsMsg m;
            std::memcpy(static_cast<void*>(&m), record.data(), sizeof m);
            ++log_.params;
            log_.snaps.push_back(m.snap != 0u ? 1 : 0);
            for (std::size_t i = 0; i < kParamCount; ++i)
                log_.lastParams[i] = m.plain[i];
        }
        else if (is(Kind::attach) && record.size() == sizeof(AttachMsg))
        {
            AttachMsg m;
            std::memcpy(static_cast<void*>(&m), record.data(), sizeof m);
            ++log_.attaches;
            log_.attached.push_back(m.attached != 0u ? 1 : 0);
        }
        else if (is(Kind::reset))
            ++log_.resets;
        else if (is(Kind::pull))
            ++log_.pulls;
        else
            ++log_.other;

        const std::int32_t n = fcmp_web_post(engine_, record.data(), static_cast<std::int32_t>(record.size()));
        if (n < 0)
        {
            ++log_.refused;
            log_.lastRefusal = n;
        }
        else if (n > 0 && sink_ != nullptr)
        {
            ++log_.replies;
            sink_->reply({ fcmp_web_reply(engine_), static_cast<std::size_t>(n) });
        }
    }
} // namespace fcmp::probe
