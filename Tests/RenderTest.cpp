// Offline render test for the Murmur engine (no JUCE needed).
// Runs a set of scenarios, checks for NaNs, runaway levels and clicks,
// and optionally writes WAV files so you can listen to each one.
//
// Usage: murmur_render_test [output_folder]

#include "MurmurEngine.h"
#include <cstdio>
#include <functional>
#include <string>

using namespace murmur;

static constexpr float kFs = 48000.0f;

// A simple plucked-note phrase: a few notes, then silence
static void makeInput (std::vector<float>& l, std::vector<float>& r, int n)
{
    l.assign ((size_t) n, 0.0f);
    r.assign ((size_t) n, 0.0f);
    const float notes[6] = { 220.0f, 277.18f, 329.63f, 440.0f, 329.63f, 277.18f };
    for (int k = 0; k < 6; ++k)
    {
        const int s0 = (int) (k * 0.45f * kFs);
        for (int i = 0; i < (int) (1.2f * kFs) && s0 + i < n; ++i)
        {
            const float t = (float) i / kFs;
            const float e = std::exp (-t * 3.5f) * std::min (1.0f, t * 400.0f);
            const float f = notes[k];
            const float v = 0.25f * e * (std::sin (kTwoPi * f * t) + 0.4f * std::sin (kTwoPi * 2 * f * t) + 0.2f * std::sin (kTwoPi * 3 * f * t));
            l[(size_t) (s0 + i)] += v;
            r[(size_t) (s0 + i)] += v * (k % 2 ? 0.6f : 1.0f);
        }
    }
}

static void writeWav (const std::string& path, const std::vector<float>& l, const std::vector<float>& r)
{
    FILE* f = std::fopen (path.c_str(), "wb");
    if (! f) return;
    const uint32_t n = (uint32_t) l.size(), dataBytes = n * 4, sr = (uint32_t) kFs;
    auto u32 = [&] (uint32_t v) { std::fwrite (&v, 4, 1, f); };
    auto u16 = [&] (uint16_t v) { std::fwrite (&v, 2, 1, f); };
    std::fwrite ("RIFF", 1, 4, f); u32 (36 + dataBytes); std::fwrite ("WAVE", 1, 4, f);
    std::fwrite ("fmt ", 1, 4, f); u32 (16); u16 (1); u16 (2); u32 (sr); u32 (sr * 4); u16 (4); u16 (16);
    std::fwrite ("data", 1, 4, f); u32 (dataBytes);
    for (uint32_t i = 0; i < n; ++i)
    {
        int16_t a = (int16_t) (clampf (l[i], -1, 1) * 32767.0f);
        int16_t b = (int16_t) (clampf (r[i], -1, 1) * 32767.0f);
        std::fwrite (&a, 2, 1, f); std::fwrite (&b, 2, 1, f);
    }
    std::fclose (f);
}

static MurmurEngine* g_eng = nullptr; // lets a scenario play MIDI notes

struct Result { float peak = 0, rms = 0, tailRms = 0, maxJump = 0, jumpAt = 0; bool nan = false; };

// script(t, params) is called once per block so a scenario can press buttons over time
static Result run (const std::string& name, float seconds,
                   const std::function<void (float, EngineParams&)>& script,
                   const std::string& outDir)
{
    const int n = (int) (seconds * kFs);
    std::vector<float> inL, inR;
    makeInput (inL, inR, n);
    std::vector<float> l = inL, r = inR;

    MurmurEngine eng;
    eng.prepare (kFs);
    g_eng = &eng;
    EngineParams p;
    p.mix = 1.0f;
    p.dryKill = true;

    const int block = 256;
    for (int s = 0; s < n; s += block)
    {
        script ((float) s / kFs, p);
        eng.process (l.data() + s, r.data() + s, std::min (block, n - s), p);
    }

    Result res;
    double sum = 0, tail = 0;
    const int tailStart = n - (int) (2.0f * kFs);
    for (int i = 0; i < n; ++i)
    {
        const float a = l[(size_t) i], b = r[(size_t) i];
        if (! std::isfinite (a) || ! std::isfinite (b)) { res.nan = true; break; }
        res.peak = std::max (res.peak, std::max (std::fabs (a), std::fabs (b)));
        sum += a * a + b * b;
        if (i >= tailStart) tail += a * a + b * b;
        if (i > 0)
        {
            const float j = std::max (std::fabs (a - l[(size_t) i - 1]), std::fabs (b - r[(size_t) i - 1]));
            if (j > res.maxJump) { res.maxJump = j; res.jumpAt = (float) i / kFs; }
        }
    }
    res.rms = (float) std::sqrt (sum / (2.0 * n));
    res.tailRms = (float) std::sqrt (tail / (2.0 * (n - tailStart)));

    std::printf ("%-34s peak %6.3f  rms %6.3f  tail %6.3f  jump %5.3f @%5.2fs %s\n",
                 name.c_str(), res.peak, res.rms, res.tailRms, res.maxJump, res.jumpAt, res.nan ? " NaN!" : "");
    if (! outDir.empty())
        writeWav (outDir + "/" + name + ".wav", l, r);
    return res;
}

