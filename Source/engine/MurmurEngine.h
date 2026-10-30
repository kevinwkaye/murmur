#pragma once

// Top-level engine: input stage, the variable-rate Clock, both channels, routing
// and the mixer. The host calls process() at its own sample rate. Internally the
// channels run at the Clock rate (2 kHz to 64 kHz), so lowering the Clock slows,
// lengthens and degrades everything together.

#include "LoopChannel.h"
#include "WetChannel.h"
#include <atomic>

namespace murmur
{
struct EngineParams
{
    // front panel
    float mix = 0.5f;
    float clock = 1.0f;          // knob 0..1
    int routing = 1;             // 0 In, 1 Both, 2 Loop
    bool wetOn = false, freeze = false;
    int wetMode = 0;
    float time = 0.5f, wetModify = 0.5f;
    bool loopOn = false, overdub = false;
    int loopMode = 1;
    float length = 1.0f, loopModify = 0.7f;

    // hidden options
    float width = 1.0f;          // 0..2
    float toneHz = 20000.0f;
    float fade = 1.0f;
    float direct = 0.0f;
    float balance = 0.0f;        // -1 wet quieter .. +1 loop quieter
    int sync = 0;                // 0 unsynced, 1 loop follows wet, 2 wet follows loop
    bool halfLength = false;
    int spreadSolo = 0;          // 0 both, 1 wet only, 2 loop only

    // switches
    bool miso = false, spread = false, dryKill = false, trails = true;
    bool noDub = false, smoothClock = false, classic = false;

    // plugin extras
    float inGainDb = 0.0f, outGainDb = 0.0f;
    float bufferSeconds = 2.0f;  // loop length at a 64 kHz clock (full length)
    int stepSet = 1;             // 0 octaves, 1 fourths and fifths, 2 semitones
};

class MurmurEngine
{
public:
    static constexpr float kTopRate = 64000.0f;
    static constexpr int kMaxSeconds = 32;

    void prepare (double sampleRate)
    {
        fs = (float) sampleRate;
        wet.prepare();
        loop.prepare ((int) (kMaxSeconds * kTopRate));
        reset();
    }

    void reset()
    {
        wet.reset();
        loop.reset();
        phase = 0.0f;
        prevOut = curOut = lastWet = {};
        for (auto& f : inLp) f.reset();
        for (auto& f : outLp) f.reset();
        logRatio = -1000.0f; // forces a snap on the first block
        wetGain = 0.0f;
        filterCounter = 0;
    }

    // Musical clock: knob position -> log2 ratio below 64 kHz, snapped to the chosen step set
    static float clockLog2 (float knob, int stepSet, bool smooth)
    {
        const float semis = -60.0f * (1.0f - clampf (knob, 0, 1)); // five octaves
        if (smooth)
            return semis / 12.0f;
        if (stepSet == 0) // octaves
            return std::round (semis / 12.0f);
        if (stepSet == 2) // semitones
            return std::round (semis) / 12.0f;

        // fourths and fifths: ratios 1, 4/3, 3/2 within each octave
        const float target = semis / 12.0f;
        float best = 0.0f, bestDist = 1e9f;
        const float steps[3] = { 0.0f, std::log2 (4.0f / 3.0f), std::log2 (3.0f / 2.0f) };
        for (int o = -5; o <= 0; ++o)
            for (float s : steps)
            {
                const float c = (float) o + s;
                if (c > 0.0f || c < -5.0f) continue;
                const float d = std::fabs (c - target);
                if (d < bestDist) { bestDist = d; best = c; }
            }
        return best;
    }

    static float clockHzFor (float knob, int stepSet, bool smooth)
    {
        return kTopRate * std::pow (2.0f, clockLog2 (knob, stepSet, smooth));
    }

    void process (float* left, float* right, int numSamples, const EngineParams& p)
    {
        const float target = clockLog2 (p.clock, p.stepSet, p.smoothClock);
        if (logRatio < -100.0f) logRatio = target;
        const float slewSec = p.smoothClock ? 0.04f : 0.003f;
        const float slew = 1.0f - std::exp (-1.0f / (slewSec * fs));

        const float inGain = dbToGain (p.inGainDb);
        const float outGain = dbToGain (p.outGainDb);
        const float mix = clampf (p.mix, 0, 1);
        const float dryG = p.dryKill ? 0.0f : std::min (1.0f, 2.0f * (1.0f - mix));
        const float wetG = std::min (1.0f, 2.0f * mix);

        params = &p;

        for (int i = 0; i < numSamples; ++i)
        {
            logRatio += (target - logRatio) * slew;
            const float rate = kTopRate * std::exp2 (logRatio);
            coreRate = rate;

            if (filterCounter-- <= 0)
            {
                filterCounter = 16;
                const float fc = std::min (0.45f * rate, 0.45f * fs);
                for (auto& f : inLp) f.setCutoff (fc, fs);
                for (auto& f : outLp) f.setCutoff (fc, fs);
            }

            float inL = left[i] * inGain;
            float inR = right[i] * inGain;
            if (p.miso) inR = inL;

            const float aL = inLp[0].process (inL);
            const float aR = inLp[1].process (inR);

            phase += rate / fs;
            while (phase >= 1.0f)
            {
                phase -= 1.0f;
                prevOut = curOut;
                curOut = coreTick ({ aL, aR });
            }

            const float oL = outLp[0].process (prevOut.l + (curOut.l - prevOut.l) * phase);
            const float oR = outLp[1].process (prevOut.r + (curOut.r - prevOut.r) * phase);

            left[i] = (inL * dryG + oL * wetG) * outGain;
            right[i] = (inR * dryG + oR * wetG) * outGain;
        }

        uiClockHz.store (kTopRate * std::exp2 (logRatio), std::memory_order_relaxed);
    }

