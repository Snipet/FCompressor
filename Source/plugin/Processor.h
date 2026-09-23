// Source/plugin/Processor.h: the FCompressor audio processor (namespace fcmp).
//
// Sprint 0 stub (card B0): a passthrough with the final bus layout and no parameters, so the build, the host
// validation (Scripts/validate.sh: auval -strict, pluginval 10) and the probe plumbing run end to end before any DSP
// exists. P1 (S7) replaces it with the real processor (EngineHost, SetupWatcher, ProcessorFacade, the parameter
// layout) and owns this file from then on.
//
// Buses (proc.layout, 03 §3.5): main in -> out 1->1, 1->2 and 2->2 are accepted, 2->1 is rejected; the side-chain
// input is optional (disabled, mono or stereo). getNumPrograms() == 1 (K2 #25d). No parameters and no listeners
// (K2 #6). Product constants come from the generated FcmpProduct.h, never JucePlugin_*.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace fcmp
{
    class Processor final : public juce::AudioProcessor
    {
    public:
        Processor();
        ~Processor() override;

        const juce::String getName() const override;
        bool acceptsMidi() const override;
        bool producesMidi() const override;
        bool isMidiEffect() const override;
        double getTailLengthSeconds() const override;

        bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
        void prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) override;
        void releaseResources() override;
        void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
        void processBlockBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
        using juce::AudioProcessor::processBlock;               // the double-precision overloads keep JUCE's defaults
        using juce::AudioProcessor::processBlockBypassed;

        bool hasEditor() const override;
        juce::AudioProcessorEditor* createEditor() override;

        int getNumPrograms() override;
        int getCurrentProgram() override;
        void setCurrentProgram(int index) override;
        const juce::String getProgramName(int index) override;
        void changeProgramName(int index, const juce::String& newName) override;

        void getStateInformation(juce::MemoryBlock& destData) override;
        void setStateInformation(const void* data, int sizeInBytes) override;

    private:
        // Main input copied to the main output: in place for 1->1 and 2->2; for 1->2 the input also feeds the right
        // channel. The side-chain input is never heard. Real-time safe: no allocation, no lock.
        void passThrough(juce::AudioBuffer<float>& buffer);

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Processor)
    };

    // The editor factory. Exactly one of Source/plugin/CreateEditorGpu.cpp and CreateEditorGeneric.cpp is compiled into
    // each target, chosen by cmake/FcmpSources.cmake (no #if; SPRINTS §7 D22).
    juce::AudioProcessorEditor* createEditor(Processor& processor);
} // namespace fcmp
