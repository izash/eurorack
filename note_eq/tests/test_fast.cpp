// Fast-line behaviour: tracking delay and click level. Build:
//   g++ -O2 -std=c++17 -Isrc tests/test_fast.cpp -o t && ./t
#include "NoteEQ.h"
#include <cstdio>
#include <vector>
using namespace noteeq;
static const double SR = 48000.0, PI = 3.14159265358979;

int main()
{
    // 16th-note line at ~140 bpm (107 ms/note), plucked: decaying harmonics + 8 ms noise burst at the attack
    const int midi[] = { 33, 33, 40, 38, 33, 45, 43, 40, 38, 36, 33, 40 };
    const double noteLen = 0.107;
    const int N = (int) (sizeof midi / sizeof *midi);
    std::vector<float> in ((size_t) (N * noteLen * SR));
    std::vector<int> expected (in.size());
    unsigned seed = 7;
    for (size_t n = 0; n < in.size(); ++n)
    {
        int k = (int) ((double) n / SR / noteLen);  double t = (double) n / SR - k * noteLen;
        double f0 = 440.0 * std::pow (2.0, (midi[k] - 69) / 12.0), s = 0;
        for (int h = 1; h <= 6; ++h) s += std::sin (2 * PI * f0 * h * t) / h;
        seed = seed * 1664525u + 1013904223u;
        double noise = t < 0.008 ? ((seed >> 8) / 16777216.0 - 0.5) * 0.8 : 0.0;
        in[n] = (float) (0.25 * (s * std::exp (-4.0 * t) + noise));
        expected[n] = midi[k];
    }

    for (float holdMs : { 400.0f, 120.0f, 30.0f })
    {
        Params p;
        for (int i = 0; i < kNumBands; ++i) { p.bands[(size_t) i].harmonic = (float) (i + 1); p.bands[(size_t) i].q = 8.0f; p.bands[(size_t) i].gainDb = (i % 2 ? -12.0f : 12.0f); }
        p.holdMs = holdMs;
        NoteEQ eq; eq.setParams (p); eq.prepare (SR);
        std::vector<float> out (in.size());
        double delaySum = 0; int delayCount = 0, correct = 0;
        int lastK = -1; bool locked = false;
        for (size_t n = 0; n < in.size(); ++n)
        {
            out[n] = eq.process (in[n]);
            int k = (int) ((double) n / SR / noteLen);
            if (k != lastK) { lastK = k; locked = false; }
            const bool ok = eq.isTracking() && std::abs (eq.currentNote() - (float) expected[n]) < 0.5f;
            correct += ok;
            if (ok && !locked && k > 0 && midi[k] != midi[k - 1]) { locked = true; delaySum += ((double) n / SR - k * noteLen) * 1000; ++delayCount; }
        }
        // "crackle" = largest jump of the EQ's added signal (out-in) between samples, relative to input peak
        double worst = 0, peak = 0; float prev = 0;
        for (size_t n = 0; n < in.size(); ++n) { peak = std::max (peak, (double) std::abs (in[n])); float d = out[n] - in[n]; worst = std::max (worst, (double) std::abs (d - prev)); prev = d; }
        std::printf ("hold %5.0f ms | mean time to lock on a new note: %5.1f ms | correct %4.1f%% of the time | worst click %.3f of peak\n",
                     holdMs, delayCount ? delaySum / delayCount : -1.0, 100.0 * correct / (double) in.size(), worst / peak);
    }
}
