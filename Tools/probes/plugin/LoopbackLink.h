// Tools/probes/plugin/LoopbackLink.h — an EngineLink over an engine module in the same process (web Sprint C, ADR-93):
// what the page's script and the worklet's MessagePort are in the browser, without either. A helper of
// fcmp_probe_plugin, not a probe (no FCMP_PROBE line) and not facade code: Source/web/facade is exactly what ships and
// never includes the engine, so the one place that joins the two is here.
//
// - It owns one FcmpWebEngine (fcmp_web_create / fcmp_web_destroy). post() hands the record to fcmp_web_post; a
//   record that asked for a reply (a Pull) gets it at once: the sink's reply() runs INSIDE post(), over the engine's
//   own reply buffer, which is valid until the next post (a sink that posts before it has copied reads other bytes).
// - engine() is the worklet's side: the probe calls fcmp_web_configure, fcmp_web_process and fcmp_web_set_gate on it,
//   between posts, as the worklet does between messages.
// - It logs what went through, for the probes' record rows: per kind a count, for every Params record its snap flag and
//   for the last one its 30 values; the records the engine refused (a negative fcmp_web_post); the replies delivered.
//
// Single-threaded: the probe thread is the editor's thread and the worklet's.
#pragma once

#include "web/engine/WebEngine.h"
#include "web/engine/WebProtocol.h"
#include "web/facade/EngineLink.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace fcmp::probe
{
    class LoopbackLink final : public fcmp::web::EngineLink
    {
    public:
        struct Log
        {
            int params = 0, attaches = 0, resets = 0, pulls = 0;     // records posted, by kind
            int other = 0;                                           // records with no header, or of no kind above
            int refused = 0;                                         // fcmp_web_post < 0
            int replies = 0;                                         // replies handed to the sink
            std::vector<std::uint8_t> snaps;                         // one per Params record, in order: 1 = snap
            std::vector<std::uint8_t> attached;                      // one per Attach record: its flag
            std::array<float, fcmp::web::kParamCount> lastParams{};  // the last Params record's values
            std::int32_t lastRefusal = 0;                            // the last negative result (a PostError)

            int snapped() const noexcept;                            // Params records with snap != 0
        };

        LoopbackLink();                                              // throws std::bad_alloc without an engine
        ~LoopbackLink() override;

        LoopbackLink(const LoopbackLink&) = delete;
        LoopbackLink& operator=(const LoopbackLink&) = delete;

        // fcmp::web::EngineLink
        void post(std::span<const std::uint8_t> record) override;
        void setSink(fcmp::web::ReplySink* sink) override;

        FcmpWebEngine* engine() noexcept { return engine_; }
        const Log& log() const noexcept { return log_; }
        void clearLog() noexcept;                                    // the counts and lists; lastParams stays

    private:
        FcmpWebEngine*        engine_ = nullptr;
        fcmp::web::ReplySink* sink_ = nullptr;
        Log                   log_;
    };
} // namespace fcmp::probe
