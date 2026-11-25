#pragma once

#include "PluginProcessor.h"
#include <array>
#include <memory>
#include <vector>

class TopVoxLookAndFeel : public juce::LookAndFeel_V4
{
public:
    TopVoxLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                           juce::Slider&) override;
};

class TopVoxEditor : public juce::AudioProcessorEditor,
                     private juce::Timer
{
public:
    explicit TopVoxEditor (TopVoxProcessor&);
    ~TopVoxEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void drawMeter (juce::Graphics&, juce::Rectangle<int> area, const juce::String& title,
                    float value01, bool fromTop);

    TopVoxProcessor& proc;
    TopVoxLookAndFeel laf;

    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };
    std::array<Knob, topvox::kNumParams> knobs;

    struct Section
    {
        juce::String title;
        std::vector<int> ids;
        juce::Rectangle<int> bounds;
    };
    std::vector<Section> sections;

    juce::ComboBox styleBox;
    juce::TextButton presetButton, autoButton, resetButton;

    juce::Rectangle<int> analysisArea, metersArea;
    juce::String statusMessage;

    float meterIn = 0.f, meterOut = 0.f, meterGr = 0.f;
    topvox::VoiceReport report;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TopVoxEditor)
};
