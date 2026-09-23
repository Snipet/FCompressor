#pragma once

// Input sanitisation (01 §5.1, §5.8; K2 #13). EngineHost runs it on main and key before ANY delay line, filter or
// meter touches them, so the dry delay and the bypass path are finite by construction.
//
// Frozen at FZ0. F0 declares; F1 (S1) implements.

namespace fcdsp {

// Before ANY delay line or filter: NaN/inf -> 0 (bit test (bits & 0x7f800000) != 0x7f800000), then clamp |x| <= 1e6
// (+120 dBFS). Returns how many samples were replaced or clamped (-> UiFrame kUiPoisonReset notice when > 0).
int sanitize(const float* in, float* out, int n) noexcept;

} // namespace fcdsp
