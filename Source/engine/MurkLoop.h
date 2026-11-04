#pragma once

// Murk's micro-looper playback: Burst, Radio and Mask. The loop buffer, capture,
// replace and overdub stay exactly as in Murmur's LoopChannel; only playback changes.
// Everything here reads the loop through a function, rd (position in loop samples),
// so it never touches the buffer layout.
//
// Burst: the loop becomes a pattern of up to 8 steps, sliced where its own hits are.
//        Loud playing scrambles the pattern into fills (repeats, skips, reverses, stutters).
// Radio: five stations play the same loop. Modify tunes between them; between stations
//        the neighbours blend through light shortwave interference.
// Mask:  wherever the loop is louder than Modify's threshold it switches to a
//        transformed copy. Length sweeps the mask through five characters.

#include "DspUtil.h"
#include <array>
#include <utility>

namespace murmur
{
class MurkLoop
{
public:
    enum Mode { Burst = 0, Radio = 1, Mask = 2 };

    struct Params
    {
        float length = 0.5f, modify = 0.5f;
        float coreRate = 64000.0f;
        float beat = 0.0f;       // core samples per beat when tempo synced, else 0
        bool spread = false;
        float width = 1.0f;
    };

    static constexpr int kStations = 5;
    static const char* stationName (int i)
    {
        static const char* n[kStations] = { "TAPE", "AMBIENT", "ORCHESTRAL", "SHOEGAZE", "DANCE" };
        return n[std::max (0, std::min (kStations - 1, i))];
    }
    static float stationPos (float modify) { return clampf (modify, 0, 1) * (float) (kStations - 1); }

    static const char* maskName (int i)
    {
        static const char* n[5] = { "OCTAVE DOWN", "REVERSE", "CRUSH", "RING", "STUTTER" };
        return n[std::max (0, std::min (4, i))];
    }
    static float maskPos (float length) { return clampf (length, 0, 1) * 4.0f; }

    void reset()
    {
        pos = 0; stepAge = 0; step = 0; fillLeft = 0; nSlices = 8;
        for (int i = 0; i < 9; ++i) slices[(size_t) i] = 0;
        for (auto& s : st) s = {};
        noiseLp = 0; wobblePh = 0; dropGain = 1; dropTimer = 0;
        maskEnv = 0; maskGate = 0; maskOpen = false; maskAt = 0; maskAge = 0; maskHold = 0; crushHold = {}; crushCount = 0;
        ringPh = 0; bpZ1 = bpZ2 = {};
        uiPos = 0;
    }

    // ---- on capture: Burst finds the loop's own hits and slices there ----
    template <class Abs>
    void analyse (int L, const Abs& absAt)
    {
        constexpr int B = 256;
        const int blocks = std::max (1, L / B);
        // energy per block and its rise (onset strength)
        std::array<std::pair<float, int>, 64> best {};
        int nBest = 0;
        float prev = 0.0f;
        for (int b = 0; b < blocks; ++b)
        {
            float e = 0.0f;
            const int stride = 4;
            for (int i = b * B; i < (b + 1) * B && i < L; i += stride) e = std::max (e, absAt (i));
            const float rise = e - prev;
            prev = e;
            if (rise > 0.02f && b > 0)
            {
                // keep the strongest rises, no closer than 1/16 of the loop
                const int at = b * B;
                bool merged = false;
                for (int k = 0; k < nBest; ++k)
                    if (std::abs (best[(size_t) k].second - at) < L / 16)
                    {
                        if (rise > best[(size_t) k].first) best[(size_t) k] = { rise, at };
                        merged = true;
                        break;
                    }
                if (! merged && nBest < 64) best[(size_t) nBest++] = { rise, at };
            }
        }
        std::sort (best.begin(), best.begin() + nBest, [] (auto& a, auto& b) { return a.first > b.first; });
        std::array<int, 8> cuts {};
        int n = 0;
        cuts[(size_t) n++] = 0;
        for (int k = 0; k < nBest && n < 8; ++k) cuts[(size_t) n++] = best[(size_t) k].second;
        std::sort (cuts.begin(), cuts.begin() + n);
        if (n < 3) // nothing clear to slice on: eight equal steps
        {
            n = 8;
            for (int k = 0; k < 8; ++k) cuts[(size_t) k] = (int) ((int64_t) L * k / 8);
        }
        nSlices = n;
        for (int k = 0; k < n; ++k) slices[(size_t) k] = (float) cuts[(size_t) k];
        slices[(size_t) n] = (float) L;
        step = 0; stepAge = 0; fillLeft = 0; pos = 0;
        playSlice = 0; playDir = 1.0f; playStart = 0;
    }

