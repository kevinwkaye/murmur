// CPU benchmark for the Murmur engine (no JUCE needed).
// Renders 20 seconds of audio per scenario and reports the share of one CPU core
// that a single instance needs in real time, at 48 kHz with 64 and 128 sample buffers.
//
// Usage: murmur_bench

#include "MurmurEngine.h"
#include <chrono>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

using namespace murmur;

namespace
{
constexpr double kFs = 48000.0;
constexpr int kSeconds = 20;

double cpuPercent (int blockSize, const std::function<void (EngineParams&, double)>& scene)
{
    MurmurEngine engine;
    engine.prepare (kFs);
    EngineParams p;
    std::vector<float> l ((size_t) blockSize), r ((size_t) blockSize);

    const int blocks = (int) (kSeconds * kFs / blockSize);
    double busy = 0.0;
    uint32_t seed = 1;
    for (int b = 0; b < blocks; ++b)
    {
        const double t = b * blockSize / kFs;
        for (int i = 0; i < blockSize; ++i) // noisy guitar-ish input keeps every stage busy
        {
            seed = seed * 1664525u + 1013904223u;
            const float noise = (float) (seed >> 9) / 8388608.0f - 1.0f;
            l[(size_t) i] = r[(size_t) i] = 0.2f * std::sin ((float) (6.2831853 * 196.0 * (t + i / kFs))) + 0.02f * noise;
        }
        scene (p, t);
        const auto start = std::chrono::steady_clock::now();
        engine.process (l.data(), r.data(), blockSize, p);
        busy += std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();
    }
    return 100.0 * busy / kSeconds;
}

struct Scene { std::string name; std::function<void (EngineParams&, double)> set; };
} // namespace

int main()
{
    const std::vector<Scene> scenes {
        { "Classic, idle (both channels off)", [] (EngineParams& p, double) { p.wetOn = false; p.loopOn = false; } },
        { "Classic, reverb only", [] (EngineParams& p, double) { p.wetOn = true; p.wetMode = 0; p.wetModify = 0.7f; } },
        { "Classic, slip + tape loop", [] (EngineParams& p, double t) { p.wetOn = true; p.wetMode = 2; p.time = 0.7f; p.loopOn = t > 3; p.loopMode = 1; } },
        { "Classic, busy (reverb, stretch, spread, ramping)", [] (EngineParams& p, double t) {
              p.wetOn = true; p.wetMode = 0; p.wetModify = 0.8f; p.loopOn = t > 3; p.loopMode = 2; p.loopModify = 0.6f;
              p.spread = true; p.rampLoopMod = true; p.bounce = true; } },
    };

    // Worst case: Synth Mode an octave above the original pitch runs the engine at 128 kHz
    auto synthWorst = [] (int block)
    {
        MurmurEngine e;
        e.prepare (kFs);
        e.synthNoteOn (72, 1.0f);
        EngineParams p;
        p.wetOn = true; p.wetMode = 0; p.wetModify = 0.8f; p.loopOn = true; p.loopMode = 2; p.spread = true;
        std::vector<float> l ((size_t) block, 0.1f), r ((size_t) block, 0.1f);
        const int blocks = (int) (kSeconds * kFs / block);
        const auto start = std::chrono::steady_clock::now();
        for (int b = 0; b < blocks; ++b) e.process (l.data(), r.data(), block, p);
        return 100.0 * std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count() / kSeconds;
    };

    std::printf ("Murmur CPU per instance at 48 kHz (share of one core)\n\n");
    std::printf ("%-52s %10s %10s\n", "Scenario", "64 smp", "128 smp");
    for (const auto& s : scenes)
        std::printf ("%-52s %9.2f%% %9.2f%%\n", s.name.c_str(), cpuPercent (64, s.set), cpuPercent (128, s.set));
    std::printf ("%-52s %9.2f%% %9.2f%%\n", "Classic, worst case (Synth Mode +1 oct, 128 kHz)", synthWorst (64), synthWorst (128));
    return 0;
}
