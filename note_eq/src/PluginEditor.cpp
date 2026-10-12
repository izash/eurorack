#include "PluginEditor.h"
#include <complex>

static juce::String noteName (float midi)
{
    static const char* names[] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
    const int n = (int) std::lround (midi);
    return juce::String (names[((n % 12) + 12) % 12]) + juce::String (n / 12 - 1);
}

static void setupKnob (juce::Component& parent, juce::Slider& s, juce::Label& l, const juce::String& text,
                       juce::AudioProcessorValueTreeState& apvts, const juce::String& id,
                       std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>& att)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 62, 16);
    l.setText (text, juce::dontSendNotification);
    l.setJustificationType (juce::Justification::centred);
    parent.addAndMakeVisible (s);
    parent.addAndMakeVisible (l);
    att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, id, s);
}

NoteEQEditor::NoteEQEditor (NoteEQProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    for (int i = 0; i < noteeq::kNumBands; ++i)
    {
        auto& c = cols[(size_t) i];
        const juce::String n (i + 1);
        c.on.setButtonText ("Band " + n);
        addAndMakeVisible (c.on);
        c.onA = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, "on" + n, c.on);
        setupKnob (*this, c.harm, c.harmL, "Harmonic", p.apvts, "harm" + n, c.harmA);
        setupKnob (*this, c.gain, c.gainL, "Gain",     p.apvts, "gain" + n, c.gainA);
        setupKnob (*this, c.q,    c.qL,    "Q",        p.apvts, "q" + n,    c.qA);
        c.harm.setTextValueSuffix ("x");
        c.gain.setTextValueSuffix (" dB");
    }
    auto addG = [&] (juce::Slider& s, juce::Label& l, const char* t, const char* id, const char* suffix = "")
    {
        sAtt.emplace_back();
        setupKnob (*this, s, l, t, p.apvts, id, sAtt.back());
        s.setTextValueSuffix (suffix);
    };
    addG (glide, glideL, "Glide", "glide", " ms");
    addG (hold, holdL, "Hold", "hold", " ms");
    addG (sens, sensL, "Sensitivity", "sens");
    addG (refA, refAL, "Tuning A", "refa", " Hz");
    addG (lowNote, lowL, "Lowest note", "lownote");
    addG (highNote, highL, "Highest note", "highnote");
    addG (outGain, outL, "Output", "out", " dB");
    for (auto* s : { &lowNote, &highNote })
    {
        s->textFromValueFunction = [] (double v) { return noteName ((float) v); };
    }
    addG (compThr, compThrL, "Threshold", "comp_thr", " dB");
    addG (compRatio, compRatioL, "Ratio", "comp_ratio", ":1");
    addG (compAtt, compAttL, "Attack", "comp_att", " ms");
    addG (compRel, compRelL, "Release", "comp_rel", " ms");
    addAndMakeVisible (compOn);
    addAndMakeVisible (compAuto);
    compOnA = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, "comp_on", compOn);
    compAutoA = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, "comp_auto", compAuto);
    addAndMakeVisible (snap);
    snapA = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, "snap", snap);

    setSize (760, 694);
    startTimerHz (30);
}

void NoteEQEditor::timerCallback()
{
    const float n = proc.shownNote.load(), f = proc.shownHz.load(), gr = proc.shownGainReduction.load();
    if (n != note || f != hz) { note = n; hz = f; repaint(); }
    if (std::abs (gr - grDb) > 0.05f) { grDb = gr; repaint (meterArea.toNearestInt()); }
}

void NoteEQEditor::resized()
{
    auto r = getLocalBounds().reduced (12);
    r.removeFromTop (28);                                   // title / note readout
    graphArea = r.removeFromTop (170).toFloat();
    r.removeFromTop (8);

    auto colArea = r.removeFromTop (230);
    const int cw = colArea.getWidth() / noteeq::kNumBands;
    for (auto& c : cols)
    {
        auto a = colArea.removeFromLeft (cw).reduced (4);
        c.on.setBounds (a.removeFromTop (24));
        for (auto pr : { std::pair<juce::Slider*, juce::Label*> { &c.harm, &c.harmL },
                         { &c.gain, &c.gainL }, { &c.q, &c.qL } })
        {
            auto row = a.removeFromTop (66);
            pr.second->setBounds (row.removeFromLeft (52));
            pr.first->setBounds (row);
        }
    }
    r.removeFromTop (6);
    auto cr = r.removeFromTop (112);                        // compressor row
    {
        auto left = cr.removeFromLeft (110);
        compOn.setBounds (left.removeFromTop (24).translated (0, 6));
        compAuto.setBounds (left.removeFromTop (24).translated (0, 6));
        meterArea = cr.removeFromRight (120).withTrimmedTop (30).withHeight (18).toFloat();
        const int kw = cr.getWidth() / 4;
        for (auto pr : { std::pair<juce::Slider*, juce::Label*> { &compThr, &compThrL }, { &compRatio, &compRatioL },
                         { &compAtt, &compAttL }, { &compRel, &compRelL } })
        {
            auto a = cr.removeFromLeft (kw);
            pr.second->setBounds (a.removeFromTop (16));
            pr.first->setBounds (a);
        }
    }
    r.removeFromTop (6);
    auto g = r.removeFromTop (88);
    snap.setBounds (g.removeFromLeft (110).withHeight (24).translated (0, 8));
    const int gw = g.getWidth() / 7;
    for (auto pr : { std::pair<juce::Slider*, juce::Label*> { &glide, &glideL }, { &hold, &holdL }, { &sens, &sensL },
                     { &refA, &refAL }, { &lowNote, &lowL }, { &highNote, &highL }, { &outGain, &outL } })
    {
        auto a = g.removeFromLeft (gw);
        pr.second->setBounds (a.removeFromTop (16));
        pr.first->setBounds (a);
    }
}

