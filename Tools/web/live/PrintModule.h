#pragma once

// PrintModule: fcmp-print.wasm, the TEST-ONLY module of the browser gate (ADR-93, the web lead phase).
//
// The gate's pages (web/live/*.js), its expectation tool (Tools/web/live/expect.mjs) and the node twin
// (web/tests/print.mjs) render dsp.print's material through the SHIPPED worklet and engine. What they need beside those
// two files is here, compiled from the sources the native probes use, so that a script only moves bytes:
//   the material   Tools/probes/common/PrintProgram.h's program as it is, and the same program with its second half
//                  replaced by a floor at the denormal boundary;
//   the records    a Mode's four parameter sets, with a setup (QUALITY, the lookahead budget, LOOKAHEAD), as
//                  WebProtocol Params records, and an Attach record;
//   the answers    the latency fcdsp gives a setup, the Harness's hash of a channel, and where a channel's tail ends.
// It is a standalone module like the engine (no JavaScript glue, no imports, a fixed memory, malloc and free
// exported): cmake/FcmpWeb.cmake's fcmp_web_print builds it into build-web/live-obj, and cmake/FcmpWebLive.cmake puts
// it in build-web/live beside the pages. It is never part of the site, and the shipped engine module does not grow.
//
// Every pointer is an address in this module's own memory (a block from its malloc). A Mode is an index into the
// registered Modes in slot order, 0 .. fcmp_print_modes() - 1; a set is 0 default, 1 lo, 2 hi, 3 mid.

#include <cstdint>

#if defined(__wasm__)
  #define FCMP_PRINT_EXPORT(name) __attribute__((export_name(#name)))
#else
  #define FCMP_PRINT_EXPORT(name)
#endif

extern "C" {

// The program's length in frames (4 s at 48 kHz: 192,000, a whole number of 128-frame quanta, and so is its half).
FCMP_PRINT_EXPORT(fcmp_print_frames) std::int32_t fcmp_print_frames(void) noexcept;

// dsp.print's program into l and r (fcmp_print_frames() floats each).
FCMP_PRINT_EXPORT(fcmp_print_program) void fcmp_print_program(float* l, float* r) noexcept;

// The denormal-range material into l and r (fcmp_print_frames() floats each): the program's first half, then a square
// wave of +-2^-120 with a period of 96 frames, the right channel inverted. 2^-120 is a normal float, so the engine
// reads it (a denormal input is read as zero) and its silence gate stays open, and every product of it with a gain
// below 1/64 is in the denormal range. The other material of that range, the tail, is the program's first half and
// then no source at all.
FCMP_PRINT_EXPORT(fcmp_print_floor) void fcmp_print_floor(float* l, float* r) noexcept;

// The number of registered Modes.
FCMP_PRINT_EXPORT(fcmp_print_modes) std::int32_t fcmp_print_modes(void) noexcept;

// The key of a Mode, NUL-terminated (valid until the next call), or nullptr.
FCMP_PRINT_EXPORT(fcmp_print_key) const char* fcmp_print_key(std::int32_t mode) noexcept;

// The name of a set as the golden rows spell it (print.<name>.<l|r>.hash), or nullptr.
FCMP_PRINT_EXPORT(fcmp_print_set) const char* fcmp_print_set(std::int32_t set) noexcept;

// One Params record (140 bytes at out): the Mode's set as web.engine.print posts it (the 22 Mode-filtered values and
// the Mode; the other globals at the host's defaults), with QUALITY and the lookahead budget as given (plain values:
// 0 ECO, 1 STD, 2 HQ; 0 off, 1 5 ms, 2 20 ms), LOOKAHEAD in ms when lookMs >= 0, and the snap flag. Returns 140, or
// -1 when there is no such Mode or set.
FCMP_PRINT_EXPORT(fcmp_print_record)
std::int32_t fcmp_print_record(std::int32_t mode, std::int32_t set, float quality, float budget, float lookMs,
                               std::int32_t snap, std::uint8_t* out) noexcept;

// One Attach record (20 bytes at out): the editor is listening (attached != 0) or has gone. Returns 20.
FCMP_PRINT_EXPORT(fcmp_print_attach) std::int32_t fcmp_print_attach(std::int32_t attached, std::uint8_t* out) noexcept;

// One Pull record (16 bytes at out). Returns 16.
FCMP_PRINT_EXPORT(fcmp_print_pull) std::int32_t fcmp_print_pull(std::uint8_t* out) noexcept;

// Reads the fixed part of a Reply (n bytes at reply, at least 320), as web.engine.tail reads it after a timed run:
// 1 when it is a well-formed reply of a configured engine with the editor attached, whose silence gate is open and
// whose UiFrame::publishCount is exactly `blocks`; 0 when it is not, -1 when it is no reply. That count is every block
// the engine has run with the editor attached since it was made: it is NOT reset by a reconfigure (EngineHost.cpp,
// "UiFrame::publishCount survives a reconfigure"), whether a second fcmp_web_configure or a Params record that
// changes QUALITY or the lookahead budget made it. So `blocks` is a row's own count of blocks only on an engine
// configured once and attached from its first block, as fcmp-tail.js's main.cost runs one; an engine armed by
// fcmp-live.js's two records, or one that ran earlier rows, needs the difference from a Pull taken when it was armed.
FCMP_PRINT_EXPORT(fcmp_print_running)
std::int32_t fcmp_print_running(const std::uint8_t* reply, std::int32_t n, std::int32_t blocks) noexcept;

// The latency in samples fcdsp gives a setup (EngineHost::latencyFor): what a row's engine must report once its
// records are in. quality and budget as indices (0 .. 2); -1 when either is out of range.
FCMP_PRINT_EXPORT(fcmp_print_latency)
std::int32_t fcmp_print_latency(std::int32_t quality, std::int32_t budget, double sampleRate) noexcept;

// The Harness's hashFloats (FNV-1a 64 over the little-endian bytes of each float's bit pattern) as two halves,
// hash[0] the low one.
FCMP_PRINT_EXPORT(fcmp_print_hash) void fcmp_print_hash(const float* v, std::int32_t n, std::uint32_t* hash) noexcept;

// Where a channel's tail ends, by its bits: out[0] the index of the last sample that is not a zero (-1: there is
// none), out[1] the number of denormal samples. Beside the hash they say how a browser's values differ from node's: one
// that flushes to zero on its audio thread may end a tail at another sample, and computes no denormal one.
FCMP_PRINT_EXPORT(fcmp_print_tail) void fcmp_print_tail(const float* v, std::int32_t n, std::int32_t* out) noexcept;

} // extern "C"
