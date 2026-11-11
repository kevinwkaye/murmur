// End-to-end fidelity checks, measured against what the pedal demos showed (Claude, November 2026).
// Each check runs the whole engine, host rate in and out, so the Clock's resampling is included.
//
//  1. Stretch keeps a steady tone steady (it measured 22 dB of grain flutter before).
//  2. Reverb tails darken as they decay: treble dies faster than bass, as in the MOOD MKII demo
//     (low 7.5 s, mid 3.3 s, high 2.2 s), and the sides stay partly correlated (demo 0.55 to 0.9).
//  3. Low Clock settings keep their aliasing grit but add no mirror images above the clock's band.
//
// Usage: murmur_fidelity_test
#include "MurmurEngine.h"
#include <cmath>
#include <cstdio>
#include <functional>
#include <vector>

using namespace murmur;

namespace
{
constexpr float kFs = 48000.0f;
int failures = 0;

void expect (bool ok, const char* what, double value)
{
    std::printf ("%s  %-64s %8.2f\n", ok ? "pass" : "FAIL", what, value);
    if (! ok) ++failures;
}

struct Render { std::vector<float> l, r; };

Render run (float seconds, const std::function<float (int)>& input, const std::function<void (float, EngineParams&)>& set)
{
    const int n = (int) (seconds * kFs);
    Render out { std::vector<float> ((size_t) n), std::vector<float> ((size_t) n) };
    for (int i = 0; i < n; ++i) out.l[(size_t) i] = out.r[(size_t) i] = input (i);
    MurmurEngine e;
    e.prepare (kFs);
    EngineParams p;
    p.glue = 0.0f;
    for (int i = 0; i < n; i += 64)
    {
        set ((float) i / kFs, p);
        e.process (out.l.data() + i, out.r.data() + i, std::min (64, n - i), p);
    }
    return out;
}

// level range in dB over whole cycles of a steady tone, from `from` seconds on
double levelRange (const std::vector<float>& x, float tone, float from)
{
    const int w = (int) (kFs / tone * 10.0f);
    double lo = 1e9, hi = -1e9;
    for (int i = (int) (from * kFs); i + w < (int) x.size(); i += w / 4)
    {
        double s = 0.0;
        for (int k = 0; k < w; ++k) s += (double) x[(size_t) (i + k)] * x[(size_t) (i + k)];
        const double db = 10.0 * std::log10 (s / w + 1e-12);
        lo = std::min (lo, db);
        hi = std::max (hi, db);
    }
    return hi - lo;
}

// Decay times of a low band (centred on 300 Hz) and a high band (centred on 4 kHz),
// each through two cascaded band-pass biquads
struct BandDecay { double low = 0, high = 0; };

std::vector<double> bandPass (const std::vector<float>& x, double centre, double q)
{
    const double w = 2.0 * 3.14159265 * centre / kFs, alpha = std::sin (w) / (2.0 * q), a0 = 1.0 + alpha;
    const double b0 = alpha / a0, b2 = -alpha / a0, a1 = -2.0 * std::cos (w) / a0, a2 = (1.0 - alpha) / a0;
    std::vector<double> y (x.begin(), x.end());
    for (int pass = 0; pass < 2; ++pass)
    {
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        for (auto& v : y)
        {
            const double in = v, out = b0 * in + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1; x1 = in; y2 = y1; y1 = out; v = out;
        }
    }
    return y;
}

BandDecay decayTimes (const std::vector<float>& x, float from)
{
    const auto lo = bandPass (x, 300.0, 1.0), hi = bandPass (x, 4000.0, 1.0);
    auto t60 = [&] (const std::vector<double>& y)
    {
        // Schroeder backward integration, slope from -5 to -25 dB
        std::vector<double> e (y.size());
        double acc = 0.0;
        for (size_t i = y.size(); i-- > (size_t) (from * kFs);) { acc += y[i] * y[i]; e[i] = acc; }
        const double top = e[(size_t) (from * kFs)];
        size_t i5 = 0, i25 = 0;
        for (size_t i = (size_t) (from * kFs); i < e.size(); ++i)
        {
            const double db = 10.0 * std::log10 (e[i] / top + 1e-30);
            if (i5 == 0 && db < -5.0) i5 = i;
            if (db < -25.0) { i25 = i; break; }
        }
        return i25 > i5 ? 60.0 * (double) (i25 - i5) / 20.0 / kFs : 0.0;
    };
    return { t60 (lo), t60 (hi) };
}

double correlation (const Render& r, float from, float to)
{
    double xy = 0, xx = 0, yy = 0;
    for (int i = (int) (from * kFs); i < (int) (to * kFs); ++i)
    {
        xy += (double) r.l[(size_t) i] * r.r[(size_t) i];
        xx += (double) r.l[(size_t) i] * r.l[(size_t) i];
        yy += (double) r.r[(size_t) i] * r.r[(size_t) i];
    }
    return xy / std::sqrt (xx * yy + 1e-30);
}

// level of one frequency, in dBFS (Hann-windowed DFT bin)
double levelAt (const std::vector<float>& x, float hz, float from, float secs)
{
    const int a = (int) (from * kFs), n = (int) (secs * kFs);
    double re = 0, im = 0, wsum = 0;
    for (int i = 0; i < n; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (2.0 * 3.14159265 * i / n);
        const double ph = 2.0 * 3.14159265 * hz * (a + i) / kFs;
        re += w * x[(size_t) (a + i)] * std::cos (ph);
        im += w * x[(size_t) (a + i)] * std::sin (ph);
        wsum += w;
    }
    return 20.0 * std::log10 (2.0 * std::sqrt (re * re + im * im) / wsum + 1e-12);
}
} // namespace

