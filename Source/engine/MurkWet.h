#pragma once

// Murk's Wet Channel: Soup, Relay and Flip. Like the Murmur modes these run at the
// CORE rate, so the Clock slows, lengthens and lowers everything together.
//
// Soup:  a spectral reverb. The input is split into 256 narrow bands; each band keeps
//        its level but is re-synthesised with fresh phase as it decays, giving a smeared,
//        re-synthesised wash. Modify runs from natural to synthetic (coarse bands, random phase).
// Relay: a delay that doesn't fade. 1 to 16 repeats, all at the same level. At the top
//        the repeats never stop and stack up like a looper.
// Flip:  a chordcaster. Up to four pitch-shifted voices, each entering one lag later.
//        Modify steps through 48 chords built from 4ths, 5ths and octaves.

#include "DspUtil.h"
#include <array>
#include <complex>

namespace murmur
{
// ---- a small radix-2 FFT (N fixed at construction) ------------------------------------
class SmallFFT
{
public:
    using C = std::complex<float>;
    explicit SmallFFT (int n) : N (n)
    {
        twiddle.resize ((size_t) N / 2);
        for (int k = 0; k < N / 2; ++k)
            twiddle[(size_t) k] = std::polar (1.0f, -kTwoPi * (float) k / (float) N);
        rev.resize ((size_t) N);
        int bits = 0;
        while ((1 << bits) < N) ++bits;
        for (int i = 0; i < N; ++i)
        {
            int r = 0;
            for (int b = 0; b < bits; ++b) if (i & (1 << b)) r |= 1 << (bits - 1 - b);
            rev[(size_t) i] = r;
        }
    }

    // in place; inverse = true gives the unscaled inverse transform
    void transform (C* x, bool inverse) const
    {
        for (int i = 0; i < N; ++i)
            if (i < rev[(size_t) i]) std::swap (x[i], x[rev[(size_t) i]]);
        for (int len = 2; len <= N; len <<= 1)
        {
            const int step = N / len;
            for (int i = 0; i < N; i += len)
                for (int j = 0; j < len / 2; ++j)
                {
                    C w = twiddle[(size_t) (j * step)];
                    if (inverse) w = std::conj (w);
                    const C u = x[i + j], v = x[i + j + len / 2] * w;
                    x[i + j] = u + v;
                    x[i + j + len / 2] = u - v;
                }
        }
    }

    int size() const { return N; }

private:
    int N;
    std::vector<C> twiddle;
    std::vector<int> rev;
};

// The 48 Flip chords: intervals in semitones above (or below) the note you play.
// Noon is a single 4th; further round adds notes, up on the right, down on the left.
struct FlipChords
{
    struct Chord { int n = 0; int semis[4] {}; };

    static const Chord& at (int i)
    {
        static const std::array<Chord, 48> table = []
        {
            static const int up[24][5] = {
                { 1, 5 },            { 1, 7 },            { 1, 12 },           { 2, 5, 12 },
                { 2, 7, 12 },        { 2, 12, 19 },       { 2, 12, 17 },       { 2, 5, 7 },
                { 2, 7, 14 },        { 2, 12, 24 },       { 3, 5, 12, 17 },    { 3, 7, 12, 19 },
                { 3, 5, 7, 12 },     { 3, 7, 14, 19 },    { 3, 12, 19, 24 },   { 3, 12, 17, 24 },
                { 3, 5, 12, 19 },    { 3, 7, 12, 24 },    { 4, 5, 7, 12, 19 }, { 4, 7, 12, 19, 24 },
                { 4, 5, 12, 17, 24 },{ 4, 12, 17, 19, 24 },{ 4, 5, 7, 12, 17 }, { 4, 7, 12, 19, 26 } };
            std::array<Chord, 48> t {};
            for (int k = 0; k < 24; ++k)
            {
                Chord u; u.n = up[k][0];
                for (int v = 0; v < u.n; ++v) u.semis[v] = up[k][v + 1];
                Chord d = u;
                for (int v = 0; v < d.n; ++v) d.semis[v] = -u.semis[v];
                t[(size_t) (24 + k)] = u;       // right of noon: up, denser as you turn
                t[(size_t) (23 - k)] = d;       // left of noon: the mirror image, down
            }
            return t;
        }();
        return table[(size_t) std::max (0, std::min (47, i))];
    }

