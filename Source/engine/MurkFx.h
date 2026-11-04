#pragma once

// Murk's global stages: Glue (a saturator and destroyer after both channels),
// Cross (one signal's level makes another dip and wobble) and the EQ tilt.

#include "DspUtil.h"

namespace murmur
{
// Glue: low settings warm and bind the channels with gentle compression and tape-style
// saturation, the middle drives harder and lopsided, the top folds back and bit-crushes.
// Runs at the host rate on the wet signal (or on everything with Dry Glue).
class Glue
{
public:
    void reset() { hold = {}; holdCount = 0; env = 0.0f; dcL = dcR = 0.0f; }

    Stereo process (Stereo x, float amount)
    {
        const float a = clampf (amount, 0, 1);
        if (a < 0.001f) return x;

        // gentle level-following compression: louder passages are pushed into the drive less
        const float lvl = std::max (std::fabs (x.l), std::fabs (x.r));
        env = lvl > env ? lvl + 0.995f * (env - lvl) : lvl + 0.9998f * (env - lvl);
        const float comp = 1.0f / (1.0f + a * 2.0f * env);

        const float drive = dbToGain (a * 30.0f) * comp;
        const float bias = 0.18f * clampf ((a - 0.25f) * 2.0f, 0, 1);   // lopsided above a quarter
        auto sat = [&] (float v) { return std::tanh (v * drive + bias) - std::tanh (bias); };
        Stereo y { sat (x.l), sat (x.r) };

        // the bias leaves a little DC: take it out
        dcL += 0.0005f * (y.l - dcL);
        dcR += 0.0005f * (y.r - dcR);
        y = { y.l - dcL, y.r - dcR };

        // level stays roughly even as the drive goes up: quiet parts rise a little, loud parts bind
        const float makeUp = 1.0f / std::pow (std::max (1.0f, drive), 0.82f);
        y = y * makeUp;

        if (a > 0.7f) // thrashed: foldback, fewer bits and a lower sample rate
        {
            const float k = (a - 0.7f) / 0.3f;
            auto fold = [&] (float v) { return std::sin (v * (1.0f + 3.0f * k) * kPi * 0.5f); };
            y = { lerpf (y.l, fold (y.l), k), lerpf (y.r, fold (y.r), k) };
            const float q = std::pow (2.0f, 15.0f - 11.0f * k);
            y = { std::round (y.l * q) / q, std::round (y.r * q) / q };
            const int down = 1 + (int) (k * k * 15.0f);
            if (holdCount-- <= 0) { hold = y; holdCount = down - 1; }
            y = hold;
        }
        return { softClip (y.l), softClip (y.r) };
    }

private:
    Stereo hold;
    int holdCount = 0;
    float env = 0, dcL = 0, dcR = 0;
};

// Cross: an envelope follower on the source moves the target's volume and the pitch.
// Gentle movement low down, stutters and dropouts near the top.
class Cross
{
public:
    void reset() { env = 0; chopPh = 0; volume = 1; pitch = 0; wobPh = 0; }

    // call once per core sample with the source signal; then read volume and pitch
    void tick (Stereo source, float amount, float coreRate)
    {
        const float a = clampf (amount, 0, 1);
        const float lvl = std::max (std::fabs (source.l), std::fabs (source.r));
        const float atk = std::exp (-1.0f / (0.004f * coreRate)), rel = std::exp (-1.0f / (0.12f * coreRate));
        env = lvl > env ? lvl + atk * (env - lvl) : lvl + rel * (env - lvl);
        if (a < 0.001f) { volume = 1.0f; pitch = 0.0f; return; }

        const float e = clampf (env * 4.0f, 0, 1);
        float v = 1.0f - 0.8f * a * e;
        if (a > 0.6f && e > 0.25f) // chopped into stutters, then full dropouts
        {
            chopPh += (8.0f + 16.0f * a) / coreRate;
            if (chopPh > 1.0f) chopPh -= 1.0f;
            const float depth = clampf ((a - 0.6f) * 2.5f, 0, 1);
            if (chopPh > 0.5f) v *= 1.0f - depth;
        }
        volume += 0.01f * (v - volume);

        wobPh += 6.0f / coreRate;
        if (wobPh > 1.0f) wobPh -= 1.0f;
        pitch = -a * e * 0.08f + std::sin (kTwoPi * wobPh) * a * e * 0.03f; // octaves
    }

    float volume = 1.0f, pitch = 0.0f;

private:
    float env = 0, chopPh = 0, wobPh = 0;
};

// EQ tilt: one way darker, the other brighter, flat at noon (pivot round 800 Hz)
struct Tilt
{
    OnePole lp[2];
    float rate = 0.0f;
    void reset() { lp[0].reset(); lp[1].reset(); rate = 0.0f; }
    Stereo process (Stereo x, float tilt, float coreRate)
    {
        const float t = clampf (tilt, -1, 1);
        if (std::fabs (t) < 0.01f) return x;
        if (coreRate != rate) { rate = coreRate; lp[0].setCutoff (800.0f, coreRate); lp[1].a = lp[0].a; }
        const float gl = dbToGain (-9.0f * t), gh = dbToGain (9.0f * t);
        const float lo = lp[0].process (x.l), ro = lp[1].process (x.r);
        return { lo * gl + (x.l - lo) * gh, ro * gl + (x.r - ro) * gh };
    }
};
} // namespace murmur
