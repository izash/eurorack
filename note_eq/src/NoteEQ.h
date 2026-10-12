#pragma once
#include "PitchDetector.h"
#include <array>
#include <vector>
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
        if (std::abs (freq - lf) < 1.0e-5f * freq && std::abs (q - lq) < 1.0e-4f && std::abs (gainDb - lg) < 1.0e-4f) return;
        lf = freq; lq = q; lg = gainDb;
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
    float g = 0, k = 1, a1 = 1, a2 = 0, a3 = 0, m1 = 0, s1 = 0, s2 = 0, lf = -1, lq = -1, lg = -1000;
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
    float lookaheadMs = 0.0f;    // audio is delayed this long so the EQ is already tuned when a note arrives
};

class NoteEQ
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        detector.prepare (sr, midiToHz (p.minNote, p.refA) * 0.9f, midiToHz (p.maxNote, p.refA) * 1.1f);
        delayLine.assign ((size_t) (sr * 0.2) + 4, 0.0f);
        reset();
    }
    int latencySamples() const { return (int) std::lround (std::min (p.lookaheadMs, 150.0f) * 0.001 * sr); }
    void reset()
    {
        std::fill (delayLine.begin(), delayLine.end(), 0.0f); writePos = 0;
        detector.reset();
        for (auto& bank : bell) for (auto& b : bank) b.reset();
        bankNote = { -1.0f, -1.0f }; weight = { 1.0f, 0.0f }; dom = 0;
        targetNote = -1.0f; sinceVoiced = 1 << 30;
        candNote = -1000; candCount = 0; peakLevel = 0.0f;
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
                    const int need = confirmNeeded();
                    if (sameNote || candCount >= need || targetNote < 0.0f) targetNote = note;
                    if (sameNote || candCount >= need) sinceVoiced = 0;
                }
            }
        }
        if (sinceVoiced < (1 << 30)) ++sinceVoiced;
        const bool held = sinceVoiced <= (int) (p.holdMs * 0.001 * sr);

        // --- two filter banks: the EQ for the old note and the EQ for the new note run side by side
        //     and are crossfaded, so the level never dips and no second "attack" appears.
        //     Small moves (slides, bends, vibrato) just glide the active bank.
        if (targetNote >= 0.0f)
        {
            if (bankNote[(size_t) dom] < 0.0f)
            {
                bankNote = { targetNote, targetNote }; weight = { 1.0f, 0.0f }; dom = 0;
            }
            else if (std::abs (targetNote - bankNote[(size_t) dom]) > 1.0f)
            {
                const int nb = weight[0] < weight[1] ? 0 : 1;            // retune whichever bank is quieter
                bankNote[(size_t) nb] = targetNote; dom = nb;
            }
            else
            {
                const float coef = 1.0f - std::exp (-1.0f / std::max (1.0f, (float) (p.glideMs * 0.001 * sr)));
                bankNote[(size_t) dom] += coef * (targetNote - bankNote[(size_t) dom]);
            }
            const float step = 1.0f / std::max (1.0f, (float) (std::max (1.0f, p.glideMs) * 0.001 * sr));
            weight[(size_t) dom] = std::min (1.0f, weight[(size_t) dom] + step);
            weight[(size_t) (1 - dom)] = 1.0f - weight[(size_t) dom];
        }

        updateCoefficients (held);

        // the filters see the delayed audio; the tracker above saw it `lookahead` earlier
        float xd = x;
        if (!delayLine.empty())
        {
            const int n = (int) delayLine.size();
            delayLine[(size_t) writePos] = x;
            xd = delayLine[(size_t) (((writePos - latencySamples()) % n + n) % n)];
            writePos = (writePos + 1) % n;
        }
        float out = xd;
        for (int b = 0; b < 2; ++b)
        {
            if (bankNote[(size_t) b] < 0.0f) continue;
            float y = xd;
            for (auto& band : bell[(size_t) b]) y = band.process (y);
            out += weight[(size_t) b] * (y - xd);
        }
        return out;
    }

    // test hook: bypass the tracker and impose a note (used to compare against a perfect tracker)
    void forceNote (float midi) { if (midi >= 0.0f) { targetNote = midi; sinceVoiced = 0; } }

    float currentNote() const { return bankNote[(size_t) dom]; }   // MIDI note (fractional), <0 = none
    float currentFrequency() const { return currentNote() < 0 ? 0.0f : midiToHz (currentNote(), p.refA); }
    bool  isTracking() const { return currentNote() >= 0.0f && sinceVoiced <= (int) (p.holdMs * 0.001 * sr); }

    static float midiToHz (float n, float refA) { return refA * std::pow (2.0f, (n - 69.0f) / 12.0f); }
    static float hzToMidi (float f, float refA) { return 69.0f + 12.0f * std::log2 (f / refA); }

private:
    void updateCoefficients (bool held)
    {
        const float step = 1.0f / (float) sr;
        const float domHz = currentFrequency();
        for (int i = 0; i < kNumBands; ++i)
        {
            const auto& band = p.bands[(size_t) i];
            const bool ok = band.enabled && held && domHz > 0.0f && domHz * band.harmonic < (float) sr * 0.45f;
            // a band that stops applying fades out (~25 ms) and fades back in (~5 ms) instead of switching
            auto& lv = bandLevel[(size_t) i];
            lv = ok ? std::min (1.0f, lv + step / 0.005f) : std::max (0.0f, lv - step / 0.025f);
            const float g = band.gainDb * lv;
            for (int b = 0; b < 2; ++b)
            {
                if (bankNote[(size_t) b] < 0.0f) continue;
                const float f = midiToHz (bankNote[(size_t) b], p.refA) * band.harmonic;
                const bool inRange = f < (float) sr * 0.45f;
                bell[(size_t) b][(size_t) i].setParams (inRange ? f : (float) sr * 0.45f, band.q, inRange ? g : 0.0f, sr);
            }
        }
    }

    Params p;
    PitchDetector detector;
    std::array<std::array<Bell, kNumBands>, 2> bell;
    // with lookahead the extra waiting is hidden by the delay, so be stricter about believing a new note
    int confirmNeeded() const { return p.lookaheadMs >= 30.0f ? 5 : 2; }
    float peakLevel = 0.0f;
    int candNote = -1000, candCount = 0;
    std::array<float, kNumBands> bandLevel {};
    std::vector<float> delayLine;
    int writePos = 0;
    std::array<float, 2> bankNote { -1.0f, -1.0f }, weight { 1.0f, 0.0f };
    int dom = 0;
    double sr = 0.0;
    float targetNote = -1.0f;
    int sinceVoiced = 1 << 30;
};
} // namespace noteeq
