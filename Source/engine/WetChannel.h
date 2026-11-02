#pragma once

// The Wet Channel: Reverb (multi-tap cluster smeared into a reverb), Delay
// (clean, crossfaded time changes) and Slip (chunk delay that plays your last
// phrase back reversed or pitched), plus Freeze. All times are in CORE samples, so the global Clock
// stretches and pitches everything together.

#include "DspUtil.h"
#include <array>

namespace murmur
{
class WetChannel
{
public:
    enum Mode { Reverb = 0, Delay = 1, Slip = 2 };

    struct Params
    {
        int mode = Reverb;
        float time = 0.5f;
        float modify = 0.5f;
        bool freeze = false;
        bool spread = false;
        bool classic = false;
        float width = 1.0f;     // 0..2, only applied when spread is on
        float toneHz = 20000.0f;
        float coreRate = 64000.0f;
        int syncLen = 0;        // > 0: Time snaps to divisions of this length (wet follows loop)
        float fixedLen = 0.0f;  // > 0: tempo sync sets Delay and Slip time directly (core samples)
    };

    void prepare()
    {
        for (int c = 0; c < 2; ++c)
        {
            tapLine[c].init (1 << 16);
            dly[c].init (1 << 17);
            slipBuf[c].init (1 << 18);
            hist[c].init (1 << 18);
            comb[c].init (1 << 14);
            for (auto& a : diff[c]) a.init (1 << 13);
            for (auto& a : smear[c]) a.init (1 << 12);
            freezeBuf[c].assign ((size_t) kFreezeMax + kFreezeX + 4, 0.0f);
        }
        for (auto& f : fdn) f.init (1 << 15);
        reset();
    }

    void reset()
    {
        for (int c = 0; c < 2; ++c)
        {
            tapLine[c].clear(); dly[c].clear(); slipBuf[c].clear(); hist[c].clear(); comb[c].clear();
            for (auto& a : diff[c]) a.clear();
            for (auto& a : smear[c]) a.clear();
            std::fill (freezeBuf[c].begin(), freezeBuf[c].end(), 0.0f);
            tone[c].reset();
        }
        for (auto& f : fdn) f.clear();
        for (auto& z : fdnLp) z = 0.0f;
        lastTap = {};
        dA = dB = dCur = 4096.0f; xfade = 0;
        slipCur = {}; slipPrev = {}; slipCounter = 0.0f; slipPan = 0.0f;
        frozen = false; freezeGain = 0.0f; freezePos = 0.0f; freezeLen = 1024;
        for (auto& p : lfo) p = 0.0f;
    }

    // ---- time mappings (core samples) -------------------------------------------------
    static float reverbSize (float t) { return 600.0f + 23400.0f * std::pow (clampf (t, 0, 1), 1.5f); }
    static float delaySamples (float t) { return 256.0f * std::pow (2.0f, clampf (t, 0, 1) * 8.8f); }
    static float slipSize (float t) { return 128.0f * std::pow (2.0f, clampf (t, 0, 1) * 9.0f); } // 2 ms to 1 s at 64 kHz

    // Slip / frozen-Slip playback speed: semitone steps, 2x reverse ... neutral ... 2x forward
    static float slipSpeed (float m)
    {
        const int n = (int) std::lround ((clampf (m, 0, 1) * 2.0f - 1.0f) * 24.0f);
        if (n == 0)
            return 1.0f;
        const float mag = std::pow (2.0f, (float) (std::abs (n) - 12) / 12.0f);
        return n > 0 ? mag : -mag;
    }

    // Length of the current Time setting in core samples (used for channel sync)
    int timeSamples (const Params& p) const
    {
        switch (p.mode)
        {
            case Reverb: return (int) sizeFor (p);
            case Delay:  return (int) delayFor (p);
            default:     return (int) slipFor (p);
        }
    }

