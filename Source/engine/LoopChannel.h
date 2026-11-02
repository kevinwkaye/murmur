#pragma once

// The Micro-Looper: an always-listening circular buffer. While bypassed it keeps
// recording; switching it on loops whatever it heard most recently. The buffer is
// measured in CORE samples, so the Clock sets the loop length and resolution.

#include "DspUtil.h"

namespace murmur
{
class LoopChannel
{
public:
    enum Mode { Env = 0, Tape = 1, Stretch = 2 };

    struct Params
    {
        int mode = Tape;
        float length = 1.0f;
        float modify = 0.7f;
        bool playing = false;
        bool overdub = false;
        bool noDub = false;     // refresh mode
        bool classic = false;
        bool spread = false;
        float width = 1.0f;
        float fade = 1.0f;      // 1 = loops keep forever while dubbing
        int capacity = 128000;  // circle size in core samples
        float lengthFraction = 0.0f; // > 0: tempo-synced Tape length as a fraction of the loop
        float coreRate = 64000.0f;
    };

    void prepare (int maxCapacity)
    {
        maxCap = std::max (maxCapacity, 1024);
        for (auto& b : buf) b.assign ((size_t) maxCap + 4, 0.0f);
        reset();
    }

    void reset()
    {
        for (auto& b : buf) std::fill (b.begin(), b.end(), 0.0f);
        w = 0; filled = 0; C = std::min (128000, maxCap); start = 0; L = C;
        playing = false; playGain = 0.0f; env = 0.0f;
        tapePos = 0; envPos = 0; intra = 0; sliceStart = 0; held = false;
        stretchC = 0; grainTimer = 0; nextGrain = 0;
        for (auto& g : grains) g = {};
        panPh = 0; gate = 1; lastReadIdx = -1; uiPos = 0;
        dubbing = false; recPos = 0; refreshOff = 0;
    }

    // envIn: live input for Env detection. rec: what the listening buffer records
    // (input plus Wet output). dub: what overdubbing records (clean input only).
    Stereo process (Stereo envIn, Stereo rec, Stereo dub, const Params& p)
    {
        // envelope follower in real time
        const float lvl = std::max (std::fabs (envIn.l), std::fabs (envIn.r));
        const float atk = std::exp (-1.0f / (0.001f * p.coreRate));
        const float rel = std::exp (-1.0f / (0.060f * p.coreRate));
        env = lvl > env ? lvl + atk * (env - lvl) : lvl + rel * (env - lvl);

        if (p.playing && ! playing) capture();
        if (! p.playing && playing) release();
        playing = p.playing;

        playGain += clampf ((playing ? 1.0f : 0.0f) - playGain, -1.0f / 256.0f, 1.0f / 256.0f);

        if (! playing)
        {
            const int cap = std::min (std::max (p.capacity, 256), maxCap);
            if (cap != C)
            {
                C = cap;
                if (w >= C) w = 0;
                filled = std::min (filled, C);
            }
            buf[0][(size_t) w] = rec.l;
            buf[1][(size_t) w] = rec.r;
            ++w;
            filled = std::min (C, std::max (filled, w));
            if (w >= C) w = 0;
            if (playGain <= 0.0f)
                return {};
        }

        Stereo out = readPlayer (p, dub);
        out = out * playGain;
        if (p.classic && p.noDub && p.overdub && playing)
            out = {}; // MKI behaviour: loop is muted while you record
        return out;
    }

    // ---- UI helpers (read from the message thread, benign races) ----
    bool isReady() const { return ! buf[0].empty(); }
    bool isPlaying() const { return playing; }
    int length() const { return playing ? L : C; }
    int origin() const { return playing ? start : w; }
    int filledSamples() const { return playing ? L : filled; }
    float positionFraction() const { return L > 0 ? uiPos / (float) L : 0.0f; }
    const float* data (int ch) const { return buf[ch].data(); }

private:
    struct Grain { float startPos = 0, age = 0, len = 0; float dir = 1; bool active = false; };