int main()
{
    auto tone = [] (float hz, float amp) { return [hz, amp] (int i) { return amp * std::sin (6.2831853f * hz * (float) i / kFs); }; };

    // ---- 1. Stretch grains don't cancel ----
    std::printf ("Stretch\n");
    for (float modify : { 0.5f, 0.2f, 0.8f, 0.05f })
    {
        const auto r = run (10.0f, tone (330.0f, 0.3f), [modify] (float t, EngineParams& p)
        {
            p.loopMode = 2; p.loopModify = modify; p.length = 1.0f; p.mix = 1.0f; p.dryKill = true; p.loopOn = t > 3.0f;
        });
        char what[96];
        std::snprintf (what, sizeof what, "Stretch at Modify %.2f holds a steady tone within 3 dB", modify);
        expect (levelRange (r.l, 330.0f, 5.0f) < 3.0, what, levelRange (r.l, 330.0f, 5.0f));
    }

    // ---- 2. Reverb tails darken and stay partly correlated ----
    std::printf ("Reverb\n");
    auto click = [] (int i) { return i >= 4800 && i < 4824 ? 0.5f * std::sin (3.14159f * (float) (i - 4800) / 24.0f) : 0.0f; };
    for (float time : { 0.4f, 0.7f })
    {
        const auto r = run (12.0f, click, [time] (float, EngineParams& p)
        {
            p.wetOn = true; p.wetMode = 0; p.time = time; p.wetModify = 0.7f; p.mix = 1.0f; p.dryKill = true;
        });
        const auto d = decayTimes (r.l, 0.12f);
        char what[96];
        std::snprintf (what, sizeof what, "Reverb Time %.1f: treble decays faster than bass (high/low)", time);
        expect (d.high / d.low < 0.8, what, d.high / d.low);
        std::snprintf (what, sizeof what, "Reverb Time %.1f: left/right correlation between 0.4 and 0.85", time);
        const double c = correlation (r, 0.3f, 3.0f);
        expect (c > 0.4 && c < 0.85, what, c);
    }

    // ---- 3. Low Clock: grit, but no mirror images ----
    std::printf ("Clock\n");
    auto lowClock = [] (float, EngineParams& p)
    {
        p.clock = 0.15f; p.wetOn = true; p.wetMode = 1; p.time = 0.0f; p.wetModify = 0.0f; p.mix = 1.0f; p.dryKill = true;
    };
    const auto clean = run (3.0f, tone (1000.0f, 0.3f), lowClock);
    const double toneLevel = levelAt (clean.l, 1000.0f, 1.0f, 1.5f);
    const double image = levelAt (clean.l, 3000.0f, 1.0f, 1.5f);
    expect (toneLevel - image > 30.0, "4 kHz clock: a 1 kHz tone's image at 3 kHz is 30 dB down", toneLevel - image);
    const auto above = run (3.0f, tone (3000.0f, 0.3f), lowClock);
    const double alias = levelAt (above.l, 1000.0f, 1.0f, 1.5f);
    expect (alias > -30.0, "4 kHz clock: a 3 kHz tone still aliases to 1 kHz (grit kept)", alias);

    std::printf ("\n%s (%d failures)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
