// Standalone check of the DSP core (no JUCE needed):  g++ -O2 -std=c++17 -Isrc tests/test_core.cpp -o /tmp/t && /tmp/t
#include "NoteEQ.h"
#include <cstdio>
#include <vector>
#include <complex>

using namespace noteeq;
static const double SR = 48000.0, PI = 3.14159265358979;

static std::vector<float> bassNote (double f0, double seconds, double phase0 = 0)
{
    std::vector<float> v ((size_t) (seconds * SR));
    for (size_t n = 0; n < v.size(); ++n)
    {
        double t = (double) n / SR, s = 0;
        for (int h = 1; h <= 8; ++h) s += std::sin (2 * PI * f0 * h * t + phase0 * h) / (h * 1.0);
        v[n] = (float) (0.2 * s);
    }
    return v;
}
// amplitude of one frequency component (single-bin DFT) over a segment
static double mag (const std::vector<float>& x, size_t from, size_t to, double f)
{
    std::complex<double> a = 0;
    for (size_t n = from; n < to; ++n) a += (double) x[n] * std::polar (1.0, -2 * PI * f * (double) n / SR);
    return std::abs (a) * 2 / (double) (to - from);
}
static int fails = 0;
static void check (bool ok, const char* what) { std::printf ("[%s] %s\n", ok ? "PASS" : "FAIL", what); fails += !ok; }

int main()
{
    // bass line: A1 (55 Hz) then D2 (73.416 Hz)
    auto in = bassNote (55.0, 1.0);
    auto d2 = bassNote (73.4162, 1.0);
    in.insert (in.end(), d2.begin(), d2.end());

    Params p;                                    // 5 bands = harmonics 1..5, all flat...
    for (int i = 0; i < kNumBands; ++i) { p.bands[(size_t) i].harmonic = (float) (i + 1); p.bands[(size_t) i].q = 8.0f; }
    p.bands[0].gainDb = 12.0f;                   // ...except fundamental +12 dB
    p.bands[2].gainDb = -12.0f;                  // and 3rd harmonic -12 dB

    NoteEQ eq; eq.setParams (p); eq.prepare (SR);
    std::vector<float> out (in.size());
    float noteAtEndA = 0, noteAtEndD = 0;
    for (size_t n = 0; n < in.size(); ++n)
    {
        out[n] = eq.process (in[n]);
        if (n == (size_t) SR - 1) noteAtEndA = eq.currentNote();
        if (n == in.size() - 1)   noteAtEndD = eq.currentNote();
    }
    std::printf ("tracked note during A1: %.2f (want 33)   during D2: %.2f (want 38)\n", noteAtEndA, noteAtEndD);
    check (std::abs (noteAtEndA - 33.0f) < 0.1f, "A1 detected as MIDI 33");
    check (std::abs (noteAtEndD - 38.0f) < 0.1f, "D2 detected as MIDI 38");

    auto dB = [] (double a, double b) { return 20 * std::log10 (a / b); };
    // measure on the settled tail of each note
    size_t a0 = (size_t) (0.6 * SR), a1 = (size_t) (0.95 * SR), d0 = (size_t) (1.6 * SR), d1 = (size_t) (1.95 * SR);
    double gA1 = dB (mag (out, a0, a1, 55.0),  mag (in, a0, a1, 55.0));
    double gA3 = dB (mag (out, a0, a1, 165.0), mag (in, a0, a1, 165.0));
    double gA2 = dB (mag (out, a0, a1, 110.0), mag (in, a0, a1, 110.0));
    double gD1 = dB (mag (out, d0, d1, 73.4162),  mag (in, d0, d1, 73.4162));
    double gD3 = dB (mag (out, d0, d1, 220.2486), mag (in, d0, d1, 220.2486));
    double gD2 = dB (mag (out, d0, d1, 146.8324), mag (in, d0, d1, 146.8324));
    std::printf ("A1 note: fund %+.1f dB, 2nd %+.1f dB, 3rd %+.1f dB\n", gA1, gA2, gA3);
    std::printf ("D2 note: fund %+.1f dB, 2nd %+.1f dB, 3rd %+.1f dB\n", gD1, gD2, gD3);
    check (std::abs (gA1 - 12) < 1.0 && std::abs (gA3 + 12) < 1.0 && std::abs (gA2) < 1.0, "A1: +12 fundamental, -12 on 3rd, 2nd untouched");
    check (std::abs (gD1 - 12) < 1.0 && std::abs (gD3 + 12) < 1.0 && std::abs (gD2) < 1.0, "D2: filters followed the note");

    // flat settings must be transparent
    Params flat; NoteEQ eq2; eq2.setParams (flat); eq2.prepare (SR);
    double err = 0; for (size_t n = 0; n < in.size(); ++n) err = std::max (err, (double) std::abs (eq2.process (in[n]) - in[n]));
    check (err < 1e-6, "all gains at 0 dB is bit-transparent");

    // silence / noise must not crash or produce NaN
    NoteEQ eq3; eq3.setParams (p); eq3.prepare (SR);
    bool finite = true; unsigned seed = 1;
    for (int n = 0; n < 96000; ++n) { seed = seed * 1664525u + 1013904223u; float y = eq3.process (n < 48000 ? 0.0f : ((seed >> 8) / 16777216.0f - 0.5f)); finite &= std::isfinite (y); }
    check (finite, "silence and white noise stay finite");

    std::printf (fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails != 0;
}
