// Source/plugin/Processor.cpp: the Sprint 0 passthrough stub (card B0). See Processor.h.
#include "plugin/Processor.h"

#include "FcmpProduct.h"

#include <algorithm>

// The plugin wrappers' factory (declared by JUCE only inside juce_audio_plugin_client).
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

namespace fcmp
{
    Processor::Processor()
        : juce::AudioProcessor(BusesProperties()
                                   .withInput("Input", juce::AudioChannelSet::stereo(), true)
                                   .withOutput("Output", juce::AudioChannelSet::stereo(), true)
                                   .withInput("Sidechain", juce::AudioChannelSet::stereo(), false))
    {
    }

    Processor::~Processor() = default;

    const juce::String Processor::getName() const { return product::kName; }
    bool Processor::acceptsMidi() const { return false; }
    bool Processor::producesMidi() const { return false; }
    bool Processor::isMidiEffect() const { return false; }
    double Processor::getTailLengthSeconds() const { return 0.0; }

    bool Processor::isBusesLayoutSupported(const BusesLayout& layouts) const
    {
        const auto mono = juce::AudioChannelSet::mono();
        const auto stereo = juce::AudioChannelSet::stereo();
        const auto in = layouts.getMainInputChannelSet();
        const auto out = layouts.getMainOutputChannelSet();
        const bool mainOk = (in == mono && out == mono) || (in == mono && out == stereo) || (in == stereo && out == stereo);
        if (!mainOk || layouts.outputBuses.size() != 1 || layouts.inputBuses.size() > 2)
            return false;
        if (layouts.inputBuses.size() == 2)
        {
            const auto sidechain = layouts.getChannelSet(true, 1);
            if (!(sidechain.isDisabled() || sidechain == mono || sidechain == stereo))
                return false;
        }
        return true;
    }

    void Processor::prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock)
    {
        juce::ignoreUnused(sampleRate, maximumExpectedSamplesPerBlock);
    }

    void Processor::releaseResources() {}

    void Processor::passThrough(juce::AudioBuffer<float>& buffer)
    {
        const int channels = buffer.getNumChannels();
        const int numIn = std::min(getMainBusNumInputChannels(), channels);
        const int numOut = std::min(getMainBusNumOutputChannels(), channels);
        const int n = buffer.getNumSamples();
        // Main input and main output share channels 0..; anything above the main input channels that is an output is
        // not input data (for 1->2 it holds side-chain samples), so it is replaced: the mono input feeds it, and any
        // other extra output (none with the accepted layouts) is cleared.
        for (int ch = numIn; ch < numOut; ++ch)
        {
            if (numIn == 1)
                buffer.copyFrom(ch, 0, buffer, 0, 0, n);
            else
                buffer.clear(ch, 0, n);
        }
    }

    void Processor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
    {
        juce::ignoreUnused(midi);
        const juce::ScopedNoDenormals noDenormals;
        passThrough(buffer);
    }

    void Processor::processBlockBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
    {
        juce::ignoreUnused(midi);
        passThrough(buffer);                                      // no latency: bypass is the same passthrough
    }

    bool Processor::hasEditor() const { return true; }
    juce::AudioProcessorEditor* Processor::createEditor() { return fcmp::createEditor(*this); }

    // One program: FCompressor's presets live in its own browser, and a program list would make JUCE's VST3 wrapper add
    // a "Program" parameter whose step count moves with the bank (K2 #25d).
    int Processor::getNumPrograms() { return 1; }
    int Processor::getCurrentProgram() { return 0; }
    void Processor::setCurrentProgram(int index) { juce::ignoreUnused(index); }
    const juce::String Processor::getProgramName(int index)
    {
        juce::ignoreUnused(index);
        return "Default";
    }
    void Processor::changeProgramName(int index, const juce::String& newName) { juce::ignoreUnused(index, newName); }

    // The stub has no state: it writes an empty PARAMS tree with its product and build (the 01 §9.1 root; P2 owns the
    // real format) and restores nothing.
    void Processor::getStateInformation(juce::MemoryBlock& destData)
    {
        juce::XmlElement xml("PARAMS");
        xml.setAttribute("product", product::kName);
        xml.setAttribute("build", product::kVersion);
        copyXmlToBinary(xml, destData);
    }

    void Processor::setStateInformation(const void* data, int sizeInBytes) { juce::ignoreUnused(data, sizeInBytes); }
} // namespace fcmp

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new fcmp::Processor();
}
