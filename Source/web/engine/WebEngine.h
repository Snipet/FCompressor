#pragma once

// WebEngine: the engine module of the browser demo (ADR-93), a C ABI over fcdsp::EngineHost for an AudioWorklet.
//
// fcmp-engine.wasm is this file's functions over fcdsp, linked standalone: no JavaScript glue and no imports. The
// worklet instantiates it with an empty import object, calls `_initialize`, and drives it with the calls below; the
// page's editor module talks to it in WebProtocol.h's records, which the worklet copies in and out without reading.
// The same sources build natively (lint web.engine: portable C++ over fcdsp alone), so the checks drive this exact code
// against the native goldens (Tools/web/enginecheck.cpp).
//
// Threads: one. Every call comes from the worklet's thread; a message is handled between two render quanta. That is
// why fcmp_web_post may reconfigure the engine (it allocates): the worklet has no other thread to do it on. ADR-93
// records this deviation from "configure only from prepareToPlay or the SetupWatcher" (ARCHITECTURE §7).
// fcmp_web_process itself never allocates, locks or calls libm.
//
// Memory: the caller owns the audio and message buffers. In the wasm module they are blocks from the exported
// malloc; every pointer below is an address in the module's own memory.
//
// Exports: under wasm each function carries the compiler's own export attribute, so the module exports exactly these
// names (plus memory, malloc, free and the runtime's initialiser); natively the macro is empty.

#include "fcdsp/core/Rt.h"

#include <cstdint>

#if defined(__wasm__)
  #define FCMP_WEB_EXPORT(name) __attribute__((export_name(#name)))
#else
  #define FCMP_WEB_EXPORT(name)
#endif

extern "C" {

struct FcmpWebEngine;                                   // opaque

// The protocol version this module speaks (WebProtocol.h's kVersion).
FCMP_WEB_EXPORT(fcmp_web_abi_version) std::int32_t fcmp_web_abi_version(void) noexcept;

// A new engine holding the host's default parameter values (Mode slot 0, quality STD, lookahead budget off), not yet
// configured, the silence gate on. Allocates. nullptr when out of memory.
FCMP_WEB_EXPORT(fcmp_web_create) FcmpWebEngine* fcmp_web_create(void) noexcept;
FCMP_WEB_EXPORT(fcmp_web_destroy) void fcmp_web_destroy(FcmpWebEngine* engine) noexcept;

// Configures (or reconfigures) for a sample rate and the largest frame count one fcmp_web_process call will usually
// carry (the worklet: 128), at the current parameter values, quality and lookahead budget; the engine starts snapped
// and cleared. Stereo in, stereo out, no key input. Allocates: with fcmp_web_create and a quality or budget change
// inside fcmp_web_post, the only allocation points. Returns the latency in samples, or -1 on failure.
FCMP_WEB_EXPORT(fcmp_web_configure)
std::int32_t fcmp_web_configure(FcmpWebEngine* engine, double sampleRate, std::int32_t maxBlock) noexcept;

// One render quantum: `frames` samples (any count >= 0) from inL/inR to outL/outR. inR == nullptr: mono, inL feeds both
// channels; inL == nullptr: silence. outR may be nullptr (the right channel is dropped). Outputs may alias inputs.
// Before fcmp_web_configure the output is silence. Denormal input samples are read as zero (wasm has no
// denormals-are-zero mode). Real-time safe: no allocation, no lock, no libm; FCDSP_NONBLOCKING (fcdsp/core/Rt.h), so
// a build with -Wfunction-effects proves it at compile time, as it does for EngineHost::process.
FCMP_WEB_EXPORT(fcmp_web_process)
void fcmp_web_process(FcmpWebEngine* engine, const float* inL, const float* inR, float* outL, float* outR,
                      std::int32_t frames) noexcept FCDSP_NONBLOCKING;

// One WebProtocol record of n bytes (any alignment). Returns the reply's byte count when the record asked for one
// (Pull: read that many bytes at fcmp_web_reply), 0 when it was applied and has no reply, and a negative
// fcmp::web::PostError when it was refused (nothing changed). A Params record whose quality or lookahead budget
// differs from the running setup reconfigures the engine here (see "Threads").
FCMP_WEB_EXPORT(fcmp_web_post)
std::int32_t fcmp_web_post(FcmpWebEngine* engine, const std::uint8_t* bytes, std::int32_t n) noexcept;

// The reply buffer: a fcmp::web::Reply, valid until the next fcmp_web_post. The same address for the engine's life.
FCMP_WEB_EXPORT(fcmp_web_reply) const std::uint8_t* fcmp_web_reply(const FcmpWebEngine* engine) noexcept;

// The running engine's latency in samples (before fcmp_web_configure: what the current quality and budget will give
// at 48 kHz).
FCMP_WEB_EXPORT(fcmp_web_latency) std::int32_t fcmp_web_latency(const FcmpWebEngine* engine) noexcept;

// The silence gate (on by default): once the input has been exactly zero for longer than the engine's tail (and at
// least 100 ms), the engine is reset and the output is zeros, with no processing, until a non-zero sample or a Params
// record arrives. A record counts as activity: the engine then runs the new values on the silence for that long
// again, as the plugin's engine does all the time, so a Mode change made while idle has finished its crossfade before
// signal returns. The gate keeps decaying state from sitting in the denormal range (wasm cannot flush it) and costs
// nothing audible. on == 0 turns it off.
FCMP_WEB_EXPORT(fcmp_web_set_gate) void fcmp_web_set_gate(FcmpWebEngine* engine, std::int32_t on) noexcept;

// The self-check: renders a short fixed program through this ABI (five Modes; per Mode create, a Params record,
// configure, an edit that ramps, a reconfigure to HQ with lookahead, process and Pull throughout) inside the module
// and writes a 64-bit hash of what came out as two 32-bit halves, hash[0] the low one (a wasm32 export cannot return
// an i64 to JavaScript without BigInt glue). The value is the same on every target whose arithmetic meets fcdsp's
// contract. Allocates and frees one engine. Returns 0, or -1 on failure.
FCMP_WEB_EXPORT(fcmp_web_selfcheck) std::int32_t fcmp_web_selfcheck(std::uint32_t* hash) noexcept;

} // extern "C"

namespace fcmp::web
{
    enum PostError : std::int32_t
    {
        kPostBadArgument = -1,       // a null engine or pointer, or fewer bytes than a header
        kPostBadMagic = -2,
        kPostBadVersion = -3,
        kPostBadKind = -4,
        kPostBadSize = -5,           // the byte count is not the kind's
        kPostFailed = -6,            // a reconfigure ran out of memory: the previous setup and values stay
    };
}
