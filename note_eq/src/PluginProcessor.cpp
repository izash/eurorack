#include "PluginProcessor.h"
#include "PluginEditor.h"

using APVTS = juce::AudioProcessorValueTreeState;

APVTS::ParameterLayout NoteEQProcessor::createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    auto id = [] (const String& s) { return ParameterID { s, 1 }; };

    for (int i = 0; i < noteeq::kNumBands; ++i)
    {
        const String n (i + 1);
        p.push_back (std::make_unique<AudioParameterBool>  (id ("on" + n), "Band " + n + " On", true));
        p.push_back (std::make_unique<AudioParameterFloat> (id ("harm" + n), "Band " + n + " Harmonic",
                                                            NormalisableRange<float> (0.5f, 16.0f, 0.5f), (float) (i + 1)));
        p.push_back (std::make_unique<AudioParameterFloat> (id ("gain" + n), "Band " + n + " Gain",
                                                            NormalisableRange<float> (-18.0f, 18.0f, 0.1f), 0.0f, AudioParameterFloatAttributes().withLabel ("dB")));
        p.push_back (std::make_unique<AudioParameterFloat> (id ("q" + n), "Band " + n + " Q",
                                                            NormalisableRange<float> (0.3f, 20.0f, 0.01f, 0.4f), 4.0f));
    }
    p.push_back (std::make_unique<AudioParameterBool>  (id ("snap"), "Snap To Note", true));
    p.push_back (std::make_unique<AudioParameterFloat> (id ("glide"), "Glide",
                                                        NormalisableRange<float> (1.0f, 500.0f, 1.0f, 0.5f), 20.0f, AudioParameterFloatAttributes().withLabel ("ms")));
    p.push_back (std::make_unique<AudioParameterFloat> (id ("hold"), "Hold",
                                                        NormalisableRange<float> (0.0f, 2000.0f, 1.0f, 0.5f), 150.0f, AudioParameterFloatAttributes().withLabel ("ms")));
    p.push_back (std::make_unique<AudioParameterFloat> (id ("sens"), "Sensitivity",
                                                        NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.5f));
    p.push_back (std::make_unique<AudioParameterFloat> (id ("refa"), "Tuning A",
                                                        NormalisableRange<float> (415.0f, 466.0f, 0.1f), 440.0f, AudioParameterFloatAttributes().withLabel ("Hz")));
    p.push_back (std::make_unique<AudioParameterInt>   (id ("lownote"),  "Lowest Note",  16, 60, 28));  // E1
    p.push_back (std::make_unique<AudioParameterInt>   (id ("highnote"), "Highest Note", 40, 96, 79));  // G5
    p.push_back (std::make_unique<AudioParameterBool>  (id ("comp_on"), "Compressor", false));
    p.push_back (std::make_unique<AudioParameterFloat> (id ("comp_thr"), "Comp Threshold",
                                                        NormalisableRange<float> (-48.0f, 0.0f, 0.1f), -18.0f, AudioParameterFloatAttributes().withLabel ("dB")));
    p.push_back (std::make_unique<AudioParameterFloat> (id ("comp_ratio"), "Comp Ratio",
                                                        NormalisableRange<float> (1.5f, 20.0f, 0.1f, 0.5f), 4.0f));
    p.push_back (std::make_unique<AudioParameterFloat> (id ("comp_att"), "Comp Attack",
                                                        NormalisableRange<float> (0.1f, 100.0f, 0.1f, 0.4f), 10.0f, AudioParameterFloatAttributes().withLabel ("ms")));
    p.push_back (std::make_unique<AudioParameterFloat> (id ("comp_rel"), "Comp Release",
                                                        NormalisableRange<float> (10.0f, 1000.0f, 1.0f, 0.5f), 150.0f, AudioParameterFloatAttributes().withLabel ("ms")));
    p.push_back (std::make_unique<AudioParameterBool>  (id ("comp_auto"), "Comp Auto Gain", true));
    p.push_back (std::make_unique<AudioParameterFloat> (id ("out"), "Output",
                                                        NormalisableRange<float> (-18.0f, 18.0f, 0.1f), 0.0f, AudioParameterFloatAttributes().withLabel ("dB")));
    return { p.begin(), p.end() };
}

NoteEQProcessor::NoteEQProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
}

bool NoteEQProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
    return in == out && (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo());
}

noteeq::Params NoteEQProcessor::readParams() const
{
    noteeq::Params p;
    auto get = [this] (const juce::String& s) { return apvts.getRawParameterValue (s)->load(); };
    for (int i = 0; i < noteeq::kNumBands; ++i)
    {
        const juce::String n (i + 1);
        auto& b = p.bands[(size_t) i];
        b.enabled = get ("on" + n) > 0.5f;
        b.harmonic = get ("harm" + n);
        b.gainDb = get ("gain" + n);
        b.q = get ("q" + n);
    }
    p.snapToNote = get ("snap") > 0.5f;
    p.glideMs = get ("glide");
    p.holdMs = get ("hold");
    p.sensitivity = get ("sens");
    p.refA = get ("refa");
    p.minNote = get ("lownote");
    p.maxNote = juce::jmax (get ("highnote"), p.minNote + 12.0f);
    return p;
}

noteeq::Compressor::Params NoteEQProcessor::readCompParams() const
{
    auto get = [this] (const char* s) { return apvts.getRawParameterValue (s)->load(); };
    noteeq::Compressor::Params c;
    c.enabled = get ("comp_on") > 0.5f;
    c.thresholdDb = get ("comp_thr");
    c.ratio = get ("comp_ratio");
    c.attackMs = get ("comp_att");
    c.releaseMs = get ("comp_rel");
    c.autoGain = get ("comp_auto") > 0.5f;
    return c;
}

void NoteEQProcessor::prepareToPlay (double sr, int)
{
    sampleRate = sr;
    comp.prepare (sr);
    comp.setParams (readCompParams());
    const auto p = readParams();
    for (auto& e : engines) { e.setParams (p); e.prepare (sr); }
}

void NoteEQProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const auto p = readParams();
    const int nCh = juce::jmin (buffer.getNumChannels(), (int) engines.size());
    for (int ch = 0; ch < nCh; ++ch)
    {
        auto& e = engines[(size_t) ch];
        e.setParams (p);
        auto* d = buffer.getWritePointer (ch);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            d[i] = e.process (d[i]);
    }
    // output stage: compressor (reduction linked across channels) then output trim
    comp.setParams (readCompParams());
    const float outGain = std::pow (10.0f, apvts.getRawParameterValue ("out")->load() / 20.0f);
    const int n = buffer.getNumSamples();
    for (int i = 0; i < n; ++i)
    {
        float a = 0.0f;
        for (int ch = 0; ch < nCh; ++ch) a = juce::jmax (a, std::abs (buffer.getReadPointer (ch)[i]));
        const float g = comp.gain (a) * outGain;
        for (int ch = 0; ch < nCh; ++ch) buffer.getWritePointer (ch)[i] *= g;
    }
    shownGainReduction.store (comp.gainReductionDb());

    shownNote.store (engines[0].isTracking() ? engines[0].currentNote() : -1.0f);
    shownHz.store (engines[0].isTracking() ? engines[0].currentFrequency() : 0.0f);
}

juce::AudioProcessorEditor* NoteEQProcessor::createEditor() { return new NoteEQEditor (*this); }

void NoteEQProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, dest);
}
void NoteEQProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType())) apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new NoteEQProcessor(); }
