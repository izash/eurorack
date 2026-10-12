#pragma once
#include "PluginProcessor.h"

class NoteEQEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit NoteEQEditor (NoteEQProcessor&);
    ~NoteEQEditor() override = default;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void drawResponse (juce::Graphics&, juce::Rectangle<float>);

    struct Column
    {
        juce::ToggleButton on;
        juce::Slider harm, gain, q;
        juce::Label harmL, gainL, qL;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> onA;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> harmA, gainA, qA;
    };

    NoteEQProcessor& proc;
    std::array<Column, noteeq::kNumBands> cols;

    juce::ToggleButton snap { "Snap to note" };
    juce::ToggleButton compOn { "Compressor" }, compAuto { "Auto gain" };
    juce::Slider compThr, compRatio, compAtt, compRel;
    juce::Label compThrL, compRatioL, compAttL, compRelL;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> compOnA, compAutoA;
    juce::Rectangle<float> meterArea;
    float grDb = 0.0f;
    juce::Slider glide, hold, sens, refA, lowNote, highNote, outGain;
    juce::Label glideL, holdL, sensL, refAL, lowL, highL, outL;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> sAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> snapA;

    juce::Rectangle<float> graphArea;
    float note = -1.0f, hz = 0.0f;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NoteEQEditor)
};
