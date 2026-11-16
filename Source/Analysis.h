#pragma once
#include <vector>
#include <cmath>
#include <algorithm>

namespace lockstep
{
constexpr double maxDelayMs  = 20.0;  // bass can be pushed back up to 20 ms
constexpr double windowMs    = 100.0; // analysis window after each kick hit
constexpr double preRollMs   = 5.0;   // shown before the hit in the display

// RBJ second-order allpass: flat magnitude, phase rotates 0 -> -360 deg around f0.
struct Allpass
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    float z1 = 0, z2 = 0;

    void set (double fs, double f0, double q)
    {
        const double w0 = 2.0 * 3.14159265358979323846 * f0 / fs;
        const double alpha = std::sin (w0) / (2.0 * q);
        const double a0 = 1.0 + alpha;
        b0 = (float) ((1.0 - alpha) / a0);
        b1 = (float) ((-2.0 * std::cos (w0)) / a0);
        b2 = 1.0f;                          // (1 + alpha) / a0
        a1 = b1;
        a2 = b0;
    }

    void reset() { z1 = z2 = 0; }

    // transposed direct form II
    float process (float x)
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

// One captured kick hit. All signals are low-passed (analysis band), mono.
struct Hit
{
    double fs = 48000.0;
    int pre = 0;                    // samples of pre-roll in kick / bass
    std::vector<float> kick;        // [-pre, W)
    std::vector<float> bass;        // processed bass (as you hear it, pre-duck), [-pre, W)
    std::vector<float> rawBass;     // unprocessed bass, [-pre - maxDelay, W)
    float match = 50.0f;            // 0..100
};

inline float correlation (const float* a, const float* b, int n)
{
    double ab = 0, aa = 0, bb = 0;
    for (int i = 0; i < n; ++i)
    {
        ab += (double) a[i] * b[i];
        aa += (double) a[i] * a[i];
        bb += (double) b[i] * b[i];
    }
    if (aa < 1e-12 || bb < 1e-12)
        return 0.0f;
    return (float) (ab / std::sqrt (aa * bb));
}

inline float correlationToMatch (float r) { return std::clamp ((r + 1.0f) * 50.0f, 0.0f, 100.0f); }

struct AlignResult
{
    bool found = false;
    bool invert = false;
    float delayMs = 0.0f;
    bool phaseOn = false;
    float phaseHz = 100.0f;
    float matchBefore = 50.0f, matchAfter = 50.0f;
};

// Brute-force search over polarity, delay (0..20 ms) and an optional allpass
// (25..400 Hz at the given Q) that maximises kick/bass correlation, averaged over hits.
// Runs on decimated copies so it finishes in well under a second.
inline AlignResult autoAlign (const std::vector<Hit>& hits, float phaseQ)
{
    AlignResult res;
    if (hits.empty())
        return res;

    const double fs = hits.front().fs;
    const int dec = std::max (1, (int) std::lround (fs / 6000.0));
    const double dfs = fs / dec;

    struct H { std::vector<float> k, raw; int lead = 0; };
    std::vector<H> hs;
    float before = 0;

    for (auto& h : hits)
    {
        if (std::abs (h.fs - fs) > 0.5) continue;
        const int W = (int) h.kick.size() - h.pre;
        const int maxD = (int) h.rawBass.size() - h.pre - W;
        before += h.match;

        H d;
        // kick from onset, decimated (signals are already band-limited to < 300 Hz)
        for (int i = 0; i < W; i += dec) d.k.push_back (h.kick[(size_t) (h.pre + i)]);
        for (int i = 0; i < (int) h.rawBass.size(); i += dec) d.raw.push_back (h.rawBass[(size_t) i]);
        d.lead = (h.pre + maxD) / dec;   // index of onset in raw
        hs.push_back (std::move (d));
    }
    if (hs.empty())
        return res;
    res.matchBefore = before / (float) hs.size();

    std::vector<float> freqs { 0.0f };   // 0 = filter off
    for (int i = 0; i < 24; ++i)
        freqs.push_back (25.0f * std::pow (16.0f, (float) i / 23.0f));   // 25 .. 400 Hz

    const double maxDelaySmp = maxDelayMs * 0.001 * dfs;
    const double step = 0.05 * 0.001 * dfs;   // 0.05 ms

    float bestScore = -2.0f;
    std::vector<std::vector<float>> filtered (hs.size());
    std::vector<float> tmp;

    for (float f : freqs)
    {
        for (size_t i = 0; i < hs.size(); ++i)
        {
            filtered[i] = hs[i].raw;
            if (f > 0.0f)
            {
                Allpass ap; ap.set (dfs, f, phaseQ);
                for (auto& v : filtered[i]) v = ap.process (v);
            }
        }

        for (double d = 0.0; d <= maxDelaySmp + 1e-9; d += step)
        {
            double sab = 0, saa = 0, sbb = 0;
            for (size_t i = 0; i < hs.size(); ++i)
            {
                const auto& k = hs[i].k;
                const auto& r = filtered[i];
                const int n = (int) k.size();
                double ab = 0, aa = 0, bb = 0;
                for (int t = 0; t < n; ++t)
                {
                    const double pos = hs[i].lead + t - d;
                    const int p0 = (int) std::floor (pos);
                    if (p0 < 0 || p0 + 1 >= (int) r.size()) continue;
                    const float fr = (float) (pos - p0);
                    const float b = r[(size_t) p0] + fr * (r[(size_t) p0 + 1] - r[(size_t) p0]);
                    ab += k[(size_t) t] * b; aa += k[(size_t) t] * k[(size_t) t]; bb += b * b;
                }
                if (aa > 1e-12 && bb > 1e-12)
                {   // normalise per hit so loud hits don't dominate
                    const double nrm = std::sqrt (aa * bb);
                    sab += ab / nrm; saa += 1; sbb += 1;
                }
            }
            if (saa < 1) continue;
            float r = (float) (sab / saa);
            const bool inv = r < 0.0f;
            float score = std::abs (r);
            // tiny bias towards the simplest fix (less delay, no filter)
            score -= (float) (d / maxDelaySmp) * 0.01f + (f > 0 ? 0.005f : 0.0f);

            if (score > bestScore)
            {
                bestScore = score;
                res.found = true;
                res.invert = inv;
                res.delayMs = (float) (d / dfs * 1000.0);
                res.phaseOn = f > 0.0f;
                res.phaseHz = f > 0.0f ? f : 100.0f;
                res.matchAfter = correlationToMatch (std::abs (r));
            }
        }
    }
    return res;
}
} // namespace lockstep
