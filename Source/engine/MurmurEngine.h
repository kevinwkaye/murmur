#pragma once

// Top-level engine: input stage, the variable-rate Clock, both channels, routing
// and the mixer. The host calls process() at its own sample rate. Internally the
// channels run at the Clock rate (2 kHz to 64 kHz), so lowering the Clock slows,
// lengthens and degrades everything together.

#include "LoopChannel.h"
#include "Modulation.h"
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
    int stepSet = 0;             // 0 pedal (11 stops), 1 octaves, 2 fourths and fifths, 3 semitones

    // ramping (the pedal's left dip-switch bank) and expression
    bool rampTime = false, rampWetMod = false, rampClock = false, rampLoopMod = false, rampLength = false;
    bool bounce = true;          // false = ramp once when a channel turns on
    bool sweepTop = false;       // knob sets the bottom of the movement (true) or the top (false)
    float rampSpeed = 0.4f;      // 0..1, 0.05 Hz to 10 Hz
    float rampWave = 0.0f;       // 0..1 morphs Triangle, Square, Sine, Random, Smooth Random
    int modSource = 0;           // 0 ramp, 1 expression
    float expression = 0.0f;     // 0 heel .. 1 toe
    bool expReverse = false;

    // tempo: > 0 when synced to Ableton or tap tempo
    float bpm = 0.0f;

    // Synth Mode
    int synthGate = 0;           // 0 drone (the pedal's default), 1 gate, 2 ADSR
    float attack = 0.04f, decay = 0.3f, sustain = 0.7f, release = 1.2f, glide = 0.08f; // seconds / level
    int synthRoot = 60;          // MIDI note that plays at the original pitch (64 kHz)
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
        ramp.reset();
        synth.exit();
        wasOn = false;
    }

    // Musical clock: knob position -> log2 ratio below 64 kHz, snapped to the chosen step set.
    // Step set 0 copies the pedal: 11 stops at 64, 48, 32, 24, 16, 12, 8, 6, 4, 3 and 2 kHz
    // (each octave and a fourth below it).
    static float clockLog2 (float knob, int stepSet, bool smooth)
    {
        const float k = clampf (knob, 0, 1);
        const float semis = -60.0f * (1.0f - k); // five octaves
        if (smooth)
            return semis / 12.0f;
        if (stepSet == 0) // pedal
        {
            const int stop = 10 - (int) std::lround (k * 10.0f); // 0 = 64 kHz ... 10 = 2 kHz
            return -(float) (stop / 2) - ((stop % 2) ? std::log2 (4.0f / 3.0f) : 0.0f);
        }
        if (stepSet == 1) // octaves
            return std::round (semis / 12.0f);
        if (stepSet == 3) // semitones
            return std::round (semis) / 12.0f;

        // fourths and fifths: ratios 1, 4/3, 3/2 within each octave
        const float target = semis / 12.0f;
        float best = 0.0f, bestDist = 1e9f;
        const float steps[3] = { 0.0f, std::log2 (4.0f / 3.0f), std::log2 (3.0f / 2.0f) };
        for (int o = -5; o <= 0; ++o)
            for (float st : steps)
            {
                const float c = (float) o + st;
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

    void process (float* left, float* right, int numSamples, const EngineParams& raw)
    {
        int done = 0;
        while (done < numSamples)
        {
            const int n = std::min (32, numSamples - done);
            cur = raw;
            applyMotion (cur, (float) n / fs);
            processChunk (left + done, right + done, n, cur);
            done += n;
        }
        uiClockHz.store (kTopRate * std::exp2 (logRatio), std::memory_order_relaxed);
        uiInputLevel.store (inPeak, std::memory_order_relaxed);
    }

    std::atomic<float> uiInputLevel { 0.0f };

    // ---- Synth Mode (MIDI notes move the Clock) ----
    void synthNoteOn (int note, float velocity) { synth.noteOn (note, velocity); }
    void synthNoteOff (int note) { synth.noteOff (note); }
    void synthPitchBend (float semis) { synth.pitchBend (semis); }
    void synthExit() { synth.exit(); }
    bool isSynthActive() const { return synth.isActive(); }
    int synthNote() const { return synth.note(); }

    // Knob positions after ramping, for the UI
    std::atomic<float> uiTime { 0.5f }, uiWetMod { 0.5f }, uiClock { 1.0f }, uiLoopMod { 0.5f }, uiLength { 1.0f };

    const LoopChannel& getLoop() const { return loop; }
    std::atomic<float> uiClockHz { kTopRate };

private:
    void applyMotion (EngineParams& p, float dtSec)
    {
        const bool anyOn = p.wetOn || p.loopOn;
        const bool trigger = (anyOn && ! wasOn) || p.bounce != lastBounce;
        wasOn = anyOn;
        lastBounce = p.bounce;

        float u;
        if (p.modSource == 1)
        {
            const float e = p.expReverse ? 1.0f - clampf (p.expression, 0, 1) : clampf (p.expression, 0, 1);
            u = 1.0f - e;
        }
        else
        {
            u = ramp.tick (dtSec, p.bounce, p.rampSpeed, p.rampWave, trigger);
        }

        if (p.rampTime)    p.time = applySweep (p.time, u, p.sweepTop);
        if (p.rampWetMod)  p.wetModify = applySweep (p.wetModify, u, p.sweepTop);
        if (p.rampClock)   p.clock = applySweep (p.clock, u, p.sweepTop);
        if (p.rampLoopMod) p.loopModify = applySweep (p.loopModify, u, p.sweepTop);
        if (p.rampLength)  p.length = applySweep (p.length, u, p.sweepTop);

        uiTime.store (p.time, std::memory_order_relaxed);
        uiWetMod.store (p.wetModify, std::memory_order_relaxed);
        uiClock.store (p.clock, std::memory_order_relaxed);
        uiLoopMod.store (p.loopModify, std::memory_order_relaxed);
        uiLength.store (p.length, std::memory_order_relaxed);
    }

    void processChunk (float* left, float* right, int numSamples, const EngineParams& p)
    {
        const bool synthOn = synth.isActive();
        const float target = synthOn ? synth.targetLog2 (p.synthRoot) : clockLog2 (p.clock, p.stepSet, p.smoothClock);
        if (logRatio < -100.0f) logRatio = target;
        const float slewSec = synthOn ? std::max (0.002f, p.glide) : (p.smoothClock ? 0.04f : 0.003f);
        const float slew = 1.0f - std::exp (-1.0f / (slewSec * fs));
        const float dt = 1.0f / fs;

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
            inPeak = std::max (inPeak * 0.99995f, std::max (std::fabs (inL), std::fabs (inR)));

            const float aL = inLp[0].process (inL);
            const float aR = inLp[1].process (inR);

            phase += rate / fs;
            while (phase >= 1.0f)
            {
                phase -= 1.0f;
                prevOut = curOut;
                curOut = coreTick ({ aL, aR });
            }

            float oL = outLp[0].process (prevOut.l + (curOut.l - prevOut.l) * phase);
            float oR = outLp[1].process (prevOut.r + (curOut.r - prevOut.r) * phase);

            // Synth Mode gates MOOD's output with the note
            const float g = synth.gain (p.synthGate, p.attack, p.decay, p.sustain, p.release, dt);
            oL *= g;
            oR *= g;

            left[i] = (inL * dryG + oL * wetG) * outGain;
            right[i] = (inR * dryG + oR * wetG) * outGain;
        }
    }

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

        // ---- tempo sync: Time snaps to note values, the loop listens for one bar ----
        if (p.bpm > 0.0f)
        {
            static constexpr float noteBeats[6] = { 0.125f, 0.25f, 1.0f / 3.0f, 0.5f, 0.75f, 1.0f }; // 32nd .. quarter
            static constexpr float lengthFrac[6] = { 1.0f / 32, 1.0f / 16, 1.0f / 8, 0.25f, 0.5f, 1.0f };
            const float secPerBeat = 60.0f / p.bpm;
            const int ni = std::min (5, (int) (clampf (p.time, 0, 1) * 6.0f));
            wp.fixedLen = noteBeats[ni] * secPerBeat * coreRate;
            if (p.sync != 1)
                capacity = (int) clampf (4.0f * secPerBeat * coreRate, 256.0f, (float) (kMaxSeconds * kTopRate));
            tapeFraction = lengthFrac[std::min (5, (int) (clampf (p.length, 0, 1) * 6.0f))];
        }
        else
        {
            tapeFraction = 0.0f;
        }

        LoopChannel::Params lp;
        lp.mode = p.loopMode; lp.length = p.length; lp.modify = p.loopModify;
        lp.playing = p.loopOn; lp.overdub = p.overdub && p.loopOn; lp.noDub = p.noDub;
        lp.classic = p.classic; lp.spread = loopSpread; lp.width = p.width;
        lp.fade = p.fade; lp.capacity = capacity; lp.coreRate = coreRate;
        lp.lengthFraction = tapeFraction;

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
    EngineParams cur;
    RampEngine ramp;
    SynthVoice synth;
    bool wasOn = false, lastBounce = true;
    float tapeFraction = 0.0f;
    float inPeak = 0.0f;
};
} // namespace murmur
