// Source/plugin/ProcessorFacade.h — the editor's only way into the processor (02 §9.5). Frozen at FZ1.
// Written by the lead at the end of Sprint 1, verbatim from 02 §9.5 against FunkGui v0.2.0's ParamPort.h
// (plus the standard includes it needs to compile standalone). The processor (P1) implements it; FakeFacade (U1a)
// implements it for headless UI probes.
#pragma once
#include "fcdsp/params/Pid.h"
#include "fcdsp/params/Resolve.h"          // RawParams
#include "fcdsp/telemetry/UiFrame.h"
#include "fcdsp/telemetry/HistoryRing.h"
#include <funkgui/params/ParamPort.h>
#include <cstdint>
#include <string>
#include <string_view>

namespace fcmp {
enum class ScTab : uint8_t { sidechain, colour };
struct UiState {                                   // <UI charExpanded="0|1" scTab="sidechain|colour"/> (01 §9.1)
    bool  charExpanded = false;
    ScTab scTab = ScTab::sidechain;
};
struct StateNotice {                               // footer notices after a state load (01 §9.1)
    uint32_t serial = 0;                           // bumps on every accepted state load (a non-<PARAMS> blob is ignored)
    bool     newerSession = false;                 // stateVersion > kStateVersion
    bool     modeMigrated = false;                 // modeId unknown or retired → successor or clean
    bool     modeRevised  = false;                 // saved modeRev < the Mode's current revision (K2 #10)
    uint16_t savedRev = 0, currentRev = 0;
    char     fromKey[25] {}, toKey[25] {};         // keys are ≤ 24 chars
};

class PresetAccess {                               // message thread only; the preset strip and browser draw from it
public:
    struct Row { std::string uuid, name, category, modeKey; bool factory = false; };
    virtual ~PresetAccess() = default;
    virtual int      count() const = 0;
    virtual Row      row(int index) const = 0;
    virtual int      current() const = 0;          // −1 = none
    virtual bool     modified() const = 0;         // PresetManager::isModified, or a different Mode (S12 revisions 4, 8)
    virtual uint32_t revision() const = 0;         // bumps on any list or selection change
    virtual void     apply(int index) = 0;         // brackets itself with beginBatch/endBatch (01 §9.2 hooks)
    virtual void     step(int delta) = 0;          // ‹ ›
    virtual bool     saveAs(std::string_view name, std::string_view category) = 0;
    // S12 lead revision 8 (P3's interface request), additive: user-preset management for the browser. Every call
    // returns false and changes nothing when refused; a success bumps revision(). The defaults refuse everything.
    virtual bool     rename(int /*index*/, std::string_view /*newName*/) { return false; }   // user rows; name taken: false
    virtual bool     remove(int /*index*/) { return false; }                                 // user rows only
    virtual bool     importFile(std::string_view /*path*/) { return false; }  // a preset file; fresh uuid, unique name
    virtual bool     exportFile(int /*index*/, std::string_view /*path*/) { return false; }  // any row, PresetFile
    // S12 lead revision 11 (U6's request), additive: save the current parameters and Mode over a user row, keeping its
    // name, category, uuid and tags; it becomes current() and unmodified. Factory rows refuse. Default: refuse.
    virtual bool     overwrite(int /*index*/) { return false; }
};

class ProcessorFacade {
public:
    virtual ~ProcessorFacade() = default;
    // parameters: 29 ports in Pid order, OWNED BY THE PROCESSOR, so they outlive every editor (K2 #27)
    virtual funkgui::ParamPort& port(fcdsp::Pid) = 0;
    virtual fcdsp::RawParams currentRaw() const = 0;               // relaxed loads + the configured lookahead budget
    // telemetry (01 §6)
    virtual bool readUiFrame(fcdsp::UiFrame&) const = 0;
    virtual const fcdsp::HistoryRing& history() const = 0;
    virtual void setUiAttached(bool) = 0;                          // count-based (01 §6.3)
    // per-instance UI state and notices
    virtual UiState& uiState() = 0;                                // message thread
    virtual StateNotice stateNotice() const = 0;
    // multi-parameter writes: while a batch is open the audio thread reuses the previous BlockParams (K2 #23);
    // endBatch() raises the engine snap. Nestable (a counter).
    virtual void beginBatch() = 0;
    virtual void endBatch() = 0;
    virtual PresetAccess& presets() = 0;                           // an empty implementation until P3 (03 §4.9)
};
}