    Stereo process (Stereo in, const Params& p)
    {
        Stereo y;
        switch (p.mode)
        {
            case Reverb: y = reverb (in, p); break;
            case Delay:  y = delay (in, p); break;
            default:     y = slip (in, p); break;
        }

        hist[0].push (y.l);
        hist[1].push (y.r);

        // ---- freeze ----
        if (p.freeze && ! frozen) capture (p);
        frozen = p.freeze;
        const float target = frozen ? 1.0f : 0.0f;
        freezeGain += clampf (target - freezeGain, -1.0f / 256.0f, 1.0f / 256.0f);
        if (freezeGain > 0.0f)
        {
            const Stereo f = readFreeze (p);
            y = y * (1.0f - freezeGain) + f * freezeGain;
        }

        // ---- width (only meaningful with spread) and tone ----
        if (p.spread)
        {
            const float mid = 0.5f * (y.l + y.r);
            const float side = 0.5f * (y.l - y.r) * p.width;
            y = { mid + side, mid - side };
        }
        if (p.toneHz < 19500.0f)
        {
            tone[0].setCutoff (p.toneHz, p.coreRate);
            tone[1].a = tone[0].a;
            y = { tone[0].process (y.l), tone[1].process (y.r) };
        }
        return y;
    }

private:
    static constexpr int kFreezeMax = (1 << 17);
    static constexpr int kFreezeX = 512;
    static constexpr float kTapL[8] = { 0.11f, 0.19f, 0.27f, 0.38f, 0.47f, 0.58f, 0.71f, 0.83f };
    static constexpr float kTapR[8] = { 0.13f, 0.22f, 0.31f, 0.41f, 0.52f, 0.63f, 0.77f, 0.95f };
    static constexpr float kPan[8]  = { -1.0f, 0.8f, -0.6f, 1.0f, -0.9f, 0.5f, -0.7f, 0.9f };
    static constexpr float kDiffL[4] = { 0.031f, 0.047f, 0.071f, 0.097f };
    static constexpr float kDiffR[4] = { 0.029f, 0.053f, 0.067f, 0.101f };
    static constexpr float kFdn[4] = { 0.31f, 0.37f, 0.43f, 0.53f };
    static constexpr float kSmearD[4] = { 142.0f, 379.0f, 607.0f, 1051.0f };

    float synced (const Params& p, float lo, float hi) const
    {
        static constexpr int divs[8] = { 16, 12, 8, 6, 4, 3, 2, 1 };
        const int idx = (int) (clampf (p.time, 0, 1) * 7.999f);
        return clampf ((float) p.syncLen / (float) divs[idx], lo, hi);
    }
    float sizeFor (const Params& p) const { return p.syncLen > 0 ? synced (p, 600, 24000) : reverbSize (p.time); }
    float delayFor (const Params& p) const { return p.syncLen > 0 ? synced (p, 16, 130000) : clampf (p.fixedLen > 0 ? p.fixedLen : delaySamples (p.time), 16, 130000); }
    float slipFor (const Params& p) const { return p.syncLen > 0 ? synced (p, 64, 65536) : clampf (p.fixedLen > 0 ? p.fixedLen : slipSize (p.time), 64, 65536); }

