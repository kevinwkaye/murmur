// Focused offline regression for Env slice looping.
// Build with: c++ -std=c++17 -O2 -ISource/engine Tests/EnvSliceTest.cpp -o env_slice_test
#include "LoopChannel.h"
#include <cmath>
#include <cstdio>

int main()
{
    using namespace murmur;
    LoopChannel loop;
    loop.prepare (4096);
    LoopChannel::Params p;
    p.capacity = 4096;
    p.coreRate = 48000.0f;
    p.mode = LoopChannel::Env;
    p.length = 0.7f;
    p.modify = 1.0f; // low Env threshold, keep a held slice active

    // Record deliberately different material in successive slices to make
    // accidental blending with a preceding slice easier to detect by ear.
    for (int i = 0; i < 4096; ++i)
    {
        const float x = (i / 512) % 2 == 0
            ? 0.4f * std::sin (kTwoPi * (float) i / 91.0f)
            : 0.2f * std::sin (kTwoPi * (float) i / 37.0f);
        loop.process ({ 1.0f, 1.0f }, { x, x }, { 0, 0 }, p);
    }

    p.playing = true;
    double energy = 0.0;
    float peak = 0.0f;
    float maxDelta = 0.0f;
    float prev = 0.0f;
    for (int i = 0; i < 24000; ++i)
    {
        const auto y = loop.process ({ 1.0f, 1.0f }, {}, {}, p);
        if (! std::isfinite (y.l) || ! std::isfinite (y.r))
        {
            std::puts ("FAIL: non-finite Env slice output");
            return 1;
        }
        energy += (double) y.l * y.l + (double) y.r * y.r;
        peak = std::max (peak, std::max (std::fabs (y.l), std::fabs (y.r)));
        if (i > 256)
            maxDelta = std::max (maxDelta, std::fabs (y.l - prev));
        prev = y.l;
    }

    std::printf ("Env slice: energy=%.5f peak=%.5f maxDelta=%.5f\\n",
                 energy, peak, maxDelta);
    if (energy < 1.0 || peak > 2.0f)
    {
        std::puts ("FAIL: missing or runaway held-slice output");
        return 1;
    }
    std::puts ("PASS: Env slice finite-output and signal-presence smoke test");
    return 0;
}
