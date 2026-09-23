#pragma once

// The engine interface the host drives (01 §5.3; E §3.5-3.6): one virtual call per 64-sample chunk, never per sample.
// Engines are placement-constructed into two preallocated kArenaBytes arena slots, hold POD state only, and are
// destroyed with a trivial ~IEngine(). Sprint-frozen.

#include "fcdsp/core/Simd.h"
#include "fcdsp/params/EngineParams.h"
#include <cstddef>
#include <cstdint>
#include <span>

// [[clang::nonblocking]] where the compiler supports it (03 §2.10 rtsan; ADR-46, K2 #18): EngineHost::process and the
// IEngine per-chunk calls (control, colour) carry it. -Wfunction-effects runs on fcdsp only, in the rtsan preset; an
// out-of-line definition repeats the macro so its effects match the declaration.
#if defined(__has_cpp_attribute)
  #if __has_cpp_attribute(clang::nonblocking)
    #define FCDSP_NONBLOCKING [[clang::nonblocking]]
  #endif
#endif
#if !defined(FCDSP_NONBLOCKING)
  #define FCDSP_NONBLOCKING
#endif

namespace fcdsp {

inline constexpr int         kChunk = 64;         // base-rate samples per control() call
inline constexpr std::size_t kArenaBytes = 8192;  // per slot, alignas(64) (E §3.6)
inline constexpr int         kInternals = 16;     // UiFrame words; a descriptor declares <= 8 (words 8-15 reserved)

struct PrepareInfo { float fs; int osFactor; std::span<float> scratch; };   // scratch: host-owned, per arena slot

enum class LaneDomain : uint8_t { lr = 0, ms = 1 };
struct Carry {                                    // Mode/kernel switch hand-over, lanes {c0, c1, aux0, aux1}
    simd::f32x4 grDb;                             // applied GR (>= 0)
    simd::f32x4 detDb;                            // detector level, detector-law dB
    simd::f32x4 s2GrDb;
    float    relNowMs[2];                         // lets a program-dependent Mode seed its memory term
    uint8_t  msDomain;                            // LaneDomain of lanes 0-1 in the outgoing engine (K2 #3d)
    uint8_t  pad[3];
    uint32_t valid;                               // 0 = cold start (seed() is then a no-op)
};

// Engine status bits per sample, in HistoryColumn::bits layout (01 §6.3): b0-1 phase of the max-GR lane
// (0 idle, 1 attack, 2 hold, 3 release), b2 auto-slow, b3 range-limited, b4 stage-2 active.
struct ControlIo {
    int n;                                        // 1..kChunk
    uint64_t sampleIndex;                         // absolute index of sample 0 (ControlTicker)
    const simd::f32x4* sc;                        // [n] linear SC after host filters, preGain and encode
    simd::f32x4* grDb;                            // [n] out: applied GR per lane (lanes 0-1 consumed by host)
    simd::f32x4* detDb;                           // [n] out or nullptr (tap/telemetry): curve-axis level + preGain
    simd::f32x4* tgtDb;                           // [n] out or nullptr: static target GR (post link)
    simd::f32x4* s2GrDb;                          // [n] out or nullptr
    uint8_t* bits;                                // [n] out or nullptr: status bits above
    bool keyExternal;                             // FB kernels evaluate FF on the key (E §2.6); part of the kernel key
};

struct EngineTelemetry { float attackNowMs[2], releaseNowMs[2], crestDb[2]; };   // lanes 0-1, end of the last chunk

struct AudioIo {
    int nOs;                                      // samples at the OS rate
    float* const* wet;                            // [2][nOs] in: upsampled delayed main x g (g includes this path's
                                                  //   preGain, 01 §5.4), encoded; out: coloured (pre-makeup)
    const float* const* grDbOs;                   // [2][nOs] applied GR interpolated to the OS rate
};

class IEngine {                                   // one virtual call per chunk, never per sample (E §3.6)
public:
    virtual ~IEngine() = default;                 // trivial: engines own no resources
    virtual void  prepare(const PrepareInfo&) noexcept = 0;   // math only; allocation-free; runs the FB stability guard
    virtual void  reset() noexcept = 0;
    virtual void  setParams(const EngineParams&) noexcept = 0;   // per block: smoother targets
    virtual void  snapParams() noexcept = 0;                     // recall: jump smoothers to targets
    virtual Carry carry() const noexcept = 0;
    virtual void  seed(const Carry&) noexcept = 0;
    virtual void  control(const ControlIo&) noexcept FCDSP_NONBLOCKING = 0;
    virtual void  colour(const AudioIo&) noexcept FCDSP_NONBLOCKING = 0;
    virtual float autoMakeupDb() const noexcept = 0;             // 0 unless kEngAutoMakeup (E §2.2: r^(0 dBFS)*k)
    virtual int   scDelaySamples() const noexcept = 0;           // the engine's own SC delay (true-peak interpolator), else 0
    virtual void  internals(float out[kInternals]) const noexcept = 0;
    virtual void  telemetry(EngineTelemetry&) const noexcept = 0; // K3 #11
    virtual bool  finite() const noexcept = 0;                   // poison check (01 §5.8)
};

} // namespace fcdsp
