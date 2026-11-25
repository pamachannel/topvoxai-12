#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <vector>

#include "VoiceDSP.h"

class TopVoxProcessor : public juce::AudioProcessor
{
public:
    TopVoxProcessor();
    ~TopVoxProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "TOPVOX VOICE"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 3.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // --- appelées depuis l'interface (thread message) ---
    void applyParams (const topvox::Params& p);
    void applyStyle (int style);
    bool runAutoMix (int style);          // false si la voix analysée est insuffisante
    topvox::VoiceReport getReport() const { return analyzer.report(); }
    void resetAnalysis() { analyzer.requestReset(); }

    juce::AudioProcessorValueTreeState apvts;
    topvox::VoiceChain chain;
    topvox::VoiceAnalyzer analyzer;

private:
    std::array<std::atomic<float>*, topvox::kNumParams> raw {};
    std::vector<float> scratchR;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TopVoxProcessor)
};
