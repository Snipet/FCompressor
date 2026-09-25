// Source/editor/SlotModel.h — one Mode-filtered parameter as FunkGui's ValueModel (02 §9.4, the contract between 01's
// resolver and FunkGui's RuleSlider). The Panel owns one per Mode-filtered Pid (PanelContext::slot); a slot, a TRANSFER
// or STEP handle and the SC corner handle bound to the same Pid share it, so they behave and read identically (§7.4).
// Declared by U1a and frozen at FZ4; U1s (S6) owns it and completes it (live sub-readouts, CLAMPED FROM text, snap-on-
// write details, the slot tests), and may add to it.
//
// U1s (S6) — what the texts say, in the order they win (02 §6.4, §8.1, §8.3; the fit rule is the renderer's):
// - value: formatParts ("~" kept for program values). A value whose value + unit is wider than the 112 px slot at its
//   row's value size falls back to the resolved step's label (its own abbreviation, as formatParts does for a text
//   longer than its buffer: "PEAK · FEEDBACK" -> "PEAK", "GAIN REDUCTION ON" -> "ON"); only a text with no shorter
//   label is cut with an ellipsis (ui.textfit counts those). A derived kFlagProgram value shows the live EFF value
//   while live (Mu 67's progressive RATIO).
// - tag: locked/derived ParamSpec::tag, "+" for an extension; a locked list's tag that repeats the value is dropped,
//   and so is an extension "+" that a visible word would push onto a detent line whose labels fit (the labels win;
//   the footer still says EXTENSION).
// - sub (kMicro, the sub/detent line): CLAMPED FROM <raw> (kClamped); else, on a primary slot while live, its live
//   readout (DET, EFF ratio, the knee span, EFF attack/release of a program-dependent time, AUTO, DRY; 02 §6.4); else
//   locked: brief, else the full step text an abbreviated value hides (never the tag a second time); derived: FOLLOWS
//   <the Mode's name of derivedFrom>; remapped (a DisplayMap scale): the universal value, after the universal name
//   when the Mode renames the slot ("THRESHOLD −28.0 DB" under INPUT; "≈ −28.0 DB" when that does not fit); renamed
//   only: the universal name. Everything is fitted to the room RuleSlider leaves (a tag moved beside a visible word
//   takes its width + 6).
// - key() also hashes the live readout text and the word's visibility, so a slider re-reads exactly when a printed
//   readout changes (0.1 dB / 3 significant digits).
//
// The view is built from the frame's resolved ParamView (FrameState::res.view), never from a second resolver:
// - state: ResolvedParam::state (hybrid: continuous + the end cells of the active spec);
// - label, tag, reason, brief from the active ParamSpec; aka = the universal label when the Mode renames the slot;
// - track: u = (toNorm(pid, plain) − toNorm(pid, lo)) / (toNorm(pid, hi) − toNorm(pid, lo)), t = invert ? 1 − u : u,
//   over the spec's [lo, hi] (the host range when the spec has none: locked, derived);
// - detents: host01 = toNorm(pid, step.plain), so a write lands on the canonical detent (01 §4.5, 02 §8.4);
// - text: formatParts (value never holds the label, K1 #15);
// - key(): the Mode slot, this Pid's raw bits, its driver's resolved step, the ResolvedParam and the active spec.
// Message thread only. view() fills the detent and notch arrays held here (no allocation per frame).
#pragma once

#include "editor/SubView.h"
#include "plugin/ProcessorFacade.h"

#include "fcdsp/params/ParamSpec.h"
#include "fcdsp/params/Pid.h"
#include "fcdsp/params/Resolve.h"

#include <funkgui/params/GestureController.h>
#include <funkgui/params/ParamPort.h>
#include <funkgui/widgets/ValueModel.h>

#include <array>
#include <cstdint>

namespace fcmp::ui
{
    class SlotModel final : public funkgui::ValueModel
    {
    public:
        static constexpr int kMaxDetents = 32;                   // steps of one spec (Mu 67's 21-step INPUT fits)

        SlotModel(ProcessorFacade&, const FrameState&, fcdsp::Pid);

        SlotModel(const SlotModel&) = delete;
        SlotModel& operator=(const SlotModel&) = delete;

        fcdsp::Pid pid() const noexcept { return pid_; }
        const fcdsp::ParamSpec* spec() const noexcept;           // the active spec this frame (nullptr: no Mode)
        const fcdsp::ResolvedParam& resolved() const noexcept;   // this frame's resolved value
        const char* universalLabel() const noexcept;             // "THRESHOLD" (layout::universalLabel)

        // funkgui::ValueModel
        uint64_t   key() const override;
        void       view(funkgui::ValueView&) const override;
        funkgui::ParamPort* port() override;
        float      host01FromTrack(float t) const override;
        float      defaultHost01() const override;
        // writeDetent: ValueModel's (a tap of the canonical detent; locked, derived and n/a never write, 02 §8.4.5).
        // Absolute handle drags (02 §6.5, §7.4; K1 #9): thr through T_in (slope 1 in every Mode), knee and range only
        // with kFlagPlotIsPlain and an identity DisplayMap, atk and rel in seconds on the STEP axis, schpf in Hz.
        bool       plotToHost01(float plotValue, float& host01) const override;

        // ---- U1s additions (S6; additive, no FZ4 declaration above changed) -----------------------------------------

        // The drawn-track position of a plain value of this Pid (0..1 inside the track, unclamped outside it): the
        // map view() uses for the caret, so the slot grid's detector tick and RANGE's GR-used bar sit on the same scale.
        float trackPosition(float plain) const noexcept;

        // A plain value of `pid` in the universal unit of 01 §3.1, as host text without a Mode ("−28.0 DB", "10 MS",
        // "OFF"): value and unit joined by one space. Returns the bytes written (the text is cut at a UTF-8 boundary to
        // fit `cap`). Mode-filtered Pids only. The slot grid's STORED text, CLAMPED FROM and the remap sub-lines.
        static int universalText(fcdsp::Pid pid, float plain, char* out, int cap) noexcept;

        // RuleSlider's (v0.6.0) text layout, which view() fits into and ui.textfit measures: the value, 6 px, the unit
        // (kLabel on a primary row, kMicro on a secondary one); a tag moved to the sub line, 6 px, the sub text.
        static constexpr float kUnitGap = 6.0f;
        static constexpr float kTagGap  = 6.0f;

    private:
        struct Range { float n0, n1; };                          // host-normalised ends of the drawn track
        Range trackRange() const noexcept;
        float trackOf(float plain) const noexcept;               // plain -> 0..1 on the drawn track

        const FrameState&  frame_;
        fcdsp::Pid         pid_;
        funkgui::ParamPort& port_;
        const fcdsp::HistoryRing& ring_;                         // UF1a: the DET readout's 10 ms envelope (additive)

        mutable std::array<funkgui::Detent, kMaxDetents> detents_{};   // view(): stepped detents, hybrid end cells
        mutable std::array<float, kMaxDetents> notches_{};             // view(): soft notches on the track
    };
}