    // ---- Reverb ------------------------------------------------------------------------
    Stereo reverb (Stereo in, const Params& p)
    {
        const float S = sizeFor (p);
        const float t = clampf (p.time, 0, 1);
        const float m = clampf (p.modify, 0, 1);
        const float fb = lerpf (0.25f, 0.85f, t);

        float inL = in.l, inR = in.r;
        if (p.spread)
            inL = inR = 0.5f * (in.l + in.r);

        tapLine[0].push (softClip (inL + fb * 0.3f * lastTap.r));
        tapLine[1].push (softClip (inR + fb * 0.3f * lastTap.l));

        float tl = 0.0f, tr = 0.0f, g = 0.42f;
        for (int i = 0; i < 8; ++i)
        {
            const float a = tapLine[0].tap (1.0f + S * kTapL[i]);
            const float b = tapLine[1].tap (1.0f + S * kTapR[i]);
            if (p.spread)
            {
                const Stereo s = panMono (0.5f * (a + b), kPan[i] * clampf (p.width, 0, 1));
                tl += g * s.l; tr += g * s.r;
            }
            else
            {
                tl += g * a; tr += g * b;
            }
            g *= 0.9f;
        }
        lastTap = { tl, tr };

        // Diffusion: smear the taps into a wash. At modify = 0 this is a pure delay.
        float dl = tl, dr = tr;
        for (int j = 0; j < 4; ++j)
        {
            diff[0][j].g = 0.7f * m;
            diff[1][j].g = 0.7f * m;
            dl = diff[0][j].process (dl, 1.0f + S * kDiffL[j]);
            dr = diff[1][j].process (dr, 1.0f + S * kDiffR[j]);
        }

        // 4-line feedback delay network for the tail, gently modulated
        const float fdnFb = lerpf (0.6f, 0.98f, t);
        const float depth = 4.0f + 10.0f * (S / 24000.0f);
        float o[4];
        for (int k = 0; k < 4; ++k)
        {
            lfo[k] += (0.00011f + 0.00003f * (float) k);
            if (lfo[k] > 1.0f) lfo[k] -= 1.0f;
            o[k] = fdn[k].tap (S * kFdn[k] + 8.0f + depth * (1.0f + std::sin (kTwoPi * lfo[k])));
        }
        const float h0 = 0.5f * (o[0] + o[1] + o[2] + o[3]);
        const float h1 = 0.5f * (o[0] - o[1] + o[2] - o[3]);
        const float h2 = 0.5f * (o[0] + o[1] - o[2] - o[3]);
        const float h3 = 0.5f * (o[0] - o[1] - o[2] + o[3]);
        const float hin[4] = { h0, h1, h2, h3 };
        const float inG = m * 0.6f * (1.0f - 0.6f * fdnFb); // keeps long tails from piling up
        const float fin[4] = { dl * inG, dr * inG, -dl * inG, -dr * inG };
        for (int k = 0; k < 4; ++k)
        {
            fdnLp[k] = hin[k] + 0.22f * (fdnLp[k] - hin[k]);
            fdn[k].push (softClip (fin[k] + fdnFb * fdnLp[k]));
        }
        const float tailL = 0.5f * (o[0] + o[2]);
        const float tailR = 0.5f * (o[1] + o[3]);

        Stereo out {
            lerpf (tl * 1.1f, dl * 0.8f + tailL * 1.6f, m),
            lerpf (tr * 1.1f, dr * 0.8f + tailR * 1.6f, m)
        };

        if (p.classic) // resonant, ringy trails
        {
            const int D = (int) (S * 0.061f) + 23;
            const float rl = out.l + 0.5f * comb[0].tapInt (D);
            const float rr = out.r + 0.5f * comb[1].tapInt (D + 7);
            comb[0].push (softClip (rl));
            comb[1].push (softClip (rr));
            out = { rl * 0.7f, rr * 0.7f };
        }
        return out;
    }

    // ---- Delay -------------------------------------------------------------------------
    Stereo delay (Stereo in, const Params& p)
    {
        const float target = delayFor (p);
        const float fb = clampf (p.modify, 0, 1);
        Stereo o;

        if (p.classic)
        {
            // Bendy and warped: the read head glides, so echoes pitch-bend
            dCur = target + 0.9996f * (dCur - target);
            o = { dly[0].tap (dCur), dly[1].tap (dCur) };
            dA = dB = dCur;
            xfade = 0;
        }
        else
        {
            // Crisp: jump to the new time with a short crossfade, no pitch bend
            constexpr int XF = 1024;
            if (xfade == 0 && std::fabs (target - dA) > 0.5f)
            {
                dB = target;
                xfade = 1;
            }
            if (xfade > 0)
            {
                const float k = (float) xfade / (float) XF;
                o = { dly[0].tap (dA) * (1.0f - k) + dly[0].tap (dB) * k,
                      dly[1].tap (dA) * (1.0f - k) + dly[1].tap (dB) * k };
                if (++xfade > XF) { dA = dB; xfade = 0; }
            }
            else
            {
                o = { dly[0].tap (dA), dly[1].tap (dA) };
            }
            dCur = dA;
        }

        if (p.spread)
        {
            // Ping-pong that mirrors your panning: each echo swaps sides, so a sound 30%
            // right alternates between 30% right and 30% left. A centred (mono) input has
            // nothing to mirror, so it is fed to one side to start a classic ping-pong.
            const float mid = 0.5f * (in.l + in.r), side = 0.5f * (in.l - in.r);
            const float a = std::fabs (mid), b = std::fabs (side);
            monoEnv += 0.001f * ((a > 1e-6f ? clampf (1.0f - 4.0f * b / (a + 1e-6f), 0, 1) : monoEnv) - monoEnv);
            const float inL = lerpf (in.l, mid * 2.0f, monoEnv);
            const float inR = lerpf (in.r, 0.0f, monoEnv);
            dly[0].push (softClip (inL + fb * o.r));
            dly[1].push (softClip (inR + fb * o.l));
        }
        else
        {
            dly[0].push (softClip (in.l + fb * o.l));
            dly[1].push (softClip (in.r + fb * o.r));
        }
        return o;
    }

