#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "NoteEQ.h"
#include <array>
#include <atomic>

class NoteEQProcessor : public juce::AudioProcessor
{
public:
    NoteEQProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Note EQ"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;

    // read by the editor (message thread)
    std::atomic<float> shownNote { -1.0f };   // MIDI note, <0 = none
    std::atomic<float> shownHz   { 0.0f };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    noteeq::Params readParams() const;

    std::array<noteeq::NoteEQ, 2> engines;   // one per channel (each tracks its own pitch)
    double sampleRate = 48000.0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NoteEQProcessor)
};
