// Offline renderer for debugging with real recordings.
//   render_raw in.f32 out.f32 [comp=0|1] [holdMs] -> prints tracked-note events + click stats
#include "NoteEQ.h"
#include "Compressor.h"
#include <cstdio>
#include <vector>
#include <string>
using namespace noteeq;
int main (int argc, char** argv)
{
    if (argc < 3) return 1;
    const bool useComp = argc > 3 && atoi (argv[3]);
    const float hold = argc > 4 ? (float) atof (argv[4]) : 150.0f;
    const double SR = 48000;
    std::vector<float> in; { FILE* f = fopen (argv[1], "rb"); float x; while (fread (&x, 4, 1, f) == 1) in.push_back (x); fclose (f); }
    Params p; const float g[5] = { 8, -4, 6, 4, 3 };
    for (int i = 0; i < kNumBands; ++i) { p.bands[(size_t) i].harmonic = (float) (i + 1); p.bands[(size_t) i].gainDb = g[i]; p.bands[(size_t) i].q = 4; }
    p.holdMs = hold;
    NoteEQ eq; eq.setParams (p); eq.prepare (SR);
    Compressor c; Compressor::Params cp; cp.enabled = useComp; c.prepare (SR); c.setParams (cp);
    std::vector<float> out (in.size()); std::vector<int> track (in.size() / 480 + 1, -1); int lastNote = -99; int events = 0; size_t segStart = 0; int blips = 0;
    for (size_t n = 0; n < in.size(); ++n)
    {
        float y = eq.process (in[n]);
        y *= c.gain (std::abs (y));
        out[n] = y;
        if (n % 480 == 0) track[n / 480] = eq.isTracking() ? (int) std::lround (eq.currentNote()) : -1;
        int note = eq.isTracking() ? (int) std::lround (eq.currentNote()) : -1;
        if (note != lastNote) { if (lastNote >= 0 && (double) (n - segStart) / SR < 0.060) ++blips; segStart = n; if (events < 400) std::printf ("%7.3fs  %s\n", (double) n / SR, note < 0 ? "--" : std::to_string (note).c_str()); lastNote = note; ++events; }
    }
    // artifact metrics on the EQ's added signal d = out - in
    double peak = 0; for (float x : in) peak = std::max (peak, (double) std::abs (x));
    int clicks = 0; double worst = 0; float prev = 0;
    for (size_t n = 0; n < in.size(); ++n) { float d = out[n] - in[n]; double j = std::abs (d - prev) / peak; prev = d; worst = std::max (worst, j); if (j > 0.03) ++clicks; }
    std::printf ("note changes: %d | blips <60ms: %d | samples with a jump >3%% of peak: %d | worst jump: %.3f\n", events, blips, clicks, worst);
    if (argc > 5) { FILE* t = fopen (argv[5], "w"); for (int v : track) fprintf (t, "%d\n", v); fclose (t); }
    FILE* f = fopen (argv[2], "wb"); fwrite (out.data(), 4, out.size(), f); fclose (f);
}
