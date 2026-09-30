// FCMP_PROBE layer=proc name=history scope=global timeout=120
//
// proc.history (v1.2, ADR-91): undo/redo and the A/B compare on a real fcmp::Processor (EditHistory over its ports,
// batches, presets and state), the editor's writes made as the editor makes them: through ProcessorFacade::port with a
// begin/set/end gesture, and composite writes inside beginBatch/endBatch. Clean, spec-only.
//
//   gesture.*        one gesture on THRESHOLD is one entry named THRESHOLD; undo puts the raw value back bit for bit and
//                    redo the new one; nothing to redo after a new edit
//   automation.*     a host write (setValueNotifyingHost, no gesture) is never recorded; one during a THRESHOLD gesture
//                    is not undone with it
//   batch.*          two parameters written inside one batch are one entry ("2 CHANGES"), undone together
//   setup.*          QUALITY and BYPASS gestures are not recorded
//   preset.*         applying a factory preset is one entry named PRESET; undo restores the values and the preset that
//                    was current; redo makes the preset current again, unmodified
//   capacity         150 edits leave exactly EditHistory::kCapacity undos
//   load.*           a state load clears the history
//   ab.*             B starts as a copy of A; an edit in B is left behind when A is selected and comes back with B; the
//                    switch is an undo step; copySlot fills the other slot; each slot keeps its preset; the inactive
//                    slot survives a save and a load into another instance
#include "ProbeRegistry.h"

#include "plugin/EditHistory.h"
#include "plugin/Processor.h"
#include "plugin/ProcessorFacade.h"

#include "fcdsp/params/HostParams.h"
#include "fcdsp/params/Pid.h"

#include <funkgui/params/ParamPort.h>

#include <juce_gui_basics/juce_gui_basics.h>

#include <bit>
#include <cstdint>
#include <memory>
#include <string>

namespace
{
    using funkgui::test::Probe;
    using fcdsp::Pid;

    int b(bool v) { return v ? 1 : 0; }
    bool same(float a, float c) { return std::bit_cast<uint32_t>(a) == std::bit_cast<uint32_t>(c); }

    // One editor gesture: begin, set the plain value, end (a tap or a drag's last write).
    void gesture(fcmp::Processor& p, Pid pid, float plain)
    {
        funkgui::ParamPort& port = p.port(pid);
        port.beginGesture();
        port.setValue01(fcdsp::toNorm(pid, plain));
        port.endGesture();
    }

    // A host automation write: no gesture.
    void automate(fcmp::Processor& p, Pid pid, float plain)
    {
        juce::RangedAudioParameter& prm = p.parameter(pid);
        prm.setValueNotifyingHost(prm.convertTo0to1(plain));
    }

    std::unique_ptr<fcmp::Processor> fresh() { return std::make_unique<fcmp::Processor>(); }

    void gestureRows(Probe& P)
    {
        auto p = fresh();
        fcmp::EditAccess& h = p->edits();
        const float before = p->rawValue(Pid::thr);
        P.eq("gesture.empty_at_start", b(!h.canUndo() && !h.canRedo()), 1);
        gesture(*p, Pid::thr, -31.5f);
        const float after = p->rawValue(Pid::thr);
        P.eq("gesture.one_entry", b(h.canUndo() && std::string(h.undoName()) == "THRESHOLD"), 1);
        P.eq("gesture.undo", b(h.undo() && same(p->rawValue(Pid::thr), before) && !h.canUndo() && h.canRedo()), 1);
        P.eq("gesture.redo", b(h.redo() && same(p->rawValue(Pid::thr), after) && !h.canRedo()), 1);
        h.undo();
        gesture(*p, Pid::knee, 12.0f);
        P.eq("gesture.new_edit_drops_redo", b(!h.canRedo() && std::string(h.undoName()) == "KNEE"), 1);
    }

    void automationRows(Probe& P)
    {
        {
            auto p = fresh();
            automate(*p, Pid::ratio, 0.9f);
            P.eq("automation.not_recorded", b(!p->edits().canUndo()), 1);
        }
        {
            auto p = fresh();
            const float thr0 = p->rawValue(Pid::thr);
            funkgui::ParamPort& port = p->port(Pid::thr);
            port.beginGesture();
            port.setValue01(fcdsp::toNorm(Pid::thr, -40.0f));
            automate(*p, Pid::ratio, 0.9f);                      // the host moves RATIO meanwhile
            port.endGesture();
            const float ratio = p->rawValue(Pid::ratio);
            p->edits().undo();
            P.eq("automation.kept_by_undo", b(same(p->rawValue(Pid::thr), thr0) && same(p->rawValue(Pid::ratio), ratio)),
                 1);
        }
    }

    void batchRows(Probe& P)
    {
        auto p = fresh();
        const float thr0 = p->rawValue(Pid::thr), knee0 = p->rawValue(Pid::knee);
        p->beginBatch();
        gesture(*p, Pid::thr, -20.0f);
        gesture(*p, Pid::knee, 18.0f);
        p->endBatch();
        fcmp::EditAccess& h = p->edits();
        P.eq("batch.one_entry", b(std::string(h.undoName()) == "2 CHANGES"), 1);
        h.undo();
        P.eq("batch.undone_together", b(same(p->rawValue(Pid::thr), thr0) && same(p->rawValue(Pid::knee), knee0)
                                        && !h.canUndo()), 1);

        auto s = fresh();
        gesture(*s, Pid::quality, 2.0f);
        gesture(*s, Pid::bypass, 1.0f);
        P.eq("setup.not_recorded", b(!s->edits().canUndo()), 1);
    }