int main (int argc, char** argv)
{
    const std::string out = argc > 1 ? argv[1] : "";
    int failures = 0;
    auto check = [&] (const Result& r, float maxPeak = 1.5f)
    {
        if (r.nan || r.peak > maxPeak) { ++failures; std::printf ("   ^ FAIL\n"); }
    };

    // ---- Wet Channel ----
    for (float m : { 0.0f, 0.5f, 1.0f })
        check (run ("reverb_modify_" + std::to_string ((int) (m * 100)), 8,
                    [m] (float, EngineParams& p) { p.wetOn = true; p.wetMode = 0; p.time = 0.7f; p.wetModify = m; }, out));
    check (run ("delay_fb60", 8, [] (float, EngineParams& p) { p.wetOn = true; p.wetMode = 1; p.time = 0.6f; p.wetModify = 0.6f; }, out));
    check (run ("delay_fb100_looping", 12, [] (float, EngineParams& p) { p.wetOn = true; p.wetMode = 1; p.time = 0.75f; p.wetModify = 1.0f; }, out));
    check (run ("delay_time_sweep", 8, [] (float t, EngineParams& p) { p.wetOn = true; p.wetMode = 1; p.time = 0.4f + 0.05f * std::floor (t); p.wetModify = 0.6f; }, out));
    check (run ("delay_time_sweep_classic", 8, [] (float t, EngineParams& p) { p.classic = true; p.wetOn = true; p.wetMode = 1; p.time = 0.4f + 0.05f * std::floor (t); p.wetModify = 0.6f; }, out));
    for (float m : { 0.0f, 0.5f, 0.75f, 1.0f })
        check (run ("slip_modify_" + std::to_string ((int) (m * 100)), 8,
                    [m] (float, EngineParams& p) { p.wetOn = true; p.wetMode = 2; p.time = 0.5f; p.wetModify = m; }, out));

    // ---- Freeze ----
    for (int mode = 0; mode < 3; ++mode)
        check (run ("freeze_mode" + std::to_string (mode), 10,
                    [mode] (float t, EngineParams& p) { p.wetOn = true; p.wetMode = mode; p.time = 0.6f; p.wetModify = mode == 0 ? 0.2f : 0.6f; p.freeze = t > 1.5f; if (mode == 0 && t > 5) p.wetModify = 0.8f; }, out));

    // ---- Micro-Looper ----
    const float tapeSettings[4] = { 0.70f, 0.15f, 0.85f, 0.55f }; // 1x fwd, 2x rev, 2x fwd, 0.5x fwd
    for (float m : tapeSettings)
        check (run ("tape_modify_" + std::to_string ((int) (m * 100)), 10,
                    [m] (float t, EngineParams& p) { p.loopOn = t > 2.8f; p.loopMode = 1; p.loopModify = m; p.length = 1.0f; }, out));
    check (run ("tape_short_length", 10, [] (float t, EngineParams& p) { p.loopOn = t > 2.8f; p.loopMode = 1; p.loopModify = 0.7f; p.length = 0.3f; }, out));
    check (run ("env_mode", 10, [] (float t, EngineParams& p) { p.loopOn = t > 1.0f; p.loopMode = 0; p.loopModify = 0.8f; p.length = 0.4f; }, out));
    for (float m : { 0.2f, 0.5f, 0.7f })
        check (run ("stretch_modify_" + std::to_string ((int) (m * 100)), 10,
                    [m] (float t, EngineParams& p) { p.loopOn = t > 2.8f; p.loopMode = 2; p.loopModify = m; p.length = 0.4f; }, out));
    check (run ("overdub", 10, [] (float t, EngineParams& p) { p.loopOn = t > 1.0f; p.overdub = t > 1.0f && t < 3.0f; p.loopMode = 1; p.loopModify = 0.7f; }, out));
    check (run ("refresh_nodub", 10, [] (float t, EngineParams& p) { p.loopOn = t > 0.5f; p.overdub = t > 0.5f; p.noDub = true; p.loopMode = 2; p.loopModify = 0.6f; p.clock = 0.85f; }, out));

    // ---- Interaction ----
    check (run ("trail_catcher_reverb_into_loop", 12, [] (float t, EngineParams& p) { p.wetOn = true; p.wetMode = 0; p.wetModify = 1.0f; p.time = 0.8f; p.loopOn = t > 2.8f; p.routing = 2; p.loopModify = 0.7f; }, out));
    check (run ("loop_through_delay_both", 12, [] (float t, EngineParams& p) { p.wetOn = true; p.wetMode = 1; p.wetModify = 0.5f; p.time = 0.6f; p.loopOn = t > 2.8f; p.routing = 1; p.direct = 0.5f; }, out));

    // ---- Clock ----
    check (run ("clock_low_loop", 12, [] (float t, EngineParams& p) { p.clock = 0.6f; p.loopOn = t > 3.0f; p.loopModify = 0.7f; p.wetOn = true; p.wetMode = 0; p.wetModify = 0.7f; }, out));
    check (run ("clock_steps_while_looping", 12, [] (float t, EngineParams& p) { p.loopOn = t > 2.8f; p.loopModify = 0.7f; p.clock = 1.0f - 0.08f * std::floor (std::max (0.0f, t - 3.0f)); }, out));
    check (run ("clock_smooth_sweep", 12, [] (float t, EngineParams& p) { p.smoothClock = true; p.loopOn = t > 2.8f; p.loopModify = 0.7f; p.clock = 1.0f - 0.05f * std::max (0.0f, t - 3.0f); }, out));

    // ---- Stereo and Classic ----
    for (int mode = 0; mode < 3; ++mode)
        check (run ("spread_wet_mode" + std::to_string (mode), 8,
                    [mode] (float, EngineParams& p) { p.spread = true; p.wetOn = true; p.wetMode = mode; p.wetModify = mode == 2 ? 0.75f : 0.6f; }, out));
    for (int mode = 0; mode < 3; ++mode)
        check (run ("spread_loop_mode" + std::to_string (mode), 10,
                    [mode] (float t, EngineParams& p) { p.spread = true; p.loopOn = t > 2.8f; p.loopMode = mode; p.loopModify = mode == 2 ? 0.6f : 0.7f; p.length = 0.5f; }, out));
    check (run ("classic_everything", 12, [] (float t, EngineParams& p) { p.classic = true; p.clock = 0.55f; p.wetOn = true; p.wetMode = 0; p.wetModify = 0.6f; p.loopOn = t > 2.8f; p.loopMode = 2; p.loopModify = 0.65f; }, out));

    // ---- Motion, tempo and Synth Mode ----
    check (run ("bounce_clock_triangle", 10, [] (float t, EngineParams& p) { p.loopOn = t > 2.8f; p.rampClock = true; p.bounce = true; p.rampSpeed = 0.3f; p.rampWave = 0.0f; p.clock = 1.0f; }, out));
    check (run ("bounce_time_square_delay", 10, [] (float, EngineParams& p) { p.wetOn = true; p.wetMode = 1; p.wetModify = 0.5f; p.rampTime = true; p.rampSpeed = 0.5f; p.rampWave = 0.25f; p.time = 0.7f; }, out));
    check (run ("bounce_random_loopmod", 10, [] (float t, EngineParams& p) { p.loopOn = t > 2.8f; p.loopMode = 1; p.rampLoopMod = true; p.rampSpeed = 0.6f; p.rampWave = 1.0f; p.sweepTop = true; p.loopModify = 0.5f; }, out));
    check (run ("ramp_once_clock", 8, [] (float t, EngineParams& p) { p.wetOn = t > 1.0f; p.wetMode = 0; p.rampClock = true; p.bounce = false; p.rampSpeed = 0.2f; p.clock = 1.0f; }, out));
    check (run ("expression_wetmod", 8, [] (float t, EngineParams& p) { p.wetOn = true; p.wetMode = 0; p.rampWetMod = true; p.modSource = 1; p.expression = clampf (t / 8.0f, 0, 1); }, out));
    check (run ("tempo_sync_delay_8th", 8, [] (float, EngineParams& p) { p.wetOn = true; p.wetMode = 1; p.wetModify = 0.5f; p.bpm = 120.0f; p.time = 0.55f; }, out));
    check (run ("tempo_sync_loop_bar", 10, [] (float t, EngineParams& p) { p.bpm = 120.0f; p.loopOn = t > 2.5f; p.loopMode = 1; p.length = 0.75f; }, out));
    check (run ("synth_adsr_notes", 10, [] (float t, EngineParams& p) {
        static int last = -1;
        p.loopOn = t > 2.8f; p.loopMode = 2; p.loopModify = 0.5f; p.synthGate = 2;
        const int step = (int) ((t - 3.0f) / 0.5f);
        if (t > 3.0f && step != last) { if (last >= 0) g_eng->synthNoteOff (55 + (last % 4) * 3); g_eng->synthNoteOn (55 + (step % 4) * 3, 0.9f); last = step; }
        if (t < 0.01f) last = -1; }, out));
    check (run ("synth_drone_glide", 8, [] (float t, EngineParams& p) {
        p.wetOn = true; p.wetMode = 0; p.wetModify = 0.8f; p.synthGate = 0; p.glide = 0.3f;
        if (t > 2.0f && t < 2.01f) g_eng->synthNoteOn (48, 1.0f);
        if (t > 4.0f && t < 4.01f) g_eng->synthNoteOn (60, 1.0f); }, out));

    std::printf ("\n%s (%d failures)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
