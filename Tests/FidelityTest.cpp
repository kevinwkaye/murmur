// End-to-end fidelity checks, measured against what the pedal demos showed (Claude, November 2026).
// Each check runs the whole engine, host rate in and out, so the Clock's resampling is included.
//
//  1. Stretch keeps a steady tone steady (it measured 22 dB of grain flutter before).
//  2. Reverb tails are dark from the start and decay evenly, with wide sides. Pass 2 (11 November)
//     measured clean tails in both demos: 3 to 7 kHz 31 to 47 dB under the low mids, even decay
//     across bands, 0.1 to 0.3 left/right correlation. (Pass 1's targets, treble decaying faster
//     and 0.55 to 0.9 correlation, came from segments that still held the mono dry guitar.)
//  3. Low Clock settings keep their aliasing grit but add no mirror images above the clock's band.
//  4. Murk's Soup re-synthesises held notes in tune and pure (the Bad Mood demo's Soup is tonal;
//     preview 2's was 254 cents out and noise-like).
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

    // ---- 2. Reverb tails: dark from the start, even decay, wide ----
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
        // a click is flat, so the early tail's own balance is the reverb's voicing
        auto bandDb = [&] (double centre)
        {
            const auto y = bandPass (r.l, centre, 1.0);
            double e = 0.0;
            for (int i = (int) (0.3f * kFs); i < (int) (1.3f * kFs); ++i) e += y[(size_t) i] * y[(size_t) i];
            return 10.0 * std::log10 (e + 1e-30);
        };
        const double dark = bandDb (4000.0) - bandDb (500.0);
        std::snprintf (what, sizeof what, "Reverb Time %.1f: 4 kHz sits 15 to 40 dB under 500 Hz early on", time);
        expect (dark < -15.0 && dark > -40.0, what, dark);
        std::snprintf (what, sizeof what, "Reverb Time %.1f: treble and bass decay evenly (high/low)", time);
        expect (d.high / d.low > 0.5 && d.high / d.low < 1.3, what, d.high / d.low);
        std::snprintf (what, sizeof what, "Reverb Time %.1f: left/right correlation between 0.1 and 0.45", time);
        const double c = correlation (r, 0.3f, 3.0f);
        expect (c > 0.1 && c < 0.45, what, c);
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

    // ---- 4. Soup holds a note in tune, pure and steady ----
    std::printf ("Soup\n");
    for (float modify : { 0.2f, 0.5f })
    {
        // the note fades in and out over 50 ms, so no splash from a hard edge lands in the tail
        const auto r = run (7.0f, [] (int i)
        {
            const float t = (float) i / kFs, env = std::min ({ 1.0f, t / 0.05f, std::max (0.0f, (3.0f - t) / 0.05f) });
            return 0.3f * env * std::sin (6.2831853f * 220.0f * t);
        },
                            [modify] (float, EngineParams& p)
        {
            p.mode = 1; p.wetOn = true; p.murkWet = 0; p.time = 0.8f; p.wetModify = modify; p.mix = 1.0f; p.dryKill = true;
        });
        // the note's band (+/-3% of 220 Hz) from DFT bins one resolution step apart: its power
        // (summed, over the Hann window's 1.5-bin noise bandwidth) and its centroid in cents.
        // Soup drifts on purpose, so a single narrow bin would undercount a slightly broad line.
        const double res = 1.0 / 2.4;
        double bandP = 0.0, moment = 0.0;
        for (double hz = 220.0 * 0.97; hz <= 220.0 * 1.03; hz += res)
        {
            const double pw = 0.5 * std::pow (10.0, levelAt (r.l, (float) hz, 3.3f, 2.4f) / 10.0);
            bandP += pw;
            moment += pw * hz;
        }
        const double cents = 1200.0 * std::log2 (moment / bandP / 220.0);
        bandP /= 1.5;
        // total power, Hann-weighted like the band measurement so the decay counts the same in both
        double total = 0.0, wsum = 0.0;
        const int n0 = (int) (3.3f * kFs), nw = (int) (2.4f * kFs);
        for (int i = 0; i < nw; ++i)
        {
            const double w = 0.5 - 0.5 * std::cos (2.0 * 3.14159265 * i / nw);
            total += w * w * (double) r.l[(size_t) (n0 + i)] * r.l[(size_t) (n0 + i)];
            wsum += w * w;
        }
        total /= wsum;
        const double purity = 10.0 * std::log10 (bandP / std::max (1e-20, total - bandP));
        char what[96];
        std::snprintf (what, sizeof what, "Soup Modify %.1f: a held 220 Hz note stays within 5 cents", modify);
        expect (std::fabs (cents) < 5.0, what, cents);
        if (modify < 0.3f)
        {
            std::snprintf (what, sizeof what, "Soup Modify %.1f: and at least 15 dB pure", modify);
            expect (purity > 15.0, what, purity);
        }
        // each side's level follows a smooth decay (whole-cycle windows, residual from a line)
        std::vector<double> lv;
        const int w = (int) (kFs / 220.0f * 20.0f);
        for (int i = (int) (3.3f * kFs); i + w < (int) (5.7f * kFs); i += w)
        {
            double e = 0.0;
            for (int k = 0; k < w; ++k) e += (double) r.l[(size_t) (i + k)] * r.l[(size_t) (i + k)];
            lv.push_back (10.0 * std::log10 (e / w + 1e-20));
        }
        double sx = 0, sy = 0, sxx = 0, sxy = 0;
        for (size_t i = 0; i < lv.size(); ++i) { sx += (double) i; sy += lv[i]; sxx += (double) i * i; sxy += (double) i * lv[i]; }
        const double n = (double) lv.size(), slope = (n * sxy - sx * sy) / (n * sxx - sx * sx), icpt = (sy - slope * sx) / n;
        double wob = 0.0;
        for (size_t i = 0; i < lv.size(); ++i) wob = std::max (wob, std::fabs (lv[i] - (icpt + slope * (double) i)));
        std::snprintf (what, sizeof what, "Soup Modify %.1f: each side decays smoothly, within 1.5 dB", modify);
        expect (wob < 1.5, what, wob);
    }

    std::printf ("\n%s (%d failures)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