    static int indexFor (float modify) { return std::min (47, (int) (clampf (modify, 0, 1) * 48.0f)); }
};

class MurkWet
{
public:
    enum Mode { Soup = 0, Relay = 1, Flip = 2 };

    struct Params
    {
        int mode = Soup;
        float time = 0.5f, modify = 0.5f;
        bool freeze = false, spread = false;
        float width = 1.0f;
        float coreRate = 64000.0f;
        float fixedLen = 0.0f;   // > 0: tempo sync sets Relay time and Flip lag (core samples)
    };

    static constexpr int kN = 512, kHop = kN / 4;

    MurkWet() : fft (kN) {}

    void prepare()
    {
        for (int c = 0; c < 2; ++c)
        {
            relayLine[c].init (1 << 21);
            flipLine[c].init (1 << 18);
            hist[c].init (1 << 18);
            freezeBuf[c].assign ((size_t) kFreezeMax + kFreezeX + 4, 0.0f);
        }
        inRing.assign ((size_t) kN, {});
        olaL.assign ((size_t) kN * 2, 0.0f);
        olaR.assign ((size_t) kN * 2, 0.0f);
        work.assign ((size_t) kN, {});
        accL.assign ((size_t) kN / 2 + 1, 0.0f);
        accR.assign ((size_t) kN / 2 + 1, 0.0f);
        phL.assign ((size_t) kN / 2 + 1, 0.0f);
        phR.assign ((size_t) kN / 2 + 1, 0.0f);
        window.resize ((size_t) kN);
        for (int i = 0; i < kN; ++i) window[(size_t) i] = 0.5f - 0.5f * std::cos (kTwoPi * (float) i / (float) kN);
        reset();
    }

    void reset()
    {
        for (int c = 0; c < 2; ++c)
        {
            relayLine[c].clear(); flipLine[c].clear(); hist[c].clear();
            std::fill (freezeBuf[c].begin(), freezeBuf[c].end(), 0.0f);
        }
        std::fill (inRing.begin(), inRing.end(), SmallFFT::C {});
        std::fill (olaL.begin(), olaL.end(), 0.0f);
        std::fill (olaR.begin(), olaR.end(), 0.0f);
        std::fill (accL.begin(), accL.end(), 0.0f);
        std::fill (accR.begin(), accR.end(), 0.0f);
        ringPos = 0; hopCount = 0; olaPos = 0;
        rA = rB = 8192.0f; rX = 0;
        for (auto& v : voicePh) v = 0.0f;
        flipPan = 0.0f;
        frozen = false; freezeGain = 0.0f; freezePos = 0.0f; freezeLen = 1024;
    }

    // ---- time mappings, core samples ----
    static float soupT60 (float t) { return 0.4f * std::pow (50.0f, clampf (t, 0, 1)); }   // 0.4 s to 20 s
    static float relayTime (float t) { return 256.0f * std::pow (2.0f, clampf (t, 0, 1) * 8.8f); }
    static int relayRepeats (float m) { return 1 + std::min (15, (int) (clampf (m, 0, 1) * 16.0f)); }
    static bool relayEndless (float m) { return m >= 0.97f; }
    static float flipLag (float t) { return 64.0f * std::pow (2.0f, clampf (t, 0, 1) * 10.0f); }      // 1 ms to 1 s at 64 kHz

