// Offline renderer for debugging with real recordings (mono float32 raw in/out).
//   render_raw in.f32 out.f32 sampleRate [preset.txt] [track.txt]
// preset.txt = "key value" lines using the plugin's parameter ids (gain1, harm1, q1, glide, hold, ...).
// Prints how often the tracked note changes and how short the stray ones are; track.txt gets the tracked MIDI note every 10 ms.
#include "NoteEQ.h"
#include "Compressor.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>
using namespace noteeq;

int main (int argc, char** argv)
{
    if (argc < 4) { std::printf ("usage: render_raw in.f32 out.f32 sampleRate [preset.txt] [track.txt]\n"); return 1; }
    const double SR = atof (argv[3]);
    std::map<std::string, float> v;
    if (argc > 4) { FILE* f = fopen (argv[4], "r"); char k[64]; float x; while (f && fscanf (f, "%63s %f", k, &x) == 2) v[k] = x; if (f) fclose (f); }
    auto get = [&] (const char* k, float d) { auto it = v.find (k); return it == v.end() ? d : it->second; };

    Params p;
    for (int i = 0; i < kNumBands; ++i)
    {
        const std::string n = std::to_string (i + 1); auto& b = p.bands[(size_t) i];
        b.enabled = get (("on" + n).c_str(), 1) > 0.5f; b.harmonic = get (("harm" + n).c_str(), (float) (i + 1));
        b.gainDb = get (("gain" + n).c_str(), 0); b.q = get (("q" + n).c_str(), 4);
    }
    p.glideMs = get ("glide", 20); p.holdMs = get ("hold", 150); p.sensitivity = get ("sens", 0.5f);
    p.lookaheadMs = get ("look", 0); p.snapToNote = get ("snap", 1) > 0.5f; p.minNote = get ("lownote", 28); p.maxNote = get ("highnote", 79); p.refA = get ("refa", 440);
    Compressor::Params cp; cp.enabled = get ("comp_on", 0) > 0.5f; cp.thresholdDb = get ("comp_thr", -18); cp.ratio = get ("comp_ratio", 4);
    cp.attackMs = get ("comp_att", 20); cp.releaseMs = get ("comp_rel", 100); cp.autoGain = get ("comp_auto", 1) > 0.5f;
    const float outGain = std::pow (10.0f, get ("out", 0) / 20.0f);

    std::vector<float> in; { FILE* f = fopen (argv[1], "rb"); float x; while (f && fread (&x, 4, 1, f) == 1) in.push_back (x); if (f) fclose (f); }
    std::vector<int> oracle; if (getenv ("ORACLE")) { FILE* f = fopen (getenv ("ORACLE"), "r"); int m; while (f && fscanf (f, "%d", &m) == 1) oracle.push_back (m); if (f) fclose (f); }
    NoteEQ eq; eq.setParams (p); eq.prepare (SR);
    Compressor c; c.prepare (SR); c.setParams (cp);
    std::vector<float> out (in.size());
    const int blk = (int) (SR * 0.01);
    std::vector<int> track (in.size() / (size_t) blk + 1, -1);
    int lastNote = -99, events = 0, blips = 0; size_t segStart = 0;
    for (size_t n = 0; n < in.size(); ++n)
    {
        if (!oracle.empty()) { const size_t k = std::min (n / (size_t) blk + (size_t) std::lround (get ("look", 0) * 0.001 * SR / blk) , oracle.size() - 1); eq.forceNote ((float) oracle[k]); }
        float y = eq.process (in[n]);
        y *= c.gain (std::abs (y)) * outGain;
        out[n] = y;
        const int note = eq.isTracking() ? (int) std::lround (eq.currentNote()) : -1;
        if (n % (size_t) blk == 0) track[n / (size_t) blk] = note;
        if (note != lastNote) { if (lastNote >= 0 && (double) (n - segStart) / SR < 0.060) ++blips; segStart = n; lastNote = note; ++events; }
    }
    std::printf ("latency %d samples\n", eq.latencySamples());
    std::printf ("note changes: %d | stray notes under 60 ms: %d\n", events, blips);
    if (argc > 5) { FILE* t = fopen (argv[5], "w"); for (int x : track) fprintf (t, "%d\n", x); fclose (t); }
    FILE* f = fopen (argv[2], "wb"); fwrite (out.data(), 4, out.size(), f); fclose (f);
}
