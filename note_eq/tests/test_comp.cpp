// g++ -O2 -std=c++17 -Isrc tests/test_comp.cpp -o t && ./t
#include "Compressor.h"
#include <cstdio>
#include <vector>
using namespace noteeq;
static const double SR = 48000, PI = 3.14159265358979;
static int fails = 0;
static void check (bool ok, const char* w) { std::printf ("[%s] %s\n", ok ? "PASS" : "FAIL", w); fails += !ok; }
static double db (double x) { return 20 * std::log10 (x); }

// run a 110 Hz sine of given peak dBFS for `sec`, return the output peak (dBFS) over the last 100 ms
static double steady (Compressor& c, double inDb, double sec)
{
    const double amp = std::pow (10.0, inDb / 20);
    double peak = 0; const size_t N = (size_t) (sec * SR);
    for (size_t n = 0; n < N; ++n)
    {
        float x = (float) (amp * std::sin (2 * PI * 110 * (double) n / SR));
        float y = x * c.gain (std::abs (x));
        if (n > N - (size_t) (0.1 * SR)) peak = std::max (peak, (double) std::abs (y));
    }
    return db (peak);
}

int main()
{
    Compressor::Params p; p.enabled = true; p.thresholdDb = -18; p.ratio = 4; p.attackMs = 5; p.releaseMs = 100; p.autoGain = false; p.kneeDb = 0;
    { Compressor c; c.prepare (SR); c.setParams (p);
      double o = steady (c, -6, 2.0); std::printf ("-6 dB in, thr -18, 4:1, no makeup -> %.2f dB (want ~ -15)\n", o);
      check (std::abs (o + 15.0) < 0.6, "static curve: 12 dB over threshold -> 9 dB reduction"); }
    { Compressor c; c.prepare (SR); c.setParams (p);
      double o = steady (c, -30, 2.0); check (std::abs (o + 30.0) < 0.2, "below threshold: untouched"); }

    p.autoGain = true;
    { Compressor c; c.prepare (SR); c.setParams (p);
      double quiet = steady (c, -30, 2.0), loud = steady (c, -6, 2.0);
      std::printf ("auto gain: -30 in -> %.1f out, -6 in -> %.1f out (spread %.1f dB vs 24 dB in)\n", quiet, loud, loud - quiet);
      check (loud - quiet < 24.0 - 8.0, "level spread is squeezed");
      check (loud > -3.0 && loud < 0.5, "auto gain brings loud peaks back near 0 dBFS"); }

    // attack time: step quiet -> loud, how long until ~63% of final reduction
    { Compressor::Params q = p; q.attackMs = 20; q.autoGain = false; Compressor c; c.prepare (SR); c.setParams (q);
      steady (c, -40, 0.5);
      double target = 9.0; int hit = -1; const double amp = std::pow (10.0, -6.0 / 20);
      for (int n = 0; n < (int) (0.3 * SR); ++n) { float x = (float) (amp * std::sin (2 * PI * 110 * n / SR)); c.gain (std::abs (x));
          if (hit < 0 && c.gainReductionDb() > 0.632 * target) hit = n; }
      std::printf ("attack set 20 ms, reached 63%% after %.1f ms\n", hit * 1000.0 / SR);
      check (hit * 1000.0 / SR > 10 && hit * 1000.0 / SR < 40, "attack time is about what the knob says"); }

    // disabled => exactly transparent, and switching off fades out without a jump
    { Compressor::Params off = p; off.enabled = false; Compressor c; c.prepare (SR); c.setParams (off);
      bool exact = true; for (int n = 0; n < 48000; ++n) exact &= c.gain (0.5f) == 1.0f;
      check (exact, "disabled: gain is exactly 1.0"); }
    { Compressor c; c.prepare (SR); c.setParams (p); steady (c, -6, 1.0);
      Compressor::Params off = p; off.enabled = false; c.setParams (off);
      float prev = c.gain (0.5f), worst = 0; for (int n = 0; n < 48000; ++n) { float g = c.gain (0.5f); worst = std::max (worst, std::abs (g - prev)); prev = g; }
      check (worst < 0.01f && std::abs (prev - 1.0f) < 1e-3f, "switching off glides back to unity, no jump"); }

    std::printf (fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails != 0;
}