    template <class Read>
    Stereo process (int mode, float L, float inputEnv, const Params& p, const Read& rd)
    {
        switch (mode)
        {
            case Burst: return burst (L, inputEnv, p, rd);
            case Radio: return radio (L, p, rd);
            default:    return mask (L, p, rd);
        }
    }

    float uiPos = 0;    // playhead, loop samples
    int uiFill = 0;     // Burst: steps of fill left

private:
    static float wrap (float x, float len)
    {
        if (x >= len || x < 0.0f) x -= std::floor (x / len) * len;
        return x >= len ? 0.0f : x;
    }

    // ---- Burst ------------------------------------------------------------------------
    template <class Read>
    Stereo burst (float L, float inputEnv, const Params& p, const Read& rd)
    {
        // Length sets step speed and size: tempo synced it picks a note value, otherwise
        // a share of each slice from a quarter (fast, choppy) to the whole slice
        const float len = clampf (p.length, 0, 1);
        float D;
        if (p.beat > 0.0f)
        {
            static constexpr float beats[5] = { 0.125f, 0.25f, 0.5f, 1.0f, 2.0f };
            D = p.beat * beats[std::min (4, (int) (len * 5.0f))];
        }
        else
        {
            D = (L / (float) nSlices) * lerpf (0.25f, 1.0f, len);
        }
        D = std::max (D, 256.0f);

        // Modify: how easily your playing scrambles the pattern. Fully down never does.
        const float m = clampf (p.modify, 0, 1);
        const float thr = m < 0.02f ? 99.0f : dbToGain (lerpf (-12.0f, -52.0f, m));

        if (stepAge >= D || stepAge == 0.0f)
        {
            if (stepAge >= D) { step = (step + 1) % nSlices; }
            stepAge = 0.0f;
            if (inputEnv > thr && fillLeft == 0) fillLeft = 2 + (int) (rng.next() * 1.5f + 1.5f + m * 3.0f);
            int s = step;
            playDir = 1.0f;
            stutter = false;
            if (fillLeft > 0)
            {
                --fillLeft;
                const float r = 0.5f + 0.5f * rng.next();
                if (r < 0.3f)      s = lastSlice;                                          // repeat
                else if (r < 0.5f) s = (step + 1 + (int) (r * 7.0f)) % nSlices;          // skip ahead
                else if (r < 0.75f) playDir = -1.0f;                                      // reversed
                else               stutter = true;                                        // stutter
            }
            playSlice = s;
            lastSlice = s;
            playStart = slices[(size_t) s];
        }

        const float sliceLen = std::max (64.0f, slices[(size_t) playSlice + 1] - slices[(size_t) playSlice]);
        const float within = stutter ? std::fmod (stepAge, std::max (64.0f, D * 0.25f)) : std::min (stepAge, sliceLen - 1.0f);
        const float x = playDir > 0.0f ? playStart + within : playStart + sliceLen - 1.0f - within;
        const float fl = std::min (96.0f, D * 0.1f);
        const float edge = std::min (stepAge, D - stepAge);
        const float g = std::min (1.0f, edge / fl) * (within >= sliceLen - 2.0f && ! stutter ? 0.0f : 1.0f);
        Stereo v = rd (wrap (x, L)) * g;
        stepAge += 1.0f;
        uiPos = wrap (x, L);
        uiFill = fillLeft;

        if (p.spread) // each step lands somewhere new
        {
            const float pan = std::sin ((float) playSlice * 2.3f) * clampf (p.width, 0, 1);
            v = panMono (0.5f * (v.l + v.r), pan);
        }
        return v;
    }

    // ---- Radio ------------------------------------------------------------------------
    struct Station { float pos = 0, phase = 0, swell[6] {}, swellPh[6] {}, cloud[4] {}, cloudAge[4] {}; int danceStep = 0; float danceAge = 0; };