    Stereo process (Stereo in, const Params& p)
    {
        Stereo y;
        switch (p.mode)
        {
            case Soup:  y = soup (in, p); break;
            case Relay: y = relay (in, p); break;
            default:    y = flip (in, p); break;
        }

        // Flip freezes into a repeating chord: the last stretch of its output, looped
        if (p.mode == Flip)
        {
            hist[0].push (y.l);
            hist[1].push (y.r);
            if (p.freeze && ! frozen) captureFreeze (p);
            frozen = p.freeze;
            freezeGain += clampf ((frozen ? 1.0f : 0.0f) - freezeGain, -1.0f / 256.0f, 1.0f / 256.0f);
            if (freezeGain > 0.0f) y = y * (1.0f - freezeGain) + readFreeze() * freezeGain;
        }
        else
        {
            frozen = p.freeze;
        }
        return y;
    }

private:
    static constexpr int kFreezeMax = 1 << 17, kFreezeX = 512;

    // ---- Soup ---------------------------------------------------------------------------
    Stereo soup (Stereo in, const Params& p)
    {
        // Frozen: the bands stop listening and stop decaying, an ambient pad
        inRing[(size_t) ringPos] = p.freeze ? SmallFFT::C {} : SmallFFT::C (in.l, in.r);
        ringPos = (ringPos + 1) & (kN - 1);

        if (++hopCount >= kHop)
        {
            hopCount = 0;
            soupFrame (p);
        }
        const Stereo out { olaL[(size_t) olaPos], olaR[(size_t) olaPos] };
        olaL[(size_t) olaPos] = olaR[(size_t) olaPos] = 0.0f;
        olaPos = (olaPos + 1) % (kN * 2);

        if (p.spread) // Spread widens the wash further
        {
            const float mid = 0.5f * (out.l + out.r), side = 0.5f * (out.l - out.r) * (1.0f + clampf (p.width, 0, 2));
            return { mid + side, mid - side };
        }
        return out;
    }

    void soupFrame (const Params& p)
    {
        const int half = kN / 2;
        // analysis: left and right packed into one complex FFT
        for (int i = 0; i < kN; ++i)
            work[(size_t) i] = inRing[(size_t) ((ringPos + i) & (kN - 1))] * window[(size_t) i];
        fft.transform (work.data(), false);

        const float framesPerSec = p.coreRate / (float) kHop;
        const float dec = p.freeze ? 1.0f : std::pow (10.0f, -3.0f / (soupT60 (p.time) * framesPerSec));
        const float feed = (1.0f - dec) * 2.6f;
        const float m = clampf (p.modify, 0, 1);

        for (int k = 0; k <= half; ++k)
        {
            const auto z = work[(size_t) k], zc = std::conj (work[(size_t) ((kN - k) & (kN - 1))]);
            const float magL = std::abs ((z + zc) * 0.5f);
            const float magR = std::abs ((z - zc) * 0.5f);
            if (! p.freeze)
            {
                accL[(size_t) k] = accL[(size_t) k] * dec + magL * feed;
                accR[(size_t) k] = accR[(size_t) k] * dec + magR * feed;
            }
        }

        // Synthetic end: the bands are pooled into coarse groups whose energy sits on one partial
        const int group = 1 + (int) (m * m * 14.0f);
        const float quant = clampf ((m - 0.35f) / 0.65f, 0, 1);
        const float jitter = (0.12f + 0.88f * m) * kPi;

        work[0] = {};
        for (int k = 1; k <= half; ++k)
        {
            float aL = accL[(size_t) k], aR = accR[(size_t) k];
            if (quant > 0.0f && group > 1)
            {
                const int g0 = (k / group) * group, centre = g0 + group / 2;
                float sL = 0, sR = 0;
                for (int j = g0; j < g0 + group && j <= half; ++j) { sL += accL[(size_t) j]; sR += accR[(size_t) j]; }
                const float qL = k == centre ? sL * 0.8f : 0.0f, qR = k == centre ? sR * 0.8f : 0.0f;
                aL = lerpf (aL, qL, quant);
                aR = lerpf (aR, qR, quant);
            }
            // natural: phases advance as the band's own frequency would, plus a little drift
            const float adv = kTwoPi * (float) k * (float) kHop / (float) kN;
            phL[(size_t) k] = std::fmod (phL[(size_t) k] + adv + rng.next() * jitter, kTwoPi);
            phR[(size_t) k] = std::fmod (phR[(size_t) k] + adv + rng.next() * jitter, kTwoPi);
            const auto A = std::polar (aL, phL[(size_t) k]);
            const auto B = std::polar (aR, phR[(size_t) k]);
            if (k < half)
            {
                // pack two real spectra so one inverse FFT returns left + i right
                work[(size_t) k] = A + SmallFFT::C (0, 1) * B;
                work[(size_t) (kN - k)] = std::conj (A) + SmallFFT::C (0, 1) * std::conj (B);
            }
            else
            {
                work[(size_t) k] = SmallFFT::C (A.real(), B.real());
            }
        }
        fft.transform (work.data(), true);

        // overlap-add: Hann analysis and synthesis at 4x overlap sums to 1.5
        const float scale = 1.0f / ((float) kN * 1.5f);
        for (int i = 0; i < kN; ++i)
        {
            const int k = (olaPos + i) % (kN * 2);
            olaL[(size_t) k] += work[(size_t) i].real() * window[(size_t) i] * scale;
            olaR[(size_t) k] += work[(size_t) i].imag() * window[(size_t) i] * scale;
        }
    }