void NoteEQEditor::drawResponse (juce::Graphics& g, juce::Rectangle<float> area)
{
    g.setColour (juce::Colour (0xff1b1d22));
    g.fillRoundedRectangle (area, 4.0f);
    const float fMin = 20.0f, fMax = 5000.0f, dbRange = 24.0f;
    auto xOf = [&] (float f) { return area.getX() + area.getWidth() * std::log (f / fMin) / std::log (fMax / fMin); };
    auto yOf = [&] (float db) { return area.getCentreY() - db / dbRange * (area.getHeight() * 0.5f); };

    g.setColour (juce::Colours::white.withAlpha (0.12f));
    for (float f : { 50.f, 100.f, 200.f, 500.f, 1000.f, 2000.f }) g.drawVerticalLine ((int) xOf (f), area.getY(), area.getBottom());
    for (float d : { -12.f, 0.f, 12.f }) g.drawHorizontalLine ((int) yOf (d), area.getX(), area.getRight());

    if (hz <= 0.0f)
    {
        g.setColour (juce::Colours::grey);
        g.drawText ("play a note...", area, juce::Justification::centred);
        return;
    }
    // analytic response of the bell filters at the currently tracked note
    juce::Path path;
    const int steps = (int) area.getWidth();
    for (int px = 0; px <= steps; ++px)
    {
        const float f = fMin * std::pow (fMax / fMin, (float) px / steps);
        double db = 0;
        for (int i = 0; i < noteeq::kNumBands; ++i)
        {
            const auto n = juce::String (i + 1);
            if (proc.apvts.getRawParameterValue ("on" + n)->load() < 0.5f) continue;
            const float fc = hz * proc.apvts.getRawParameterValue ("harm" + n)->load();
            const float gdb = proc.apvts.getRawParameterValue ("gain" + n)->load();
            const float q = proc.apvts.getRawParameterValue ("q" + n)->load();
            const double A = std::pow (10.0, gdb / 40.0), k = 1.0 / (q * A);
            const std::complex<double> s (0.0, f / fc);
            db += 20.0 * std::log10 (std::abs ((s * s + s * k * A * A + 1.0) / (s * s + s * k + 1.0)));
        }
        const float x = area.getX() + px, y = yOf ((float) juce::jlimit ((double) -dbRange, (double) dbRange, db));
        if (px == 0) path.startNewSubPath (x, y); else path.lineTo (x, y);
    }
    g.setColour (juce::Colour (0xff4fd1c5));
    g.strokePath (path, juce::PathStrokeType (2.0f));
    // markers at each band's centre
    for (int i = 0; i < noteeq::kNumBands; ++i)
    {
        const auto n = juce::String (i + 1);
        if (proc.apvts.getRawParameterValue ("on" + n)->load() < 0.5f) continue;
        const float fc = hz * proc.apvts.getRawParameterValue ("harm" + n)->load();
        if (fc < fMin || fc > fMax) continue;
        g.setColour (juce::Colours::orange);
        g.fillEllipse (xOf (fc) - 3.5f, yOf (proc.apvts.getRawParameterValue ("gain" + n)->load()) - 3.5f, 7.0f, 7.0f);
    }
}

void NoteEQEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff2a2d34));
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    g.drawText ("NOTE EQ", 14, 10, 200, 24, juce::Justification::left);
    g.setFont (juce::FontOptions (16.0f));
    const juce::String readout = hz > 0.0f ? noteName (note) + "   " + juce::String (hz, 1) + " Hz" : "--";
    g.drawText (readout, getWidth() - 214, 10, 200, 24, juce::Justification::right);
    drawResponse (g, graphArea);

    // gain-reduction meter (fills from the right as reduction grows, 0..18 dB)
    g.setColour (juce::Colours::white.withAlpha (0.7f));
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("Gain reduction", meterArea.translated (0, -16).toNearestInt(), juce::Justification::centredLeft);
    g.setColour (juce::Colour (0xff1b1d22));
    g.fillRoundedRectangle (meterArea, 3.0f);
    const float fill = juce::jlimit (0.0f, 1.0f, grDb / 18.0f);
    g.setColour (juce::Colour (0xffe0a030));
    g.fillRoundedRectangle (meterArea.withTrimmedLeft (meterArea.getWidth() * (1.0f - fill)), 3.0f);
    g.setColour (juce::Colours::white.withAlpha (0.8f));
    g.drawText (juce::String (grDb, 1) + " dB", meterArea.toNearestInt(), juce::Justification::centred);
}
