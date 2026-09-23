#pragma once

// The engine interface the host drives (01 §5.3; E §3.5-3.6): one virtual call per 64-sample chunk, never per sample.
// Engines are placement-constructed into two preallocated kArenaBytes arena slots, hold POD state only, and are
// destroyed with a plain ~IEngine() (engines own no resources). Sprint-frozen.
//
// Real time (FZ0 errata, R-F0 #1): EVERY member below, the destructor included, is FCDSP_NONBLOCKING. The host
// constructs, prepares, seeds, runs and destroys engines on the audio thread during a Mode switch (01 §5.5), and a
// virtual call cannot be inferred, so each declaration carries the macro and each override repeats it. The macro is
// defined in core/Rt.h (it moved there from this header, so core/ can use it too).

#include "fcdsp/core/Rt.h"
#include "fcdsp/core/Simd.h"
#include "fcdsp/params/EngineParams.h"
#include <cstddef>
#include <cstdint>
#include <span>

namespace fcdsp {

inline constexpr int         kChunk = 64;         // base-rate samples per control() call
inline constexpr std::size_t kArenaBytes = 8192;  // per slot, alignas(64) (E §3.6)
inline constexpr int         kInternals = 16;     // UiFrame words; a descriptor declares <= 8 (words 8-15 reserved)

struct PrepareInfo {                              // scratch: host-owned, per arena slot
    float fs = 0; int osFactor = 1; std::span<float> scratch{};
};

enum class LaneDomain : uint8_t { lr = 0, ms = 1 };

// Every member has a default initialiser (FZ0 errata, R-F0 #7): `Carry c;` is a well-defined cold start.
struct Carry {                                    // Mode/kernel switch hand-over, lanes {c0, c1, aux0, aux1}
    simd::f32x4 grDb{};                           // applied GR (>= 0)                    -> Ballistics::seed
    simd::f32x4 detDb{};                          // detector level, detector-law dB      -> Detector::seed
    simd::f32x4 s2GrDb{};                         // stage-2 GR, 0 without stage 2        -> Stage2::seed
    float    relNowMs[2]{};                       // lets a program-dependent Mode seed its memory term
    uint8_t  msDomain = 0;                        // LaneDomain of lanes 0-1 in the outgoing engine (K2 #3d)
    uint8_t  pad[3]{};
    uint32_t valid = 0;                           // 0 = cold start (seed() is then a no-op)
};
static_assert(sizeof(Carry) == 64 && alignof(Carry) == 16);

// Engine status bits per sample, in HistoryColumn::bits layout (01 §6.3): b0-1 phase of the max-GR lane
// (0 idle, 1 attack, 2 hold, 3 release), b2 auto-slow, b3 range-limited, b4 stage-2 active.
struct ControlIo {
    int n = 0;                                    // 1..kChunk
    uint64_t sampleIndex = 0;                     // absolute index of sample 0 (ControlTicker)
    const simd::f32x4* sc = nullptr;              // [n] linear SC after host filters, preGain and encode
    simd::f32x4* grDb = nullptr;                  // [n] out: applied GR per lane (lanes 0-1 consumed by host)
    simd::f32x4* detDb = nullptr;                 // [n] out or nullptr (tap/telemetry): curve-axis level + preGain
    simd::f32x4* tgtDb = nullptr;                 // [n] out or nullptr: static target GR (post link)
    simd::f32x4* s2GrDb = nullptr;                // [n] out or nullptr
    uint8_t* bits = nullptr;                      // [n] out or nullptr: status bits above
    bool keyExternal = false;                     // FB kernels evaluate FF on the key (E §2.6); part of the kernel key
};

struct EngineTelemetry {                          // lanes 0-1, end of the last chunk
    float attackNowMs[2]{}, releaseNowMs[2]{}, crestDb[2]{};
};

struct AudioIo {
    int nOs = 0;                                  // samples at the OS rate
    float* const* wet = nullptr;                  // [2][nOs] in: upsampled delayed main x g (g includes this path's
                                                  //   preGain, 01 §5.4), encoded; out: coloured (pre-makeup)
    const float* const* grDbOs = nullptr;         // [2][nOs] applied GR interpolated to the OS rate
};

class IEngine {                                   // one virtual call per chunk, never per sample (E §3.6)
public:
    virtual ~IEngine() FCDSP_NONBLOCKING = default;                       // engines own no resources
    virtual void  prepare(const PrepareInfo&) noexcept FCDSP_NONBLOCKING = 0;   // math only; allocation-free; runs
                                                                               //   the FB stability guard
    virtual void  reset() noexcept FCDSP_NONBLOCKING = 0;
    virtual void  setParams(const EngineParams&) noexcept FCDSP_NONBLOCKING = 0;   // per block: smoother targets
    virtual void  snapParams() noexcept FCDSP_NONBLOCKING = 0;                     // recall: jump smoothers to targets
    virtual Carry carry() const noexcept FCDSP_NONBLOCKING = 0;
    virtual void  seed(const Carry&) noexcept FCDSP_NONBLOCKING = 0;               // Detector/Ballistics/Stage2::seed
    virtual void  control(const ControlIo&) noexcept FCDSP_NONBLOCKING = 0;
    virtual void  colour(const AudioIo&) noexcept FCDSP_NONBLOCKING = 0;
    virtual float autoMakeupDb() const noexcept FCDSP_NONBLOCKING = 0;   // 0 unless kEngAutoMakeup
                                                                         //   (E §2.2: r^(0 dBFS)*k)
    virtual int   scDelaySamples() const noexcept FCDSP_NONBLOCKING = 0; // the engine's own SC delay (true-peak
                                                                         //   interpolator), else 0
    virtual void  internals(float out[kInternals]) const noexcept FCDSP_NONBLOCKING = 0;
    virtual void  telemetry(EngineTelemetry&) const noexcept FCDSP_NONBLOCKING = 0;   // K3 #11
    virtual bool  finite() const noexcept FCDSP_NONBLOCKING = 0;                      // poison check (01 §5.8)
};

} // namespace fcdsp
