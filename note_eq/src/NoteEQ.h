#pragma once
#include "PitchDetector.h"
#include <array>
#include <cmath>
#include <algorithm>

namespace noteeq
{
constexpr int kNumBands = 5;

// Zero-latency "bell" filter built on a TPT state-variable filter: stays stable
// and click-free while its centre frequency is being modulated.
class Bell
{
public:
    void reset() { s1 = s2 = 0.0f; }
    void setParams (float freq, float q, float gainDb, double sr)
    {
        const float A = std::pow (10.0f, gainDb / 40.0f);
        g  = std::tan (3.14159265f * std::min (freq, (float) sr * 0.45f) / (float) sr);
        k  = 1.0f / (q * A);
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
        m1 = k * (A * A - 1.0f);
    }
    float process (float x)
    {
        const float v3 = x - s2;
        const float v1 = a1 * s1 + a2 * v3;
        const float v2 = s2 + a2 * s1 + a3 * v3;
        s1 = 2.0f * v1 - s1;
        s2 = 2.0f * v2 - s2;
        return x + m1 * v1;
    }
private:
    float g = 0, k = 1, a1 = 1, a2 = 0, a3 = 0, m1 = 0, s1 = 0, s2 = 0;
};

struct BandParams
{
    bool  enabled  = true;
    float harmonic = 1.0f;   // multiple of the played note's fundamental
    float gainDb   = 0.0f;
    float q        = 4.0f;
};

struct Params
{
    std::array<BandParams, kNumBands> bands;
    float refA        = 440.0f;
    bool  snapToNote  = true;    // quantise detected pitch to the nearest semitone
    float glideMs     = 20.0f;   // how fast filters slide to a new note
    float holdMs      = 400.0f;  // keep last note this long after pitch is lost
    float sensitivity = 0.5f;    // 0 = strict, 1 = accept noisy pitch
    float minNote     = 28.0f;   // MIDI note range tracked (E1 = 28 for a 4-string bass)
    float maxNote     = 79.0f;
    float outputDb    = 0.0f;
};

class NoteEQ
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        detector.prepare (sr, midiToHz (p.minNote, p.refA) * 0.9f, midiToHz (p.maxNote, p.refA) * 1.1f);
        reset();
    }
    void reset()
    {
        detector.reset();
        for (auto& b : bell) b.reset();
        curNote = targetNote = -1.0f; holdSamples = 0; sinceVoiced = 1 << 30; counter = 0;
    }
    void setParams (const Params& np)
    {
        const bool rangeChanged = np.minNote != p.minNote || np.maxNote != p.maxNote || np.refA != p.refA;
        p = np;
        if (rangeChanged && sr > 0.0)
            detector.prepare (sr, midiToHz (p.minNote, p.refA) * 0.9f, midiToHz (p.maxNote, p.refA) * 1.1f);
        detector.setThreshold (0.05f + 0.25f * p.sensitivity);
        outGain = std::pow (10.0f, p.outputDb / 20.0f);
    }

    float process (float x)
    {
        // --- pitch tracking
        if (detector.push (x))
        {
            const auto& r = detector.getResult();
            if (r.frequency > 0.0f && r.clarity > 0.5f)
            {
                float note = hzToMidi (r.frequency, p.refA);
                if (note >= p.minNote - 0.5f && note <= p.maxNote + 0.5f)
                {
                    if (p.snapToNote) note = std::round (note);
                    targetNote = note;
                    if (curNote < 0.0f) curNote = note;
                    sinceVoiced = 0;
                }
            }
        }
        if (sinceVoiced < (1 << 30)) ++sinceVoiced;
        const bool held = sinceVoiced <= (int) (p.holdMs * 0.001 * sr);

        // --- glide current note toward the target (in semitones => exponential in Hz)
        if (curNote >= 0.0f && targetNote >= 0.0f)
        {
            const float coef = 1.0f - std::exp (-1.0f / std::max (1.0f, (float) (p.glideMs * 0.001 * sr)));
            curNote += coef * (targetNote - curNote);
        }

        // --- refresh filter coefficients every 16 samples
        if ((counter++ & 15) == 0)
            updateCoefficients (held);

        float y = x;
        for (int i = 0; i < kNumBands; ++i)
            if (active[(size_t) i]) y = bell[(size_t) i].process (y);
        return y * outGain;
    }

    float currentNote() const { return curNote; }              // MIDI note (fractional), <0 = none
    float currentFrequency() const { return curNote < 0 ? 0.0f : midiToHz (curNote, p.refA); }
    bool  isTracking() const { return curNote >= 0.0f && sinceVoiced <= (int) (p.holdMs * 0.001 * sr); }

    static float midiToHz (float n, float refA) { return refA * std::pow (2.0f, (n - 69.0f) / 12.0f); }
    static float hzToMidi (float f, float refA) { return 69.0f + 12.0f * std::log2 (f / refA); }

private:
    void updateCoefficients (bool held)
    {
        const float f0 = currentFrequency();
        for (int i = 0; i < kNumBands; ++i)
        {
            const auto& b = p.bands[(size_t) i];
            const float f = f0 * b.harmonic;
            const bool ok = b.enabled && held && f0 > 0.0f && f < (float) sr * 0.45f && std::abs (b.gainDb) > 0.01f;
            active[(size_t) i] = ok;
            if (ok) bell[(size_t) i].setParams (f, b.q, b.gainDb, sr);
        }
    }

    Params p;
    PitchDetector detector;
    std::array<Bell, kNumBands> bell;
    std::array<bool, kNumBands> active {};
    double sr = 0.0;
    float curNote = -1.0f, targetNote = -1.0f, outGain = 1.0f;
    int holdSamples = 0, sinceVoiced = 1 << 30;
    unsigned counter = 0;
};
} // namespace noteeq
