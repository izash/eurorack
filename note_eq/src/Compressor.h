#pragma once
#include <cmath>
#include <algorithm>

namespace noteeq
{
// Simple feed-forward compressor with soft knee and optional auto make-up gain.
// Feed it the (stereo-linked) absolute sample value, get back the linear gain to apply.
class Compressor
{
public:
    struct Params
    {
        bool  enabled     = false;
        float thresholdDb = -18.0f;
        float ratio       = 4.0f;
        float attackMs    = 10.0f;
        float releaseMs   = 150.0f;
        bool  autoGain    = true;
        float kneeDb      = 6.0f;
    };

    void prepare (double sampleRate) { sr = sampleRate; reset(); setParams (p); }
    void reset() { env = 0.0f; reduction = 0.0f; makeup = 0.0f; }

    void setParams (const Params& np)
    {
        p = np;
        attCoef = coefFor (std::max (0.1f, p.attackMs));
        relCoef = coefFor (std::max (1.0f, p.releaseMs));
        detCoef = coefFor (25.0f);    // detector falls over ~25 ms so the gain doesn't follow single bass cycles
        mkCoef  = coefFor (30.0f);
    }

    // absSample: max |x| over the channels at this instant
    float gain (float absSample)
    {
        // peak envelope: instant rise, ~25 ms fall
        env = absSample > env ? absSample : absSample + detCoef * (env - absSample);
        const float lvl = 20.0f * std::log10 (std::max (env, 1.0e-6f));

        float targetReduction = 0.0f, targetMakeup = 0.0f;
        if (p.enabled)
        {
            const float over = lvl - p.thresholdDb, inv = 1.0f / p.ratio, w = p.kneeDb;
            float out;
            if (2.0f * over < -w)       out = lvl;
            else if (2.0f * std::abs (over) <= w)
            {
                const float x = over + w * 0.5f;
                out = lvl + (inv - 1.0f) * x * x / (2.0f * w);
            }
            else                         out = p.thresholdDb + over * inv;
            targetReduction = lvl - out;                                     // >= 0 dB
            // make up the reduction a full-scale (0 dBFS) signal would receive
            if (p.autoGain) targetMakeup = -p.thresholdDb * (1.0f - inv);
        }
        // attack when reduction grows, release when it shrinks
        const float c = targetReduction > reduction ? attCoef : relCoef;
        reduction = targetReduction + c * (reduction - targetReduction);
        makeup += (1.0f - mkCoef) * (targetMakeup - makeup);
        if (!p.enabled && reduction < 1.0e-4f && makeup < 1.0e-4f) { reduction = 0.0f; makeup = 0.0f; }
        return std::pow (10.0f, (makeup - reduction) * 0.05f);
    }

    float gainReductionDb() const { return reduction; }

private:
    float coefFor (float ms) const { return std::exp (-1.0f / std::max (1.0f, (float) (ms * 0.001 * sr))); }

    Params p;
    double sr = 48000.0;
    float env = 0, reduction = 0, makeup = 0, attCoef = 0, relCoef = 0, detCoef = 0, mkCoef = 0;
};
} // namespace noteeq
