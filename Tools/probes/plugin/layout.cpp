// FCMP_PROBE layer=proc name=layout scope=global timeout=60
//
// proc.layout (P1, S7; 03 §3.5; 01 §3.1-3.3; K2 #7, #9, #25a-b, #25d; B §7.4.7): the processor's buses and its 29
// host parameters against the v1 table (kHostParams in kApvtsOrder). Scheduled with P1, not S0 (K3 #23).
//
// Rows (spec, exact unless stated):
//   layout.bus.<in>-<out>.<sc>.supported    isBusesLayoutSupported for main {mono, stereo} x {mono, stereo} x side
//                                           chain {off, mono, stereo}: accepted exactly for 1->1, 1->2, 2->2 (2->1
//                                           rejected), any side chain
//   layout.bus.rejected.<case>              a quad main, a quad side chain, a disabled main, a third input bus: 0
//   layout.bus.apply_failures               setBusesLayout of every accepted layout succeeds and reads back: 0
//   layout.params.count                     29
//   layout.params.order_mismatches          getParameters()[i] is kHostParams[kApvtsOrder[i]] by ID: 0 (K2 #9)
//   layout.params.name_mismatches           the universal names (K2 #25b): 0
//   layout.params.label_nonempty            JUCE labels that are not "" (K2 #25a): 0
//   layout.params.version_mismatches        ParameterID version hints: 0
//   layout.params.automatable_mismatches    kHostParams automatable (mode, extkey, listen, delta, quality, labudget
//                                           are not): 0
//   layout.params.type_mismatches           Float for continuous maps, Int for index maps, Choice for quality and
//                                           labudget, Bool for boolean maps; isDiscrete/isBoolean to match: 0
//   layout.params.steps_mismatches          getNumSteps(): numSteps for discrete, JUCE's continuous default else: 0
//   layout.params.range_mismatches          NormalisableRange start/end == kHostParams lo/hi: 0
//   layout.params.map_mismatches            convertFrom0to1(v) == fcdsp::toPlain(pid, v) and convertTo0to1(plain) ==
//                                           toNorm(pid, plain) on a 1/256 grid (Int/Choice off their exact .5 ties,
//                                           where JUCE's roundToInt rounds to even); the switches keep the position v
//                                           (ParamLayout.cpp SwitchParameter) and read ON exactly where toPlain does: 0
//   layout.params.default_mismatches        convertFrom0to1(getDefaultValue()) and the APVTS raw value of a fresh
//                                           instance == kHostParams def within absrel(1e-6, 1e-6) (host maps use libm,
//                                           K2 #14): 0
//   layout.programs                         getNumPrograms() == 1 (K2 #25d)
//   layout.bypass_parameter                 getBypassParameter() is the `bypass` parameter: 1
//   layout.undo_manager_null                the APVTS has no UndoManager (K2 #7): 1
//   layout.ports_mismatches                 port(pid) wraps the parameter of that Pid (native(), id()): 0
//   layout.midi                             acceptsMidi/producesMidi/isMidiEffect all false: 0 set
// Golden: layout.params.hash, an FNV-1a over the layout's libm-free facts in APVTS order (ID, name, label, version,
// type, steps, flags, range ends, kHostParams default, choices), exact.
#include "ProbeRegistry.h"

#include "plugin/Processor.h"

#include "fcdsp/params/HostParams.h"
#include "fcdsp/params/Pid.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace
{
    using fcdsp::HostParam;
    using fcdsp::Map;
    using fcdsp::Pid;
    using funkgui::test::Probe;

    using Set = juce::AudioChannelSet;

    const char* setName(const Set& s)
    {
        if (s.isDisabled())
            return "off";
        if (s == Set::mono())
            return "mono";
        if (s == Set::stereo())
            return "stereo";
        return "other";
    }

    juce::AudioProcessor::BusesLayout layoutOf(const Set& in, const Set& out, const Set& sc, bool withSc = true)
    {
        juce::AudioProcessor::BusesLayout l;
        l.inputBuses.add(in);
        if (withSc)
            l.inputBuses.add(sc);
        l.outputBuses.add(out);
        return l;
    }

    enum class Type : std::uint8_t { floating, integer, choice, boolean, unknown };

    Type typeOf(const juce::RangedAudioParameter& p)
    {
        if (dynamic_cast<const juce::AudioParameterFloat*>(&p) != nullptr)
            return Type::floating;
        if (dynamic_cast<const juce::AudioParameterInt*>(&p) != nullptr)
            return Type::integer;
        if (dynamic_cast<const juce::AudioParameterChoice*>(&p) != nullptr)
            return Type::choice;
        if (dynamic_cast<const juce::AudioParameterBool*>(&p) != nullptr)
            return Type::boolean;
        return Type::unknown;
    }

    Type expectedType(const HostParam& h)
    {
        switch (h.map)
        {
            case Map::index:   return h.choices != nullptr ? Type::choice : Type::integer;
            case Map::boolean: return Type::boolean;
            case Map::linear:
            case Map::log:
            case Map::power:
            case Map::ratio3:  break;
        }
        return Type::floating;
    }

    bool nearAbsRel(double got, double want, double a, double r)
    {
        return std::fabs(got - want) <= std::max(a, r * std::fabs(want));
    }

    // True when v*(n-1) is (within 1e-6 of) an exact .5 tie: JUCE's roundToInt rounds those to even, std::round away.
    bool atTie(double v, int numSteps)
    {
        const double x = v * static_cast<double>(numSteps - 1);
        return std::fabs(x - std::floor(x) - 0.5) < 1e-6;
    }

    struct Hasher
    {
        std::uint64_t h = 1469598103934665603ull;
        void bytes(const void* p, std::size_t n) { h = funkgui::test::fnv1a(p, n, h); }
        void str(const juce::String& s)
        {
            const char* u = s.toRawUTF8();
            bytes(u, std::strlen(u) + 1);                           // with the terminator: fields never run together
        }
        void i64(std::int64_t v) { bytes(&v, sizeof v); }
        void f32(float v)
        {
            std::uint32_t b = 0;
            std::memcpy(&b, &v, sizeof b);
            bytes(&b, sizeof b);
        }
    };
} // namespace

