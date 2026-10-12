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
    float holdMs      = 150.0f;  // keep last note this long after pitch is lost
    float sensitivity = 0.5f;    // 0 = strict, 1 = accept noisy pitch
    float minNote     = 28.0f;   // MIDI note range tracked (E1 = 28 for a 4-string bass)
    float maxNote     = 79.0f;
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
        level = 0.0f; dipping = false; candNote = -1000; candCount = 0; peakLevel = 0.0f;
        bandLevel.fill (0.0f);
    }
    void setParams (const Params& np)
    {
        const bool rangeChanged = np.minNote != p.minNote || np.maxNote != p.maxNote || np.refA != p.refA;
        p = np;
        if (rangeChanged && sr > 0.0)
            detector.prepare (sr, midiToHz (p.minNote, p.refA) * 0.9f, midiToHz (p.maxNote, p.refA) * 1.1f);
        detector.setThreshold (0.05f + 0.25f * p.sensitivity);
    }

    float process (float x)
    {
        // --- pitch tracking
        if (detector.push (x))
        {
            const auto& r = detector.getResult();
            // recent-peak tracker (falls ~30 dB/s): estimates taken in the dips between notes are junk
            peakLevel = std::max (r.level, peakLevel * 0.983f);
            const bool loudEnough = r.level > peakLevel * 0.316f;           // within 10 dB of the recent peak
            if (loudEnough && r.frequency > 0.0f && r.clarity > 0.5f)
            {
                float note = hzToMidi (r.frequency, p.refA);
                if (note >= p.minNote - 0.5f && note <= p.maxNote + 0.5f)
                {
                    const int cand = (int) std::lround (note);
                    if (p.snapToNote) note = std::round (note);
                    // a different note must be heard twice in a row before the EQ follows it:
                    // single wrong estimates (pluck noise, octave slips) are ignored
                    const bool sameNote = targetNote >= 0.0f && cand == (int) std::lround (targetNote);
                    candCount = (cand == candNote) ? candCount + 1 : 1;
                    candNote = cand;
                    if (sameNote || candCount >= kConfirm || curNote < 0.0f)
                    {
                        targetNote = note;
                        if (curNote < 0.0f) curNote = note;
                    }
                    if (sameNote || candCount >= kConfirm) sinceVoiced = 0;
                }
            }
        }
        if (sinceVoiced < (1 << 30)) ++sinceVoiced;
        const bool held = sinceVoiced <= (int) (p.holdMs * 0.001 * sr);

        // --- small moves (slides, bends, vibrato) glide; a jump of more than a semitone
        //     fades the EQ out, moves it while it is silent, then fades it back in
        if (curNote >= 0.0f && targetNote >= 0.0f)
        {
            if (!dipping && std::abs (targetNote - curNote) > 1.0f) dipping = true;
            if (dipping)
            {
                level -= 1.0f / std::max (1.0f, (float) (0.002 * sr));          // ~2 ms down
                if (level <= 0.0f) { level = 0.0f; curNote = targetNote; dipping = false; }
            }
            else
            {
                const float coef = 1.0f - std::exp (-1.0f / std::max (1.0f, (float) (p.glideMs * 0.001 * sr)));
                curNote += coef * (targetNote - curNote);
                level = std::min (1.0f, level + 1.0f / std::max (1.0f, (float) (0.008 * sr)));  // ~8 ms up
            }
        }
        else level = 0.0f;

        // --- refresh filter coefficients every 1 samples
        if ((counter++ & 0) == 0)
            updateCoefficients (held);

        float y = x;
        for (int i = 0; i < kNumBands; ++i)
            if (active[(size_t) i]) y = bell[(size_t) i].process (y);
        return y;
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
        const float step = 1.0f / (float) sr;
        for (int i = 0; i < kNumBands; ++i)
        {
            const auto& b = p.bands[(size_t) i];
            const float f = f0 * b.harmonic;
            const bool ok = b.enabled && held && f0 > 0.0f && f < (float) sr * 0.45f;
            // fade the band in/out instead of switching it (that switch was the crackle):
            // quick in (~5 ms), slower out (~25 ms)
            auto& lv = bandLevel[(size_t) i];
            lv = ok ? std::min (1.0f, lv + step / 0.005f) : std::max (0.0f, lv - step / 0.025f);
            const float g = b.gainDb * lv * level;
            // filters keep running at 0 dB while faded out so their state is always ready
            active[(size_t) i] = f0 > 0.0f && f < (float) sr * 0.45f && (b.enabled || lv > 0.0f);
            if (active[(size_t) i]) bell[(size_t) i].setParams (f, b.q, g, sr);
        }
    }

    Params p;
    PitchDetector detector;
    std::array<Bell, kNumBands> bell;
    static constexpr int kConfirm = 2;
    float peakLevel = 0.0f;
    int candNote = -1000, candCount = 0;
    std::array<bool, kNumBands> active {};
    std::array<float, kNumBands> bandLevel {};
    float level = 0.0f;
    bool dipping = false;
    double sr = 0.0;
    float curNote = -1.0f, targetNote = -1.0f;
    int holdSamples = 0, sinceVoiced = 1 << 30;
    unsigned counter = 0;
};
} // namespace noteeq
