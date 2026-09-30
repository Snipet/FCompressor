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
//                    was current; redo makes the preset current again, unmodified; after an edit and a Save As, undo
//                    goes back to the factory preset and redo to the saved one, unmodified (the store is the test's
//                    sandbox, $FCMP_PRESETS_DB)
//   capacity         150 edits leave exactly EditHistory::kCapacity undos
//   load.*           a state load clears the history
//   ab.*             B starts as a copy of A; an edit in B is left behind when A is selected and comes back with B; the
//                    switch is an undo step; copySlot fills the other slot; each slot keeps its preset; the inactive
//                    slot survives a save and a load into another instance, also when that instance saves at once
//                    (no editor ever asked it anything); a load without <COMPARE> leaves B unused, and a save right
//                    after it writes none
//   load.burst       a gesture open across a load (a wheel burst) records nothing, even after the editor's tick has seen
//                    the load
//   ramp.*           what undo and an A/B switch write ramps like an edit (no engine snap): a 1 kHz sine at −30 dBFS,
//                    OUTPUT −18 dB by a gesture, then undo: the first millisecond after it is within 3 dB of the level
//                    before, and 0.5 s later the level is 18 dB up; the same for switching to a slot 18 dB louder
#include "ProbeRegistry.h"

#include "plugin/EditHistory.h"
#include "plugin/Processor.h"
#include "plugin/ProcessorFacade.h"

#include "fcdsp/params/HostParams.h"
#include "fcdsp/params/Pid.h"

#include <funkgui/params/ParamPort.h>

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdlib>
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

        // Save As names the live sound: undo goes back to the factory preset, redo to the saved one.
        const char* sandbox = std::getenv("FCMP_PRESETS_DB");
        P.eq("preset.save_as.sandboxed", b(sandbox != nullptr && *sandbox != '\0'), 1);
        if (sandbox == nullptr || *sandbox == '\0')
            return;                                              // never into the user's own presets
        gesture(*p, Pid::thr, -14.0f);
        const float thrSaved = p->rawValue(Pid::thr);
        const bool saved = pr.saveAs("History Probe", "");
        const std::string savedUuid = p->presets().currentUuid();
        h.undo();
        P.eq("preset.save_as.undo_factory", b(saved && pr.current() == target && !pr.modified()), 1);
        h.redo();
        P.eq("preset.save_as.redo_saved", b(p->presets().currentUuid() == savedUuid && !pr.modified()
                                            && same(p->rawValue(Pid::thr), thrSaved)), 1);
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

        // A load, then a save at once (a host duplicating the track, or autosaving a project whose editor stayed shut).
        auto d = fresh();
        d->setStateInformation(blob.getData(), static_cast<int>(blob.getSize()));
        juce::MemoryBlock again;
        d->getStateInformation(again);
        auto e = fresh();
        e->setStateInformation(again.getData(), static_cast<int>(again.getSize()));
        fcmp::EditAccess& k = e->edits();
        const bool used = k.compareSlot() == 0 && k.slotUsed(1);
        k.selectSlot(1);
        P.eq("ab.session.save_at_once", b(used && same(e->rawValue(Pid::thr), bv)), 1);

        // A session without <COMPARE> over an instance whose B was used: B is unused, and a save at once writes none.
        juce::MemoryBlock plain;
        fresh()->getStateInformation(plain);
        d->setStateInformation(plain.getData(), static_cast<int>(plain.getSize()));
        juce::MemoryBlock resaved;
        d->getStateInformation(resaved);
        auto f = fresh();
        f->setStateInformation(resaved.getData(), static_cast<int>(resaved.getSize()));
        P.eq("ab.session.none_after_plain_load", b(!d->edits().slotUsed(1) && d->edits().compareSlot() == 0
                                                   && !f->edits().slotUsed(1)), 1);
    }

    void burstRows(Probe& P)
    {
        auto p = fresh();
        juce::MemoryBlock blob;
        {
            auto src = fresh();                                  // a session that sounds different
            gesture(*src, Pid::knee, 18.0f);
            src->getStateInformation(blob);
        }
        funkgui::ParamPort& port = p->port(Pid::thr);
        port.beginGesture();                                     // a wheel burst, still open
        port.setValue01(fcdsp::toNorm(Pid::thr, -22.0f));
        p->setStateInformation(blob.getData(), static_cast<int>(blob.getSize()));
        (void) p->edits().canUndo();                             // the editor's next tick sees the load
        port.endGesture();
        P.eq("load.burst", b(!p->edits().canUndo()), 1);
    }

    // A steady 1 kHz sine at −30 dBFS through a prepared processor, block by block; the peak of a stretch of the last
    // block's output.
    struct Tone
    {
        static constexpr double kFs = 48000.0;
        static constexpr int    kBlock = 256;
        explicit Tone(fcmp::Processor& p) : proc(p), buf(2, kBlock) { proc.prepareToPlay(kFs, kBlock); }
        void run(int blocks)
        {
            constexpr double kStep = 2.0 * 3.14159265358979323846 * 1000.0 / kFs;
            for (int k = 0; k < blocks; ++k)
            {
                for (int n = 0; n < kBlock; ++n)
                {
                    const auto x = static_cast<float>(0.0316227766 * std::sin(phase));
                    phase += kStep;
                    buf.setSample(0, n, x);
                    buf.setSample(1, n, x);
                }
                proc.processBlock(buf, midi);
            }
        }
        double peak(int from, int n) const
        {
            float m = 0.0f;
            for (int c = 0; c < 2; ++c)
                for (int i = from; i < from + n; ++i)
                    m = std::max(m, std::abs(buf.getSample(c, i)));
            return static_cast<double>(m);
        }
        static double db(double ratio) { return 20.0 * std::log10(std::max(ratio, 1.0e-12)); }

        fcmp::Processor&         proc;
        juce::AudioBuffer<float> buf;
        juce::MidiBuffer         midi;
        double                   phase = 0.0;
    };

    void rampRows(Probe& P)
    {
        constexpr int kMs = 48;                                  // one millisecond: one period
        {
            auto p = fresh();
            Tone t(*p);
            t.run(40);
            gesture(*p, Pid::output, -18.0f);
            t.run(40);
            const double low = t.peak(Tone::kBlock - kMs, kMs);
            p->edits().undo();
            t.run(1);
            P.le("ramp.undo.first_ms_step_db", std::abs(Tone::db(t.peak(0, kMs) / low)), 3.0);
            t.run(94);
            P.near("ramp.undo.lands_db", Tone::db(t.peak(Tone::kBlock - kMs, kMs) / low), 18.0, 0.5);
        }
        {
            auto p = fresh();
            fcmp::EditAccess& h = p->edits();
            Tone t(*p);
            gesture(*p, Pid::output, -18.0f);                    // A is 18 dB down ...
            h.selectSlot(1);
            gesture(*p, Pid::output, 0.0f);                      // ... B is not
            h.selectSlot(0);
            t.run(80);
            const double low = t.peak(Tone::kBlock - kMs, kMs);
            h.selectSlot(1);
            t.run(1);
            P.le("ramp.ab.first_ms_step_db", std::abs(Tone::db(t.peak(0, kMs) / low)), 3.0);
            t.run(94);
            P.near("ramp.ab.lands_db", Tone::db(t.peak(Tone::kBlock - kMs, kMs) / low), 18.0, 0.5);
        }
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
    burstRows(P);
    rampRows(P);
    return P.finish();
}