    const LoopChannel& getLoop() const { return loop; }
    std::atomic<float> uiClockHz { kTopRate };

private:
    Stereo coreTick (Stereo in)
    {
        const EngineParams& p = *params;
        const bool wetSpread = p.spread && p.spreadSolo != 2;
        const bool loopSpread = p.spread && p.spreadSolo != 1;

        // ---- Wet Channel params ----
        WetChannel::Params wp;
        wp.mode = p.wetMode; wp.time = p.time; wp.modify = p.wetModify;
        wp.freeze = p.freeze && p.wetOn; wp.spread = wetSpread; wp.classic = p.classic;
        wp.width = p.width; wp.toneHz = p.toneHz; wp.coreRate = coreRate;

        // ---- Loop params ----
        const int full = (int) (clampf (p.bufferSeconds, 1.0f, (float) kMaxSeconds) * kTopRate);
        int capacity = p.halfLength ? full / 2 : full;
        if (p.sync == 1) // loop follows wet: Time sets the loop length
            capacity = std::max (256, wet.timeSamples (wp));
        if (p.sync == 2 && loop.isPlaying()) // wet follows loop: Time steps in divisions of the loop
            wp.syncLen = loop.length();

        LoopChannel::Params lp;
        lp.mode = p.loopMode; lp.length = p.length; lp.modify = p.loopModify;
        lp.playing = p.loopOn; lp.overdub = p.overdub && p.loopOn; lp.noDub = p.noDub;
        lp.classic = p.classic; lp.spread = loopSpread; lp.width = p.width;
        lp.fade = p.fade; lp.capacity = capacity; lp.coreRate = coreRate;

        // While listening, the loop records the input AND the Wet Channel output,
        // whatever the routing says. Overdubs record clean input only.
        const Stereo loopOut = loop.process (in, in + lastWet, in, lp);

        // ---- routing ----
        const bool both = p.loopOn && p.wetOn;
        Stereo wetIn = in;
        if (both)
        {
            if (p.routing == 1) wetIn = in + loopOut;
            else if (p.routing == 2) wetIn = loopOut;
        }
        if (! p.wetOn) wetIn = {}; // trails: let the tail ring out on silence

        const bool wetActive = p.wetOn || p.trails;
        Stereo wetOut;
        if (wetActive || wetGain > 0.0f)
            wetOut = wet.process (wetIn, wp);
        const float wTarget = wetActive ? 1.0f : 0.0f;
        wetGain += clampf (wTarget - wetGain, -1.0f / 256.0f, 1.0f / 256.0f);
        wetOut = wetOut * wetGain;
        lastWet = wetOut;

        // Loops routed through the Wet Channel become 100% wet unless Direct is raised
        const bool routedThroughWet = both && p.routing != 0;
        const Stereo loopContrib = routedThroughWet ? loopOut * clampf (p.direct, 0, 1) : loopOut;

        const float b = clampf (p.balance, -1, 1);
        const float gWet = b < 0 ? dbToGain (12.0f * b) : 1.0f;
        const float gLoop = b > 0 ? dbToGain (-12.0f * b) : 1.0f;

        Stereo core = wetOut * gWet + loopContrib * gLoop;
        core = { softClip (core.l), softClip (core.r) }; // safety: never louder than ~0 dBFS

        if (p.classic) // noise-full clock: hiss rises as the Clock drops
        {
            const float depth = 1.0f - coreRate / kTopRate;
            const float amp = 0.006f * depth * depth;
            core.l += rng.next() * amp;
            core.r += rng.next() * amp;
        }
        return core;
    }

    float fs = 48000.0f;
    float coreRate = kTopRate;
    float phase = 0.0f;
    float logRatio = 0.0f;
    float wetGain = 0.0f;
    int filterCounter = 0;
    Stereo prevOut, curOut, lastWet;
    OnePole inLp[2], outLp[2];
    WetChannel wet;
    LoopChannel loop;
    Rng rng;
    const EngineParams* params = nullptr;
};
} // namespace murmur
