// Source/editor/SlotModel.h — one Mode-filtered parameter as FunkGui's ValueModel (02 §9.4, the contract between 01's
// resolver and FunkGui's RuleSlider). The Panel owns one per Mode-filtered Pid (PanelContext::slot); a slot, a TRANSFER
// or STEP handle and the SC corner handle bound to the same Pid share it, so they behave and read identically (§7.4).
// Declared by U1a and frozen at FZ4; U1s (S6) owns it and completes it (live sub-readouts, CLAMPED FROM text, snap-on-
// write details, the slot tests), and may add to it.
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

    private:
        struct Range { float n0, n1; };                          // host-normalised ends of the drawn track
        Range trackRange() const noexcept;
        float trackOf(float plain) const noexcept;               // plain -> 0..1 on the drawn track

        const FrameState&  frame_;
        fcdsp::Pid         pid_;
        funkgui::ParamPort& port_;

        mutable std::array<funkgui::Detent, kMaxDetents> detents_{};   // view(): stepped detents, hybrid end cells
        mutable std::array<float, kMaxDetents> notches_{};             // view(): soft notches on the track
    };
}
