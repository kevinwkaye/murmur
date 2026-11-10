#pragma once

// Small, dependency-free DSP helpers shared by the Murmur engine.
// Everything in Source/engine is plain C++17 so it can be tested without JUCE.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace murmur
{
constexpr float kPi = 3.14159265358979f;
constexpr float kTwoPi = 6.28318530717959f;

inline float clampf (float x, float lo, float hi) { return std::min (std::max (x, lo), hi); }
inline float lerpf (float a, float b, float t) { return a + (b - a) * t; }
inline float dbToGain (float db) { return std::pow (10.0f, db * 0.05f); }

// Transparent below 0.9, gently limits above. Keeps feedback paths from running away.
inline float softClip (float x)
{
    const float ax = std::fabs (x);
    if (ax <= 0.9f)
        return x;
    const float s = x > 0.0f ? 1.0f : -1.0f;
    return s * (0.9f + 0.1f * std::tanh ((ax - 0.9f) * 10.0f));
}

struct Stereo
{
    float l = 0.0f, r = 0.0f;
    Stereo operator+ (Stereo o) const { return { l + o.l, r + o.r }; }
    Stereo operator* (float g) const { return { l * g, r * g }; }
};

// Equal-power pan, pan in [-1, 1]
inline Stereo panMono (float v, float pan)
{
    const float a = (clampf (pan, -1.0f, 1.0f) + 1.0f) * (kPi * 0.25f);
    return { v * std::cos (a) * 1.41421356f, v * std::sin (a) * 1.41421356f };
}

struct OnePole
{
    float z = 0.0f, a = 0.0f;
    void setCutoff (float fc, float fs) { a = std::exp (-kTwoPi * clampf (fc, 1.0f, fs * 0.49f) / fs); }
    float process (float x) { z = x + a * (z - x); return z; }
    void reset() { z = 0.0f; }
};

// Second-order low-pass in transposed direct form II. Two sections with
// Q = 0.5411961 / 1.306563 form a maximally-flat fourth-order Butterworth.
struct LowpassBiquad
{
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
    float a1 = 0.0f, a2 = 0.0f;
    float z1 = 0.0f, z2 = 0.0f;

    void setCutoff (float fc, float fs, float q)
    {
        const float omega = kTwoPi * clampf (fc, 10.0f, fs * 0.45f) / fs;
        const float cs = std::cos (omega);
        const float alpha = std::sin (omega) / (2.0f * q);
        const float invA0 = 1.0f / (1.0f + alpha);
        b0 = (1.0f - cs) * 0.5f * invA0;
        b1 = (1.0f - cs) * invA0;
        b2 = b0;
        a1 = -2.0f * cs * invA0;
        a2 = (1.0f - alpha) * invA0;
    }

    float process (float x)
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    void reset() { z1 = z2 = 0.0f; }
};

struct Rng
{
    uint32_t s = 0x9E3779B9u;
    float next() // -1..1
    {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return (float) (s >> 8) * (2.0f / 16777216.0f) - 1.0f;
    }
};

// Power-of-two circular delay line with fractional reads.
// tap(1) returns the most recently pushed sample.
struct DelayLine
{
    std::vector<float> buf;
    int mask = 0, w = 0;

    void init (int sizePow2) { buf.assign ((size_t) sizePow2, 0.0f); mask = sizePow2 - 1; w = 0; }
    void clear() { std::fill (buf.begin(), buf.end(), 0.0f); w = 0; }
    int size() const { return mask + 1; }
    void push (float x) { buf[(size_t) w] = x; w = (w + 1) & mask; }

    float tap (float delay) const
    {
        delay = clampf (delay, 1.0f, (float) (mask - 1));
        float rp = (float) w - delay;
        if (rp < 0.0f) rp += (float) (mask + 1);
        const int i0 = (int) rp;
        const float f = rp - (float) i0;
        const float a = buf[(size_t) (i0 & mask)];
        const float b = buf[(size_t) ((i0 + 1) & mask)];
        return a + (b - a) * f;
    }

    float tapInt (int delay) const { return buf[(size_t) ((w - delay) & mask)]; }
};

// Schroeder allpass with an externally supplied (possibly modulated) delay.
struct Allpass
{
    DelayLine d;
    float g = 0.5f;
    void init (int sizePow2) { d.init (sizePow2); }
    void clear() { d.clear(); }
    float process (float x, float delay)
    {
        const float vd = d.tap (delay);
        const float v = x + g * vd;
        d.push (v);
        return vd - g * v;
    }
};
} // namespace murmur