    // ---- Slip --------------------------------------------------------------------------
    // Slip is a chunk delay. Every S samples (Time sets S, the "refresh rate") it grabs
    // the chunk you just played and plays it back over the next S samples at the chosen
    // speed and direction. At 1x forward it is a plain delay of S. Reverse gives reverse
    // delay echoes, other speeds give pitched echoes that follow behind you. Small sizes
    // refresh so fast that it behaves like a pitch shifter.
    struct SlipPlayer
    {
        float age = 0;      // samples since this chunk started playing
        float x = 0;        // read position inside the chunk (0 = its oldest sample)
        float S = 1024;     // chunk length when captured
        float F = 64;       // crossfade length
        float retired = -1; // samples since being replaced (-1 = still current)
        bool active = false;
    };

    void startChunk (float S, float speed, bool classic)
    {
        slipPrev = slipCur;
        if (slipPrev.active) slipPrev.retired = 0.0f;
        slipCur = {};
        slipCur.active = true;
        slipCur.S = S;
        slipCur.F = classic ? std::min (16.0f, S * 0.25f) : std::min (512.0f, S * 0.25f);
        slipCur.x = speed >= 0.0f ? 0.0f : S;
        slipCounter = 0.0f;
    }

    // Read chunk position x of a player. delay = age + S - x puts x = 0 at the
    // oldest sample of the chunk it captured.
    float readChunk (const DelayLine& d, const SlipPlayer& pl, float x, bool classic) const
    {
        const float delay = std::max (1.0f, pl.age + pl.S - x);
        return classic ? d.tapInt ((int) delay) : d.tap (delay);
    }

    Stereo playChunk (const SlipPlayer& pl, float speed, bool classic) const
    {
        Stereo v { readChunk (slipBuf[0], pl, pl.x, classic), readChunk (slipBuf[1], pl, pl.x, classic) };

        // Faster than 1x the chunk loops inside itself, so crossfade across each wrap
        if (std::fabs (speed) > 1.0f)
        {
            const float F = pl.F;
            float k = 0.0f, alt = 0.0f;
            if (speed > 0.0f && pl.x > pl.S - F) { k = (pl.x - (pl.S - F)) / F; alt = pl.x - pl.S; }
            if (speed < 0.0f && pl.x < F)        { k = (F - pl.x) / F;          alt = pl.x + pl.S; }
            if (k > 0.0f)
            {
                const Stereo w { readChunk (slipBuf[0], pl, alt, classic), readChunk (slipBuf[1], pl, alt, classic) };
                v = v * (1.0f - k) + w * k;
            }
        }
        return v;
    }

    static float fadeIn (float t) { const float s = std::sin (0.5f * kPi * clampf (t, 0, 1)); return s * s; }