    template <class Read>
    Stereo station (int i, float L, const Params& p, const Read& rd)
    {
        auto& s = st[(size_t) i];
        const float len = clampf (p.length, 0, 1);
        switch (i)
        {
            case 0: // Tape: the loop in full, Length sets speed and direction in octave steps
            {
                static constexpr float rates[8] = { -2.0f, -1.0f, -0.5f, -0.25f, 0.25f, 0.5f, 1.0f, 2.0f };
                const float rate = rates[std::min (7, (int) (len * 8.0f))];
                s.pos = wrap (s.pos + rate, L);
                return rd (s.pos);
            }
            case 1: // Ambient: slowed right down, pitch kept (overlapping grains)
            {
                const float speed = lerpf (0.03f, 0.6f, len * len);
                s.pos = wrap (s.pos + speed, L);
                const float G = std::min (L * 0.5f, 6144.0f);
                s.phase += 1.0f / G;
                if (s.phase >= 1.0f) s.phase -= 1.0f;
                const float ph2 = s.phase + 0.5f - std::floor (s.phase + 0.5f);
                const float a = std::sin (kPi * s.phase), b = std::sin (kPi * ph2);
                return rd (wrap (s.pos + s.phase * G, L)) * (a * a) + rd (wrap (s.pos + ph2 * G - G * 0.5f, L)) * (b * b);
            }
            case 2: // Orchestral: 1 to 6 voices swell in and out, unison, octaves and fifths
            {
                static constexpr float semis[6] = { 0.0f, 12.0f, 7.0f, -12.0f, 19.0f, -5.0f };
                const int voices = 1 + std::min (5, (int) (len * 6.0f));
                s.pos = wrap (s.pos + 1.0f, L);
                Stereo o;
                for (int v = 0; v < voices; ++v)
                {
                    const float rate = std::pow (2.0f, (semis[v] + 0.06f * (float) (v % 3 - 1)) / 12.0f);
                    s.swellPh[v] += (0.07f + 0.031f * (float) v) / p.coreRate;
                    if (s.swellPh[v] > 1.0f) s.swellPh[v] -= 1.0f;
                    const float sw = 0.5f - 0.5f * std::cos (kTwoPi * s.swellPh[v]);
                    const Stereo x = rd (wrap (s.pos * rate + (float) v * 997.0f, L));
                    const float pan = p.spread ? (v % 2 == 0 ? -0.6f : 0.6f) * clampf (p.width, 0, 1) : 0.0f;
                    o = o + panMono (0.5f * (x.l + x.r), pan) * (sw * (v == 0 ? 1.0f : 0.7f));
                }
                return o * (1.0f / std::sqrt ((float) voices));
            }
            case 3: // Shoegaze: freeze the chosen moment as a blurred cloud, new moments layer on
            {
                const float centre = len * L;
                const float G = std::min (L * 0.25f, 8192.0f);
                Stereo o;
                for (int k = 0; k < 4; ++k)
                {
                    s.cloudAge[k] += 1.0f;
                    if (s.cloudAge[k] >= G || s.cloud[k] == 0.0f)
                    {
                        s.cloudAge[k] = s.cloud[k] == 0.0f ? G * (float) k / 4.0f : 0.0f;
                        // each new grain lands a little further on from the chosen moment
                        s.danceAge = wrap (s.danceAge + G * 0.07f, L * 0.2f);
                        s.cloud[k] = 1.0f + wrap (centre + s.danceAge + rng.next() * G * 0.5f, L);
                    }
                    const float w = std::sin (kPi * s.cloudAge[k] / G);
                    o = o + rd (wrap (s.cloud[k] - 1.0f + s.cloudAge[k] * 0.5f, L)) * (w * w * 0.6f);
                }
                return o;
            }
            default: // Dance: rotates between half, double and normal speed
            {
                static constexpr float pattern[4] = { 1.0f, 2.0f, 1.0f, 0.5f };
                const float D = p.beat > 0.0f ? p.beat * (len < 0.33f ? 2.0f : (len < 0.66f ? 1.0f : 0.5f))
                                              : L * lerpf (0.5f, 0.0625f, len);
                s.danceAge += 1.0f;
                if (s.danceAge >= std::max (512.0f, D)) { s.danceAge = 0.0f; s.danceStep = (s.danceStep + 1) % 4; }
                s.pos = wrap (s.pos + pattern[s.danceStep], L);
                return rd (s.pos);
            }
        }
    }

    template <class Read>
    Stereo radio (float L, const Params& p, const Read& rd)
    {
        const float sp = stationPos (p.modify);
        const int a = std::min (kStations - 1, (int) sp);
        const int b = std::min (kStations - 1, a + 1);
        const float f = sp - (float) a;

        // only the one or two stations you can hear run
        Stereo o = station (a, L, p, rd) * std::cos (f * kPi * 0.5f);
        if (b != a && f > 0.001f) o = o + station (b, L, p, rd) * std::sin (f * kPi * 0.5f);
        uiPos = st[(size_t) (f < 0.5f ? a : b)].pos;

        // between stations: light shortwave interference, clean on a station
        const float between = clampf ((0.5f - std::fabs (f - 0.5f)) * 2.0f, 0, 1);
        const float amt = between * between * (b != a ? 1.0f : 0.0f);
        if (amt > 0.001f)
        {
            noiseLp += 0.25f * (rng.next() - noiseLp);                           // hiss
            wobblePh += (5.0f + 3.0f * std::sin (wobblePh * 0.013f)) / p.coreRate;
            if (wobblePh > 1000.0f) wobblePh -= 1000.0f;
            const float wob = 1.0f + 0.35f * amt * std::sin (kTwoPi * wobblePh); // fluttering level
            if (--dropTimer <= 0)                                                // the odd dropout
            {
                dropTimer = (int) (p.coreRate * (0.08f + 0.25f * (0.5f + 0.5f * rng.next())));
                dropTarget = rng.next() > 0.55f ? 1.0f - 0.85f * amt : 1.0f;
            }
            dropGain += 0.002f * (dropTarget - dropGain);
            const float hiss = noiseLp * 0.06f * amt;
            o = { o.l * wob * dropGain + hiss, o.r * wob * dropGain - hiss };
        }
        return o;
    }

