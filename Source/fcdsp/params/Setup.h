#pragma once

// The two setup enums (01 §3.2), shared by RawParams (01 §4.4), EngineHost (01 §5.4) and the processor without
// including the engine. Both are non-automatable globals that change latency; they are applied only in prepareToPlay
// and by the message-thread SetupWatcher (01 §3.1).

#include <cstdint>

namespace fcdsp {

enum class Quality : uint8_t { eco = 0, std = 1, hq = 2 };
enum class LookaheadBudget : uint8_t { off = 0, ms5 = 1, ms20 = 2 };
constexpr float budgetMs(LookaheadBudget b) noexcept {
    return b == LookaheadBudget::off ? 0.f : b == LookaheadBudget::ms5 ? 5.f : 20.f;
}

} // namespace fcdsp