    void presetRows(Probe& P)
    {
        auto p = fresh();
        fcmp::PresetAccess& pr = p->presets();
        fcmp::EditAccess& h = p->edits();
        gesture(*p, Pid::thr, -33.0f);                           // a sound of its own first
        const float thr0 = p->rawValue(Pid::thr);
        const int before = pr.current();
        int target = -1;
        for (int i = 1; i < pr.count() && target < 0; ++i)       // a factory preset of Clean with its own threshold
            if (pr.row(i).factory && pr.row(i).modeKey == "clean")
                target = i;
        P.eq("preset.found_clean_row", b(target > 0), 1);
        if (target <= 0)
            return;
        pr.apply(target);
        P.eq("preset.one_entry", b(std::string(h.undoName()) == "PRESET" && pr.current() == target), 1);
        h.undo();
        P.eq("preset.undo_values", b(same(p->rawValue(Pid::thr), thr0)), 1);
        P.eq("preset.undo_identity", b(pr.current() == before), 1);
        h.redo();
        P.eq("preset.redo_identity", b(pr.current() == target && !pr.modified()), 1);
    }

    void capacityRows(Probe& P)
    {
        auto p = fresh();
        for (int i = 0; i < 150; ++i)
            gesture(*p, Pid::thr, -10.0f - 0.1f * static_cast<float>(i));
        int undos = 0;
        while (p->edits().undo())
            ++undos;
        P.eq("capacity", undos, static_cast<int64_t>(fcmp::EditHistory::kCapacity));
    }

    void loadRows(Probe& P)
    {
        auto p = fresh();
        juce::MemoryBlock blob;
        p->getStateInformation(blob);
        gesture(*p, Pid::thr, -25.0f);
        p->setStateInformation(blob.getData(), static_cast<int>(blob.getSize()));
        P.eq("load.clears", b(!p->edits().canUndo() && !p->edits().canRedo()), 1);
    }

    void abRows(Probe& P)
    {
        auto p = fresh();
        fcmp::EditAccess& h = p->edits();
        P.eq("ab.starts_on_a", b(h.compareSlot() == 0 && h.slotUsed(0) && !h.slotUsed(1)), 1);
        gesture(*p, Pid::thr, -30.0f);
        const float a = p->rawValue(Pid::thr);
        h.selectSlot(1);
        P.eq("ab.b_is_copy", b(h.compareSlot() == 1 && h.slotUsed(1) && same(p->rawValue(Pid::thr), a)), 1);
        gesture(*p, Pid::thr, -12.0f);
        const float bv = p->rawValue(Pid::thr);
        h.selectSlot(0);
        P.eq("ab.a_back", b(h.compareSlot() == 0 && same(p->rawValue(Pid::thr), a)), 1);
        h.selectSlot(1);
        P.eq("ab.b_kept", b(h.compareSlot() == 1 && same(p->rawValue(Pid::thr), bv)), 1);
        P.eq("ab.switch_named", b(std::string(h.undoName()) == "A/B"), 1);
        h.undo();
        P.eq("ab.undo_switches_back", b(h.compareSlot() == 0 && same(p->rawValue(Pid::thr), a)), 1);
        h.redo();
        P.eq("ab.redo_switches_again", b(h.compareSlot() == 1 && same(p->rawValue(Pid::thr), bv)), 1);

        // copySlot: the live B sound into A.
        h.copySlot();
        h.selectSlot(0);
        P.eq("ab.copy", b(same(p->rawValue(Pid::thr), bv)), 1);

        // The session keeps the inactive slot: a save from here (A active, B holds -12) into another instance.
        gesture(*p, Pid::thr, -44.0f);
        const float aNow = p->rawValue(Pid::thr);
        juce::MemoryBlock blob;
        p->getStateInformation(blob);
        auto q = fresh();
        q->setStateInformation(blob.getData(), static_cast<int>(blob.getSize()));
        fcmp::EditAccess& g = q->edits();
        P.eq("ab.session.active", b(g.compareSlot() == 0 && g.slotUsed(1) && same(q->rawValue(Pid::thr), aNow)), 1);
        g.selectSlot(1);
        P.eq("ab.session.inactive_slot", b(same(q->rawValue(Pid::thr), bv)), 1);
    }

    void abPresetRows(Probe& P)
    {
        auto p = fresh();
        fcmp::PresetAccess& pr = p->presets();
        fcmp::EditAccess& h = p->edits();
        int first = -1, second = -1;
        for (int i = 1; i < pr.count() && second < 0; ++i)
            if (pr.row(i).factory && pr.row(i).modeKey == "clean")
                (first < 0 ? first : second) = i;
        P.eq("ab.preset.rows", b(first > 0 && second > 0), 1);
        if (second < 0)
            return;
        pr.apply(first);
        h.selectSlot(1);
        pr.apply(second);
        h.selectSlot(0);
        P.eq("ab.preset.a_keeps_its_preset", b(pr.current() == first && !pr.modified()), 1);
        h.selectSlot(1);
        P.eq("ab.preset.b_keeps_its_preset", b(pr.current() == second && !pr.modified()), 1);
    }
}

FCMP_PROBE(proc, history)
{
    (void) C;
    const juce::ScopedJuceInitialiser_GUI juceInit;              // this thread is the message thread
    gestureRows(P);
    automationRows(P);
    batchRows(P);
    presetRows(P);
    capacityRows(P);
    loadRows(P);
    abRows(P);
    abPresetRows(P);
    return P.finish();
}