FCMP_PROBE(proc, layout)
{
    const juce::ScopedJuceInitialiser_GUI juceInit;              // the processor's SetupWatcher is a juce::Timer
    auto proc = std::make_unique<fcmp::Processor>();

    // ---- buses ----------------------------------------------------------------------------------------------------
    const Set mains[2] = { Set::mono(), Set::stereo() };
    const Set scs[3] = { Set::disabled(), Set::mono(), Set::stereo() };
    std::int64_t applyFailures = 0;
    for (const Set& in : mains)
        for (const Set& out : mains)
            for (const Set& sc : scs)
            {
                const auto l = layoutOf(in, out, sc);
                const bool want = !(in == Set::stereo() && out == Set::mono());
                const bool got = proc->checkBusesLayoutSupported(l);
                P.eq(std::string("layout.bus.") + setName(in) + "-" + setName(out) + "." + setName(sc) + ".supported",
                     got ? 1 : 0, want ? 1 : 0);
                if (want)
                {
                    const bool applied = proc->setBusesLayout(l) && proc->getBusesLayout() == l;
                    applyFailures += applied ? 0 : 1;
                }
            }
    P.eq("layout.bus.apply_failures", applyFailures, 0);
    P.eq("layout.bus.rejected.quad_main", proc->checkBusesLayoutSupported(layoutOf(Set::quadraphonic(),
                                                                                  Set::quadraphonic(), Set::disabled()))
                                              ? 1 : 0, 0);
    P.eq("layout.bus.rejected.quad_sc", proc->checkBusesLayoutSupported(layoutOf(Set::stereo(), Set::stereo(),
                                                                                Set::quadraphonic()))
                                            ? 1 : 0, 0);
    P.eq("layout.bus.rejected.disabled_main", proc->checkBusesLayoutSupported(layoutOf(Set::disabled(), Set::stereo(),
                                                                                      Set::disabled()))
                                                  ? 1 : 0, 0);
    {
        auto three = layoutOf(Set::stereo(), Set::stereo(), Set::stereo());
        three.inputBuses.add(Set::stereo());
        P.eq("layout.bus.rejected.third_input", proc->checkBusesLayoutSupported(three) ? 1 : 0, 0);
    }
    proc->setBusesLayout(layoutOf(Set::stereo(), Set::stereo(), Set::disabled()));

    // ---- parameters -----------------------------------------------------------------------------------------------
    const juce::Array<juce::AudioProcessorParameter*>& all = proc->getParameters();
    P.eq("layout.params.count", all.size(), static_cast<std::int64_t>(fcdsp::kNumParams));

    std::int64_t order = 0, names = 0, labels = 0, versions = 0, automatable = 0, types = 0, steps = 0, ranges = 0,
                 maps = 0, defaults = 0, ports = 0;
    Hasher hash;
    const int n = std::min(all.size(), static_cast<int>(fcdsp::kNumParams));
    for (int i = 0; i < n; ++i)
    {
        const HostParam& h = fcdsp::kHostParams[fcdsp::idx(fcdsp::kApvtsOrder[static_cast<std::size_t>(i)])];
        auto* p = dynamic_cast<juce::RangedAudioParameter*>(all[i]);
        if (p == nullptr || p->getParameterID() != juce::String(h.id))
        {
            ++order;
            std::printf("NOTE     layout: APVTS index %d is '%s', want '%s'\n", i,
                        p != nullptr ? p->getParameterID().toRawUTF8() : "?", h.id);
            continue;
        }
        const Pid pid = h.pid;
        names += p->getName(1024) == juce::String(h.name) ? 0 : 1;
        labels += p->getLabel().isEmpty() ? 0 : 1;
        versions += p->getVersionHint() == h.versionHint ? 0 : 1;
        automatable += p->isAutomatable() == h.automatable ? 0 : 1;

        const Type t = typeOf(*p);
        const bool discrete = t == Type::integer || t == Type::choice || t == Type::boolean;   // a stepped map
        const bool juceDiscrete = t == Type::choice || t == Type::boolean;   // JUCE's Int is not isDiscrete()
        types += t == expectedType(h) && p->isDiscrete() == juceDiscrete && p->isBoolean() == (t == Type::boolean)
                     ? 0 : 1;
        const int wantSteps = discrete ? h.numSteps : juce::AudioProcessor::getDefaultNumParameterSteps();
        steps += p->getNumSteps() == wantSteps ? 0 : 1;

        const auto& range = p->getNormalisableRange();
        ranges += range.start == h.lo && range.end == h.hi ? 0 : 1;

        for (int k = 0; k <= 256; ++k)
        {
            const double v = k / 256.0;
            if (discrete && h.map != Map::boolean && atTie(v, h.numSteps))
                continue;
            const float vf = static_cast<float>(v);
            const float plain = p->convertFrom0to1(vf);
            // A switch keeps the host's position as its raw value (ParamLayout.cpp SwitchParameter); its readers
            // apply toPlain's boolean map (>= 0.5) on read.
            const float want = h.map == Map::boolean ? vf : fcdsp::toPlain(pid, vf);
            if (h.map == Map::boolean && (plain >= 0.5f) != (fcdsp::toPlain(pid, vf) >= 0.5f))
                ++maps;
            if (plain != want)
            {
                if (maps < 5)
                    std::printf("NOTE     layout: %s convertFrom0to1(%.6f) = %.9g, toPlain = %.9g\n", h.id, v,
                                static_cast<double>(plain), static_cast<double>(want));
                ++maps;
            }
            const float back = p->convertTo0to1(want);
            const float wantBack = h.map == Map::boolean ? want : fcdsp::toNorm(pid, want);
            if ((!discrete || h.map == Map::boolean) && back != wantBack)
                ++maps;
        }

        const double def = static_cast<double>(p->convertFrom0to1(p->getDefaultValue()));
        const double raw = static_cast<double>(proc->rawValue(pid));
        const bool defOk = nearAbsRel(def, h.def, 1e-6, 1e-6) && nearAbsRel(raw, h.def, 1e-6, 1e-6);
        if (!defOk)
            std::printf("NOTE     layout: %s default %.9g, raw %.9g, want %.9g\n", h.id, def, raw,
                        static_cast<double>(h.def));
        defaults += defOk ? 0 : 1;
        if (static_cast<double>(h.def) != raw)
            std::printf("NOTE     layout: %s fresh raw default %.9g differs from the table's %.9g by the host-map "
                        "round trip (K2 #14)\n", h.id, raw, static_cast<double>(h.def));

        funkgui::ParamPort& port = proc->port(pid);
        ports += port.native() == static_cast<void*>(p) && juce::String(port.id()) == juce::String(h.id) ? 0 : 1;

        hash.str(p->getParameterID());
        hash.str(p->getName(1024));
        hash.str(p->getLabel());
        hash.i64(p->getVersionHint());
        hash.i64(static_cast<std::int64_t>(t));
        hash.i64(p->getNumSteps());
        hash.i64((p->isAutomatable() ? 1 : 0) | (p->isDiscrete() ? 2 : 0) | (p->isBoolean() ? 4 : 0)
                 | (p->isMetaParameter() ? 8 : 0));
        hash.f32(range.start);
        hash.f32(range.end);
        hash.f32(h.def);
        if (auto* c = dynamic_cast<juce::AudioParameterChoice*>(p))
            for (const juce::String& s : c->choices)
                hash.str(s);
    }
    P.eq("layout.params.order_mismatches", order, 0);
    P.eq("layout.params.name_mismatches", names, 0);
    P.eq("layout.params.label_nonempty", labels, 0);
    P.eq("layout.params.version_mismatches", versions, 0);
    P.eq("layout.params.automatable_mismatches", automatable, 0);
    P.eq("layout.params.type_mismatches", types, 0);
    P.eq("layout.params.steps_mismatches", steps, 0);
    P.eq("layout.params.range_mismatches", ranges, 0);
    P.eq("layout.params.map_mismatches", maps, 0);
    P.eq("layout.params.default_mismatches", defaults, 0);
    P.eq("layout.ports_mismatches", ports, 0);

    P.eq("layout.programs", proc->getNumPrograms(), 1);
    P.eq("layout.bypass_parameter", proc->getBypassParameter() == &proc->parameter(Pid::bypass) ? 1 : 0, 1);
    P.eq("layout.undo_manager_null", proc->apvts().undoManager == nullptr ? 1 : 0, 1);
    P.eq("layout.midi", (proc->acceptsMidi() ? 1 : 0) + (proc->producesMidi() ? 1 : 0) + (proc->isMidiEffect() ? 1 : 0),
         0);

    P.hash("layout.params.hash", hash.h);
    return P.finish();
}
