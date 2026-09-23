#pragma once

// Value text for the UI and the host (01 §4.6; K1 #15; K2 #25a-b). The value never contains the slot label; the unit
// is part of the text. n/a prints "-" (U+2013), locked "(10 MS)", derived "(= 0.8 MS)", program "~" + the nominal
// value. Minus is U+2212 in every formatter; parseHost accepts both '-' and U+2212. Sprint-frozen; F2 (S1) implements
// Text.cpp.
//
// Pid precondition (FZ0 errata, R-F0 #5): every function here takes a MODE-FILTERED Pid, idx(pid) < kNumModeParams
// (the 22 values RawParams/ParamView hold; their indexers assert it). Calling formatHost/parseHost with a global is
// out of bounds. The 7 globals are formatted by the plugin's host-text glue (plugin/HostText.cpp, P1), never here:
//   mode                           the Mode's name from the registry: resolveSlot(slot).entry->desc->name (entry is
//                                  nullptr on the null row, before slot 0 is registered)
//   quality, labudget              kHostParams[idx(pid)].choices[index] ("ECO"/"STD"/"HQ", "OFF"/"5 MS"/"20 MS")
//   extkey, listen, delta, bypass  "OFF" / "ON" (kit::kOffOn's labels)

#include "fcdsp/params/Pid.h"
#include "fcdsp/params/Resolve.h"
#include <string_view>

namespace fcdsp {

// The UI needs the parts separately (value and unit on a shared baseline, a spoken string for a11y). K1 #15.
struct FormattedValue {
    char value[24];      // "30", "4:1", "250", "AUTO", "-18.0" (U+2212 minus); NEVER the slot label ("INPUT" is
                         // ParamSpec::label)
    char unit[8];        // "DB", "MS", "uS" (U+00B5), "%", "HZ", "" (dial numbers)
    char spoken[64];     // "4 to 1", "0.3 milliseconds", "all buttons"
    char prefix;         // 0, '~' (program), '=' (derived), '(' (locked: closed by formatValue)
};
void formatParts(const ParamView&, Pid, FormattedValue&) noexcept;                    // Mode-filtered Pid only
// UI and host readouts as one string: prefix + value + ' ' + unit ("4:1", "30", "250 uS", "~10 MS", "(= 0.8 MS)").
int formatValue(const ParamView&, Pid, char* out, int cap) noexcept;
// Host valueToText for an arbitrary candidate plain value: resolves `current` with v[pid] = plain. Mode-filtered Pid
// only (precondition above).
int formatHost(const ModeEntry&, const RawParams& current, Pid, float plain, char* out, int cap) noexcept;
// Host textToValue: accepts step labels/texts, Mode display numbers and units, universal units. false = no parse.
// Mode-filtered Pid only (precondition above).
bool parseHost(const ModeEntry&, const RawParams& current, Pid, std::string_view text, float& plainOut) noexcept;

} // namespace fcdsp