    // ---- Relay --------------------------------------------------------------------------
    Stereo relay (Stereo in, const Params& p)
    {
        const float maxLine = (float) (relayLine[0].size() - 4);
        const float target = clampf (p.fixedLen > 0.0f ? p.fixedLen : relayTime (p.time), 32.0f, maxLine);
        const bool endless = relayEndless (p.modify);
        int n = endless ? 1 : relayRepeats (p.modify);
        while (n > 1 && target * (float) n > maxLine) --n;

        // time changes jump with a short crossfade, no pitch bend
        constexpr int XF = 1024;
        if (rX == 0 && std::fabs (target - rA) > 0.5f) { rB = target; rX = 1; }
        const float k = rX > 0 ? (float) rX / (float) XF : 0.0f;
        auto tapAt = [&] (int c, float mult)
        {
            const float a = relayLine[c].tap (rA * mult);
            return rX > 0 ? a * (1.0f - k) + relayLine[c].tap (rB * mult) * k : a;
        };

        Stereo o;
        if (endless)
        {
            o = { tapAt (0, 1.0f), tapAt (1, 1.0f) };
        }
        else
        {
            constexpr float g = 0.72f; // every repeat at the same level
            for (int r = 1; r <= n; ++r)
            {
                const Stereo t { tapAt (0, (float) r), tapAt (1, (float) r) };
                if (p.spread) // repeats alternate sides
                {
                    const float side = (r % 2 == 1 ? -1.0f : 1.0f) * clampf (p.width, 0, 1);
                    o = o + panMono (0.5f * (t.l + t.r), side) * g;
                }
                else o = o + t * g;
            }
        }
        if (rX > 0 && ++rX > XF) { rA = rB; rX = 0; }

        // Freeze recirculates the line: the last stretch keeps looping, a looping echo.
        // Endless mode feeds the repeats back at full level so they stack like a looper.
        Stereo push = in;
        if (p.freeze) push = { relayLine[0].tap (rA), relayLine[1].tap (rA) };
        else if (endless) push = { in.l + o.l, in.r + o.r };
        relayLine[0].push (softClip (push.l));
        relayLine[1].push (softClip (push.r));
        return o;
    }