    // ---- Mask -------------------------------------------------------------------------
    template <class Read>
    Stereo maskChar (int c, float L, const Read& rd, Stereo plain)
    {
        switch (c)
        {
            case 0: return rd (wrap (maskAt + maskAge * 0.5f, L));                    // octave down
            case 1: return rd (wrap (maskAt - maskAge, L));                           // reversed
            case 2: // bit-crushed, sample-and-hold
            {
                if (crushCount-- <= 0) { crushCount = 7; crushHold = plain; }
                const float q = 12.0f;
                return { std::round (crushHold.l * q) / q, std::round (crushHold.r * q) / q };
            }
            case 3: // ring-modulated radio: ring mod then a narrow band
            {
                ringPh += 420.0f / 64000.0f;
                if (ringPh > 1.0f) ringPh -= 1.0f;
                const float rm = std::sin (kTwoPi * ringPh);
                const Stereo x { plain.l * rm, plain.r * rm };
                // a state-variable band-pass round 1.8 kHz: bpZ1 is the band, bpZ2 the low
                constexpr float f = 0.18f, q = 0.5f;
                bpZ2 = bpZ2 + bpZ1 * f;
                const Stereo hp { x.l - bpZ2.l - q * bpZ1.l, x.r - bpZ2.r - q * bpZ1.r };
                bpZ1 = bpZ1 + hp * f;
                return bpZ1 * 1.6f;
            }
            default: // stutter: the first 60 ms after the mask opens, over and over
            {
                const float S = std::max (256.0f, 0.06f * 64000.0f);
                return rd (wrap (maskAt + std::fmod (maskAge, S), L));
            }
        }
    }

    template <class Read>
    Stereo mask (float L, const Params& p, const Read& rd)
    {
        pos = wrap (pos + 1.0f, L);
        const Stereo plain = rd (pos);
        uiPos = pos;

        // the loop's own level opens the mask; fully down it never opens
        const float m = clampf (p.modify, 0, 1);
        const float lvl = std::max (std::fabs (plain.l), std::fabs (plain.r));
        maskEnv = lvl > maskEnv ? lvl + 0.97f * (maskEnv - lvl) : lvl + 0.9995f * (maskEnv - lvl);
        const float thr = m < 0.02f ? 99.0f : dbToGain (lerpf (-6.0f, -48.0f, m));
        const bool open = maskEnv > thr;
        if (open && ! maskOpen) { maskAt = pos; maskAge = 0; }
        if (open) maskHold = 0.03f * p.coreRate;     // hangs on briefly so it doesn't chatter
        else maskHold -= 1.0f;
        maskOpen = open || maskHold > 0.0f;
        maskAge += 1.0f;
        maskGate += clampf ((maskOpen ? 1.0f : 0.0f) - maskGate, -1.0f / 96.0f, 1.0f / 96.0f);
        if (maskGate <= 0.0f) return plain;

        // Length sweeps through the five characters, blending neighbours as it turns
        const float mp = maskPos (p.length);
        const int a = std::min (4, (int) mp), b = std::min (4, a + 1);
        const float f = mp - (float) a;
        Stereo t = maskChar (a, L, rd, plain) * (1.0f - f);
        if (b != a && f > 0.001f) t = t + maskChar (b, L, rd, plain) * f;
        return plain * (1.0f - maskGate) + t * maskGate;
    }

    // ---- state ------------------------------------------------------------------------
    float pos = 0;
    std::array<float, 9> slices {};
    int nSlices = 8, step = 0, fillLeft = 0, playSlice = 0, lastSlice = 0;
    float stepAge = 0, playStart = 0, playDir = 1;
    bool stutter = false;

    std::array<Station, kStations> st {};
    float noiseLp = 0, wobblePh = 0, dropGain = 1, dropTarget = 1;
    int dropTimer = 0;

    float maskEnv = 0, maskGate = 0, maskAt = 0, maskAge = 0, maskHold = 0, ringPh = 0;
    bool maskOpen = false;
    Stereo crushHold, bpZ1, bpZ2;
    int crushCount = 0;
    Rng rng;
};
} // namespace murmur