    Stereo slip (Stereo in, const Params& p)
    {
        const float S = slipFor (p);
        const float speed = slipSpeed (p.modify);

        slipBuf[0].push (in.l);
        slipBuf[1].push (in.r);

        slipCounter += 1.0f;
        if (! slipCur.active || slipCounter >= S)
            startChunk (S, speed, p.classic);

        Stereo o;
        for (SlipPlayer* pl : { &slipCur, &slipPrev })
        {
            if (! pl->active) continue;
            float g = fadeIn (pl->age / pl->F);
            if (pl->retired >= 0.0f)
            {
                g *= 1.0f - fadeIn (pl->retired / pl->F);
                if (pl->retired >= pl->F) { pl->active = false; continue; }
            }
            o = o + playChunk (*pl, speed, p.classic) * g;

            // advance
            pl->age += 1.0f;
            if (pl->retired >= 0.0f) pl->retired += 1.0f;
            pl->x += speed;
            if (std::fabs (speed) > 1.0f && (pl->x >= pl->S || pl->x < 0.0f))
                pl->x -= std::floor (pl->x / pl->S) * pl->S;
        }

        if (p.spread) // smooth auto-pan, speed set by Time (and Clock)
        {
            slipPan += 1.0f / (4.0f * S);
            if (slipPan > 1.0f) slipPan -= 1.0f;
            o = panMono (0.5f * (o.l + o.r), std::sin (kTwoPi * slipPan) * clampf (p.width, 0, 1));
        }
        return o;
    }

    // ---- Freeze ------------------------------------------------------------------------
    void capture (const Params& p)
    {
        int len = timeSamples (p);
        if (p.mode == Delay) len = (int) delayFor (p);
        freezeLen = std::min (std::max (len, 512), kFreezeMax);
        const int total = freezeLen + kFreezeX;
        for (int c = 0; c < 2; ++c)
        {
            auto& b = freezeBuf[c];
            for (int i = 0; i < total; ++i)
                b[(size_t) i] = hist[c].tapInt (total - i);
            // bake a crossfade so the loop point is seamless
            for (int i = 0; i < kFreezeX; ++i)
            {
                const float k = (float) i / (float) kFreezeX;
                b[(size_t) i] = b[(size_t) i] * k + b[(size_t) (freezeLen + i)] * (1.0f - k);
            }
        }
        freezePos = 0.0f;
        for (int c = 0; c < 2; ++c)
            for (auto& a : smear[c]) a.clear();
    }

    Stereo readFreeze (const Params& p)
    {
        const float rate = (p.mode == Slip) ? slipSpeed (p.modify) : 1.0f;
        const float L = (float) freezeLen;
        freezePos += rate;
        while (freezePos >= L) freezePos -= L;
        while (freezePos < 0.0f) freezePos += L;

        const int i0 = (int) freezePos;
        const int i1 = (i0 + 1 >= freezeLen) ? 0 : i0 + 1;
        const float f = freezePos - (float) i0;
        Stereo o {
            freezeBuf[0][(size_t) i0] + (freezeBuf[0][(size_t) i1] - freezeBuf[0][(size_t) i0]) * f,
            freezeBuf[1][(size_t) i0] + (freezeBuf[1][(size_t) i1] - freezeBuf[1][(size_t) i0]) * f
        };

        // Reverb: turning up Modify while frozen smudges the pattern, and it is written
        // back into the frozen loop, so there's no going back.
        if (p.mode == Reverb && p.modify > 0.02f && frozen)
        {
            for (int j = 0; j < 4; ++j)
            {
                smear[0][j].g = 0.55f * p.modify;
                smear[1][j].g = 0.55f * p.modify;
                o.l = smear[0][j].process (o.l, kSmearD[j]);
                o.r = smear[1][j].process (o.r, kSmearD[j] + 13.0f);
            }
            freezeBuf[0][(size_t) i0] = o.l;
            freezeBuf[1][(size_t) i0] = o.r;
        }
        return o;
    }

    // ---- state -------------------------------------------------------------------------
    DelayLine tapLine[2], dly[2], slipBuf[2], hist[2], comb[2];
    std::array<Allpass, 4> diff[2], smear[2];
    std::array<DelayLine, 4> fdn;
    float fdnLp[4] {};
    float lfo[4] {};
    Stereo lastTap;
    OnePole tone[2];

    float dA = 4096, dB = 4096, dCur = 4096;
    int xfade = 0;

    float monoEnv = 1.0f;
    SlipPlayer slipCur, slipPrev;
    float slipCounter = 0, slipPan = 0;

    std::vector<float> freezeBuf[2];
    bool frozen = false;
    float freezeGain = 0, freezePos = 0;
    int freezeLen = 1024;
};
} // namespace murmur
