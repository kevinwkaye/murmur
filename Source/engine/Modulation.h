#pragma once

// Built-in movement, copying the pedal's left dip-switch bank:
//  - Bounce: the chosen knobs move continuously between their position and the
//    top or bottom of their range, using a waveform that morphs from Triangle to
//    Square, Sine, Random and Smooth Random.
//  - Ramp: when a channel is switched on, the chosen knobs rise or fall to their
//    position once, then stay there.
//  - Expression: the same knobs follow an expression value instead.
// Plus Synth Mode: MIDI notes move the Clock in semitones, with a gate or ADSR.

#include "DspUtil.h"

namespace murmur
{
class RampEngine
{
public:
    // Returns u in 0..1, where 1 means "at the knob position" and 0 means "at the far end".
    float tick (float dtSec, bool bounce, float speed, float wave, bool trigger)
    {
        const float hz = 0.05f * std::pow (200.0f, clampf (speed, 0, 1)); // 0.05 Hz to 10 Hz
        if (trigger)
        {
            rampPos = 0.0f;
            phase = 0.0f;
        }

        float u;
        if (bounce)
        {
            phase += hz * dtSec;
            if (phase >= 1.0f)
            {
                phase -= std::floor (phase);
                prevRand = nextRand;
                nextRand = 0.5f * (rng.next() + 1.0f);
            }
            u = 1.0f - shape (phase, wave);
        }
        else
        {
            rampPos = std::min (1.0f, rampPos + hz * dtSec); // one rise or fall, then hold
            const float s = rampPos * rampPos * (3.0f - 2.0f * rampPos);
            u = s;
        }

        // Light smoothing so Square and Random don't click
        const float a = 1.0f - std::exp (-dtSec / 0.004f);
        smooth += (u - smooth) * a;
        return smooth;
    }

    void reset() { phase = 0; rampPos = 1; smooth = 1; }

private:
    float shape (float p, float wave)
    {
        const float pos = clampf (wave, 0, 1) * 4.0f;
        const int i = std::min (3, (int) pos);
        const float f = pos - (float) i;
        return lerpf (basic (i, p), basic (i + 1, p), f);
    }

    float basic (int which, float p)
    {
        switch (which)
        {
            case 0: return p < 0.5f ? 2.0f * p : 2.0f - 2.0f * p;            // triangle
            case 1: return p < 0.5f ? 1.0f : 0.0f;                           // square
            case 2: return 0.5f - 0.5f * std::cos (kTwoPi * p);              // sine
            case 3: return nextRand;                                        // random (stepped)
            default:                                                        // smooth random
            {
                const float s = 0.5f - 0.5f * std::cos (kPi * p);
                return lerpf (prevRand, nextRand, s);
            }
        }
    }

    float phase = 0.0f, rampPos = 1.0f, smooth = 1.0f;
    float prevRand = 0.5f, nextRand = 0.5f;
    Rng rng;
};

// Applies u to a knob: Bottom sweeps between 0 and the knob, Top between the knob and 1.
inline float applySweep (float knob, float u, bool top)
{
    return top ? lerpf (1.0f, knob, u) : lerpf (0.0f, knob, u);
}

class SynthVoice
{
public:
    enum Gate { Drone = 0, OnOff = 1, Adsr = 2 };

    void noteOn (int note, float velocity)
    {
        active = true;
        heldNote = note;
        held = true;
        vel = clampf (velocity, 0, 1);
        if (stage == Idle || stage == Release) stage = Attack;
        ++notesDown;
    }

    void noteOff (int note)
    {
        notesDown = std::max (0, notesDown - 1);
        if (note == heldNote && notesDown == 0)
        {
            held = false;
            stage = Release;
        }
    }

    void pitchBend (float semis) { bend = semis; }
    void exit() { active = false; held = false; stage = Idle; env = 0; notesDown = 0; }
    bool isActive() const { return active; }
    int note() const { return heldNote; }

    // Clock position in log2 below 64 kHz for the current note
    float targetLog2 (int root) const
    {
        return clampf ((float) (heldNote - root) / 12.0f + bend / 12.0f, -5.0f, 1.0f);
    }

    // Output gain for the wet signal, advanced one host sample
    float gain (int gate, float a, float d, float s, float r, float dtSec)
    {
        if (! active) return 1.0f;
        if (gate == Drone) return 1.0f;
        if (gate == OnOff)
        {
            const float target = held ? vel : 0.0f;
            env += (target - env) * (1.0f - std::exp (-dtSec / 0.003f));
            return env;
        }
        switch (stage)
        {
            case Attack:
                env += dtSec / std::max (0.001f, a);
                if (env >= 1.0f) { env = 1.0f; stage = Decay; }
                break;
            case Decay:
                env += (s - env) * (1.0f - std::exp (-dtSec / std::max (0.001f, d * 0.3f)));
                if (! held) stage = Release;
                break;
            case Release:
                env *= std::exp (-dtSec / std::max (0.001f, r * 0.3f));
                if (env < 1e-4f) { env = 0.0f; stage = Idle; }
                break;
            default: break;
        }
        return env * vel;
    }

private:
    enum Stage { Idle, Attack, Decay, Release };
    bool active = false, held = false;
    int heldNote = 60, notesDown = 0;
    float vel = 1.0f, bend = 0.0f, env = 0.0f;
    Stage stage = Idle;
};
} // namespace murmur
