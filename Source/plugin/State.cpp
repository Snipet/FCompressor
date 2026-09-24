// Source/plugin/State.cpp: session state, P1's APVTS-only body (S7). P2 (S8) replaces it with the full 01 §9.1
// format: stateVersion, modeId and modeRev on the <PARAMS> root, the migrations table, StateNotice, the <UI> child.
//
// What this body does (the parts every later body keeps; see State.h):
//   save  <PARAMS product build> + one <PARAM id value/> per host parameter in plain units (APVTS copyState, which
//         flushes the parameter values first), the <PRESET> hook if installed, copyXmlToBinary.
//   load  a <PARAMS> tree only; inside ONE batch (K2 #23): replaceState with the <PRESET> and <UI> children stripped,
//         every parameter absent from the saved tree back to its host default (HR's loop: replaceState leaves an absent
//         parameter at its live value, so a non-fresh instance would keep it), listen and delta reset to 0 (K2 #25c:
//         a session never reopens monitoring the side chain), the <PRESET> hook if installed; endBatch raises the
//         engine snap after the last write; StateNotice::serial bumps.
#include "plugin/State.h"

#include "FcmpProduct.h"

#include "fcdsp/params/HostParams.h"
#include "fcdsp/params/Pid.h"

#include <array>
#include <cstddef>

namespace fcmp
{
    namespace
    {
        void setToDefault(juce::AudioProcessorValueTreeState& apvts, const char* id)
        {
            if (juce::RangedAudioParameter* p = apvts.getParameter(id))
                p->setValueNotifyingHost(p->getDefaultValue());
        }
    } // namespace

    void saveState(const StateContext& ctx, juce::MemoryBlock& destData)
    {
        juce::ValueTree tree = ctx.apvts.copyState();
        tree.setProperty("product", product::kName, nullptr);
        tree.setProperty("build", product::kVersion, nullptr);
        if (ctx.hooks.writePreset)
            ctx.hooks.writePreset(tree);
        if (const std::unique_ptr<juce::XmlElement> xml = tree.createXml())
            juce::AudioProcessor::copyXmlToBinary(*xml, destData);
    }

    void loadState(const StateContext& ctx, const void* data, int sizeInBytes)
    {
        if (data == nullptr || sizeInBytes <= 0)
            return;
        const std::unique_ptr<juce::XmlElement> xml = juce::AudioProcessor::getXmlFromBinary(data, sizeInBytes);
        if (xml == nullptr || !xml->hasTagName(ctx.apvts.state.getType().toString()))
            return;                                               // not ours: ignore (fail-safe)
        juce::ValueTree tree = juce::ValueTree::fromXml(*xml);
        if (!tree.isValid())
            return;

        const juce::Identifier paramType("PARAM"), presetType("PRESET"), uiType("UI"), idProperty("id");
        juce::ValueTree params = tree.createCopy();
        for (int i = params.getNumChildren(); --i >= 0;)
        {
            const juce::Identifier type = params.getChild(i).getType();
            if (type == presetType || type == uiType)
                params.removeChild(i, nullptr);
        }
        // Presence is read before replaceState, which appends a child for every parameter the tree lacks.
        std::array<bool, fcdsp::kNumParams> present{};
        for (std::size_t i = 0; i < fcdsp::kNumParams; ++i)
            present[i] = params.getChildWithProperty(idProperty, juce::String(fcdsp::kHostParams[i].id)).hasType(paramType);

        ctx.facade.beginBatch();
        ctx.apvts.replaceState(params);
        for (std::size_t i = 0; i < fcdsp::kNumParams; ++i)
        {
            const fcdsp::HostParam& h = fcdsp::kHostParams[i];
            if (!present[i] || h.pid == fcdsp::Pid::listen || h.pid == fcdsp::Pid::delta)
                setToDefault(ctx.apvts, h.id);                    // listen and delta default to 0
        }
        if (ctx.hooks.readPreset)
            ctx.hooks.readPreset(tree);
        ctx.facade.endBatch();
        ++ctx.notice.serial;
    }
} // namespace fcmp