    void capture()
    {
        if (filled >= C)
        {
            L = C;           // the whole circle, oldest sample first
            start = w;
        }
        else
        {
            // Not full yet (e.g. just after loading): loop only what was heard
            L = std::max (filled, 512);
            start = (w < L) ? w : 0;
        }
        if (start >= L) start = 0;

        // Bake a short fade at the loop seam, where the newest and oldest audio meet
        const int X = std::min (256, L / 8);
        for (int i = 0; i < X; ++i)
        {
            const float g = (float) i / (float) X;
            for (int c = 0; c < 2; ++c)
            {
                buf[c][(size_t) phys (i)] *= g;
                buf[c][(size_t) phys (L - 1 - i)] *= g;
            }
        }

        tapePos = envPos = stretchC = 0.0f;
        intra = 0; sliceStart = 0; held = false;
        grainTimer = 0; nextGrain = 0;
        for (auto& g : grains) g = {};
        lastReadIdx = -1;
        dubbing = false;
    }

    void release()
    {
        // Back to listening: overwrite the old loop starting from its oldest sample,
        // which gives the pedal's "replace" behaviour.
        w = start;
        filled = L;
        C = std::max (C, L);
    }

    int phys (int i) const
    {
        i %= L;
        if (i < 0) i += L;
        int k = start + i;
        if (k >= L) k -= L;
        return k;
    }

    static float wrapPos (float x, float len)
    {
        if (x >= len || x < 0.0f) x -= std::floor (x / len) * len;
        if (x >= len) x = 0.0f;
        return x;
    }

    Stereo readAt (float pos) const
    {
        pos = wrapPos (pos, (float) L);
        const int i0 = (int) pos;
        const float f = pos - (float) i0;
        const int a = phys (i0), b = phys (i0 + 1);
        return { buf[0][(size_t) a] + (buf[0][(size_t) b] - buf[0][(size_t) a]) * f,
                 buf[1][(size_t) a] + (buf[1][(size_t) b] - buf[1][(size_t) a]) * f };
    }

    // MKII Stretch steps: no stretch, 1.5x, 2x, 4x stretch each way, stalled at noon
    static float stretchSpeed (float cc)
    {
        if (cc < 15.5f)  return -1.0f;
        if (cc < 30.5f)  return -1.0f / 1.5f;
        if (cc < 46.5f)  return -0.5f;
        if (cc < 60.5f)  return -0.25f;
        if (cc < 80.5f)  return 0.0f;
        if (cc < 96.5f)  return 0.25f;
        if (cc < 111.5f) return 0.5f;
        return 1.0f / 1.5f;
    }

    static float tapeRate (float m)
    {
        static constexpr float rates[8] = { -4.0f, -2.0f, -1.0f, -0.5f, 0.5f, 1.0f, 2.0f, 4.0f };
        return rates[(int) (clampf (m, 0, 1) * 7.999f)];
    }

    // Where the player is right now, in loop samples (used to place the record head)
    float currentPos (int mode) const
    {
        if (mode == Tape) return tapePos;
        if (mode == Env) return held ? sliceStart + intra : envPos;
        return stretchC;
    }

