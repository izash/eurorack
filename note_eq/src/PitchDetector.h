#pragma once
#include <vector>
#include <cmath>
#include <algorithm>

namespace noteeq
{
// Monophonic pitch tracker (YIN) tuned for bass: works on a decimated copy of
// the input so the difference function stays cheap. Feed samples with push();
// every `hop` input samples it produces a new estimate.
class PitchDetector
{
public:
    struct Result
    {
        float frequency = 0.0f;  // Hz, 0 when nothing reliable was found
        float clarity   = 0.0f;  // 0..1, 1 = perfectly periodic
        float level     = 0.0f;  // RMS of the analysis window
    };

    void prepare (double sampleRate, float minHz, float maxHz)
    {
        sr = sampleRate;
        decim = std::max (1, (int) std::floor (sampleRate / 12000.0));
        dsr = sr / decim;
        minFreq = minHz;
        maxFreq = maxHz;
        tauMin = std::max (2, (int) std::floor (dsr / maxHz));
        tauMax = (int) std::ceil (dsr / minHz) + 1;
        window = std::max (128, tauMax);      // one period of the lowest note: the shortest window YIN allows
        ring.assign ((size_t) (window + tauMax + 8), 0.0f);
        diff.assign ((size_t) tauMax + 2, 0.0f);
        cmnd.assign ((size_t) tauMax + 2, 0.0f);
        scratch.assign (ring.size(), 0.0f);
        hop = std::max (1, (int) (dsr * 0.005));     // new estimate every ~5 ms
        // two cascaded one-pole low-passes at ~1 kHz remove most upper harmonics
        lpCoef = 1.0f - std::exp (-2.0f * 3.14159265f * 1000.0f / (float) sr);
        reset();
    }

    void reset()
    {
        std::fill (ring.begin(), ring.end(), 0.0f);
        writePos = 0; decimCount = 0; hopCount = 0; lp1 = lp2 = 0.0f; acc = 0.0f;
        last = {};
    }

    // Returns true when a fresh estimate is available in getResult().
    bool push (float x)
    {
        lp1 += lpCoef * (x - lp1);
        lp2 += lpCoef * (lp1 - lp2);
        if (++decimCount < decim) return false;
        decimCount = 0;

        ring[(size_t) writePos] = lp2;
        writePos = (writePos + 1) % (int) ring.size();
        if (++hopCount < hop) return false;
        hopCount = 0;
        analyse();
        return true;
    }

    const Result& getResult() const { return last; }
    void setThreshold (float t) { threshold = t; }

private:
    void analyse()
    {
        const int n = (int) ring.size();
        // linearise most recent samples into scratch (oldest first)
        for (int i = 0; i < n; ++i)
            scratch[(size_t) i] = ring[(size_t) ((writePos + i) % n)];
        const float* x = scratch.data() + (n - window - tauMax);

        double energy = 0.0;
        for (int i = 0; i < window; ++i) energy += (double) x[tauMax + i] * x[tauMax + i];
        last.level = (float) std::sqrt (energy / window);
        if (last.level < 1.0e-4f) { last.frequency = 0.0f; last.clarity = 0.0f; return; }

        // YIN difference function
        const int W = window;
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            double s = 0.0;
            const float* a = x + tauMax;
            const float* b = x + tauMax - tau;
            for (int i = 0; i < W; ++i) { const float d = a[i] - b[i]; s += (double) d * d; }
            diff[(size_t) tau] = (float) s;
        }
        // cumulative mean normalised difference
        cmnd[0] = 1.0f;
        double run = 0.0;
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            run += diff[(size_t) tau];
            cmnd[(size_t) tau] = run > 0.0 ? (float) (diff[(size_t) tau] * tau / run) : 1.0f;
        }
        // first dip under the threshold, then walk to the local minimum
        int found = -1;
        for (int tau = tauMin; tau < tauMax; ++tau)
        {
            if (cmnd[(size_t) tau] < threshold)
            {
                while (tau + 1 < tauMax && cmnd[(size_t) tau + 1] < cmnd[(size_t) tau]) ++tau;
                found = tau; break;
            }
        }
        if (found < 0)
        {   // no confident dip: report the global minimum's clarity so callers can gate on it
            float best = 1.0f;
            for (int tau = tauMin; tau < tauMax; ++tau) best = std::min (best, cmnd[(size_t) tau]);
            last.frequency = 0.0f; last.clarity = 1.0f - best; return;
        }
        // parabolic interpolation for sub-sample accuracy
        float t = (float) found;
        if (found > 1 && found < tauMax)
        {
            const float s0 = cmnd[(size_t) found - 1], s1 = cmnd[(size_t) found], s2 = cmnd[(size_t) found + 1];
            const float den = s0 + s2 - 2.0f * s1;
            if (std::abs (den) > 1.0e-9f) t += 0.5f * (s0 - s2) / den;
        }
        const float f = (float) (dsr / t);
        last.frequency = (f >= minFreq * 0.9f && f <= maxFreq * 1.1f) ? f : 0.0f;
        last.clarity = 1.0f - cmnd[(size_t) found];
    }

    double sr = 48000.0, dsr = 12000.0;
    int decim = 4, decimCount = 0, tauMin = 2, tauMax = 100, window = 256, hop = 60, hopCount = 0, writePos = 0;
    float minFreq = 30.0f, maxFreq = 500.0f, threshold = 0.15f, lpCoef = 0.1f, lp1 = 0, lp2 = 0, acc = 0;
    std::vector<float> ring, diff, cmnd, scratch;
    Result last;
};
} // namespace noteeq