    // ---- Flip ---------------------------------------------------------------------------
    Stereo flip (Stereo in, const Params& p)
    {
        flipLine[0].push (in.l);
        flipLine[1].push (in.r);

        const auto& chord = FlipChords::at (FlipChords::indexFor (p.modify));
        const float lag = clampf (p.fixedLen > 0.0f ? p.fixedLen : flipLag (p.time), 1.0f, 60000.0f);
        constexpr float W = 4096.0f; // the pitch shifter's window

        // Two read heads half a window apart. Each carries the sound on its own for most of
        // its sweep and they crossfade only briefly, so a held note keeps its true pitch
        // instead of the heads drifting in and out of phase with each other.
        auto win = [] (float ph)
        {
            constexpr float F = 0.12f;
            auto s = [] (float x) { return 0.5f - 0.5f * std::cos (kPi * x); };
            if (ph < F) return s (ph / F);
            if (ph < 0.5f) return 1.0f;
            if (ph < 0.5f + F) return 1.0f - s ((ph - 0.5f) / F);
            return 0.0f;
        };

        Stereo o;
        for (int v = 0; v < chord.n; ++v)
        {
            const float ratio = std::pow (2.0f, (float) chord.semis[v] / 12.0f);
            float& ph = voicePh[v];
            ph += (1.0f - ratio) / W;
            ph -= std::floor (ph);
            const float ph2 = ph + 0.5f - std::floor (ph + 0.5f);
            const float base = lag * (float) (v + 1) + 2.0f;
            const float w1 = win (ph), w2 = win (ph2);
            Stereo s { flipLine[0].tap (base + ph * W) * w1 + flipLine[0].tap (base + ph2 * W) * w2,
                       flipLine[1].tap (base + ph * W) * w1 + flipLine[1].tap (base + ph2 * W) * w2 };
            const float g = 0.8f / std::sqrt ((float) chord.n);
            if (p.spread)
            {
                const float side = (v % 2 == 0 ? -1.0f : 1.0f) * clampf (p.width, 0, 1) * (0.5f + 0.5f * (float) (v / 2 + 1) / 2.0f);
                o = o + panMono (0.5f * (s.l + s.r), side) * g;
            }
            else o = o + s * g;
        }
        return o;
    }

    void captureFreeze (const Params& p)
    {
        const auto& chord = FlipChords::at (FlipChords::indexFor (p.modify));
        const float lag = clampf (p.fixedLen > 0.0f ? p.fixedLen : flipLag (p.time), 1.0f, 60000.0f);
        freezeLen = (int) clampf (lag * (float) (chord.n + 1) + 0.25f * p.coreRate, 4096.0f, (float) kFreezeMax);
        const int total = freezeLen + kFreezeX;
        for (int c = 0; c < 2; ++c)
        {
            auto& b = freezeBuf[c];
            for (int i = 0; i < total; ++i) b[(size_t) i] = hist[c].tapInt (total - i);
            for (int i = 0; i < kFreezeX; ++i)
            {
                const float k = (float) i / (float) kFreezeX;
                b[(size_t) i] = b[(size_t) i] * k + b[(size_t) (freezeLen + i)] * (1.0f - k);
            }
        }
        freezePos = 0.0f;
    }

    Stereo readFreeze()
    {
        freezePos += 1.0f;
        if (freezePos >= (float) freezeLen) freezePos -= (float) freezeLen;
        const int i = (int) freezePos;
        return { freezeBuf[0][(size_t) i], freezeBuf[1][(size_t) i] };
    }

    // ---- state ---------------------------------------------------------------------------
    SmallFFT fft;
    std::vector<SmallFFT::C> inRing, work;
    std::vector<float> olaL, olaR, accL, accR, phL, phR, window;
    int ringPos = 0, hopCount = 0, olaPos = 0;

    DelayLine relayLine[2];
    float rA = 8192, rB = 8192;
    int rX = 0;

    DelayLine flipLine[2];
    float voicePh[4] {};
    float flipPan = 0;

    DelayLine hist[2];
    std::vector<float> freezeBuf[2];
    bool frozen = false;
    float freezeGain = 0, freezePos = 0;
    int freezeLen = 1024;
    Rng rng;
};
} // namespace murmur