    Stereo readPlayer (const Params& p, Stereo dub)
    {
        const float Lf = (float) L;
        const bool dub_ = p.overdub && playing;
        if (dub_ && ! dubbing)
        {
            recPos = wrapPos (std::floor (currentPos (p.mode)), Lf);
            refreshOff = 0.0f;
        }
        dubbing = dub_;
        const bool refresh = dubbing && p.noDub;

        Stereo v;
        int idx = 0;

        switch (p.mode)
        {
            case Tape:
            {
                const float rate = tapeRate (p.modify);
                const float win = std::max (64.0f, p.lengthFraction > 0.0f ? Lf * p.lengthFraction
                                                                          : Lf * lerpf (0.03f, 1.0f, std::pow (p.length, 1.3f)));

                if (refresh)
                {
                    // Live, slightly delayed: read just behind the record head at the tape speed
                    const float lag = std::max (256.0f, std::min (win, Lf * 0.5f));
                    refreshOff += rate - 1.0f;
                    while (refreshOff >= 0.0f) refreshOff -= lag;
                    while (refreshOff < -lag) refreshOff += lag;
                    const float g = std::min (1.0f, std::min (-refreshOff, lag + refreshOff) / 64.0f);
                    tapePos = wrapPos (recPos - 1.0f + refreshOff, Lf);
                    v = readAt (tapePos) * g;
                    if (p.spread)
                        v.l = readAt (recPos - 1.0f - lag - refreshOff).l * g;
                    idx = (int) tapePos;
                    uiPos = tapePos;
                    break;
                }

                const float Lw = p.classic ? Lf : win;
                tapePos = wrapPos (tapePos + rate, Lw);

                const float fadeLen = std::min (128.0f, Lw * 0.25f);
                const float edge = std::min (tapePos, Lw - tapePos);
                const float g = std::min (1.0f, edge / fadeLen);

                v = readAt (tapePos) * g;
                if (p.spread) // right plays forward, left plays the same window reversed
                    v.l = readAt (Lw - tapePos).l * g;

                if (p.classic) // MKI: Length chops the loop instead of shortening it
                {
                    const float target = (std::fmod (tapePos, win) < win * 0.55f || p.length > 0.98f) ? 1.0f : 0.0f;
                    gate += clampf (target - gate, -1.0f / 48.0f, 1.0f / 48.0f);
                    v = v * gate;
                }
                idx = (int) tapePos;
                uiPos = tapePos;
                break;
            }

            case Env:
            {
                const float SS = std::max (32.0f, Lf * lerpf (0.002f, 0.25f, p.length * p.length));
                const float thr = dbToGain (lerpf (-12.0f, -66.0f, clampf (p.modify, 0, 1)));
                const bool hold = env > thr;
                float readPos;

                if (refresh && ! hold)
                    envPos = wrapPos (recPos - SS, Lf);

                if (! hold)
                {
                    if (held) envPos = wrapPos (sliceStart + intra, Lf); // carry on from where the hold left off
                    envPos = wrapPos (envPos + 1.0f, Lf);
                    sliceStart = std::floor (envPos / SS) * SS;
                    readPos = envPos;
                }
                else
                {
                    if (! held) { intra = envPos - sliceStart; holdAge = 0.0f; }
                    holdAge += 1.0f;
                    intra += 1.0f;
                    if (intra >= SS) intra -= SS;
                    readPos = sliceStart + intra;
                }
                held = hold;
                v = readAt (readPos);
                if (hold)
                {
                    // crossfade into the slice start so the repeat is seamless
                    const float fl = std::min (64.0f, SS * 0.25f);
                    if (intra > SS - fl)
                    {
                        const float k = (intra - (SS - fl)) / fl * std::min (1.0f, holdAge / fl);
                        v = v * (1.0f - k) + readAt (readPos - SS) * k;
                    }
                }

                if (p.spread && hold) // pans left and right while you play over the threshold
                {
                    panPh += 1.0f / (2.0f * SS);
                    if (panPh > 1.0f) panPh -= 1.0f;
                    const float pan = std::sin (kTwoPi * panPh) * clampf (p.width, 0, 1);
                    v = { v.l * std::min (1.0f, 1.0f - pan), v.r * std::min (1.0f, 1.0f + pan) };
                }
                idx = (int) wrapPos (readPos, Lf);
                uiPos = (float) idx;
                break;
            }

            default: // Stretch
            {
                float G = std::max (64.0f, Lf * lerpf (0.004f, 0.35f, std::pow (p.length, 1.5f)));
                G = std::min (G, Lf * 0.5f);
                const float m = clampf (p.modify, 0, 1);

                const float cc = m * 127.0f; // the pedal's MIDI table for Stretch Modify
                if (cc >= 126.5f && ! p.classic && ! refresh)
                {
                    stretchC = wrapPos (stretchC + 1.0f, Lf);
                    v = readAt (stretchC);
                    for (auto& gr : grains) gr.active = false;
                }
                else
                {
                    float sp;
                    if (p.classic) // original MOOD: a continuous sweep, slower towards noon
                    {
                        sp = clampf ((m - 0.5f) * 2.0f / 0.94f, -1.0f, 1.0f);
                        sp = (sp < 0 ? -1.0f : 1.0f) * std::pow (std::fabs (sp), 1.5f);
                    }
                    else
                    {
                        sp = stretchSpeed (cc);
                    }
                    const float dir = (p.classic || sp >= 0.0f) ? 1.0f : -1.0f;

                    if (refresh) // grains come from the audio you just played
                        stretchC = wrapPos (recPos - (dir > 0 ? G + 1.0f : 1.0f), Lf);
                    else
                        stretchC = wrapPos (stretchC + sp, Lf);

                    const int nGr = p.classic ? 4 : 2;
                    const float interval = G / (float) nGr;
                    grainTimer += 1.0f;
                    if (grainTimer >= interval)
                    {
                        grainTimer -= interval;
                        auto& gr = grains[nextGrain];
                        gr.startPos = stretchC + (p.classic ? rng.next() * G * 0.5f : 0.0f);
                        gr.age = 0; gr.len = G; gr.dir = dir; gr.active = true;
                        nextGrain = (nextGrain + 1) % nGr;
                    }

                    for (auto& gr : grains)
                    {
                        if (! gr.active) continue;
                        const float s = std::sin (kPi * gr.age / gr.len);
                        v = v + readAt (gr.startPos + gr.dir * gr.age) * (s * s);
                        gr.age += 1.0f;
                        if (gr.age >= gr.len) gr.active = false;
                    }
                    if (p.classic) v = v * 0.5f;

                    if (p.spread) // slow, smooth side-to-side motion
                    {
                        panPh += (0.05f + 0.4f * std::fabs (sp)) / p.coreRate;
                        if (panPh > 1.0f) panPh -= 1.0f;
                        v = panMono (0.5f * (v.l + v.r), std::sin (kTwoPi * panPh) * clampf (p.width, 0, 1));
                    }
                }
                idx = (int) stretchC;
                uiPos = stretchC;
                break;
            }
        }

        // ---- overdub: clean input only, written by a record head moving at normal speed.
        // Playback effects mean overdubs can land somewhere unexpected, like the pedal.
        if (dubbing)
        {
            const float keep = p.noDub ? 0.0f : lerpf (0.5f, 1.0f, clampf (p.fade, 0, 1));
            const int k = phys ((int) recPos);
            buf[0][(size_t) k] = softClip (buf[0][(size_t) k] * keep + dub.l);
            buf[1][(size_t) k] = softClip (buf[1][(size_t) k] * keep + dub.r);
            recPos += 1.0f;
            if (recPos >= Lf) recPos -= Lf;
        }

        // ---- classic: loops slowly deteriorate each pass ----
        if (p.classic && playing && idx != lastReadIdx)
        {
            const int k = phys (idx), kp = phys (idx - 1);
            for (int c = 0; c < 2; ++c)
            {
                float x = clampf (buf[c][(size_t) k], -1.0f, 1.0f);
                x = 0.998f * (x - 0.06f * x * x * x);
                buf[c][(size_t) k] = 0.92f * x + 0.08f * buf[c][(size_t) kp];
            }
            lastReadIdx = idx;
        }
        return v;
    }

    std::vector<float> buf[2];
    int maxCap = 1024;
    int w = 0, filled = 0, C = 1024, start = 0, L = 1024;
    bool playing = false;
    float playGain = 0, env = 0;

    float tapePos = 0, envPos = 0, intra = 0, sliceStart = 0, holdAge = 0;
    bool held = false;

    float stretchC = 0, grainTimer = 0;
    int nextGrain = 0;
    Grain grains[4];

    bool dubbing = false;
    float recPos = 0, refreshOff = 0;

    float panPh = 0, gate = 1, uiPos = 0;
    int lastReadIdx = -1;
    Rng rng;
};
} // namespace murmur
