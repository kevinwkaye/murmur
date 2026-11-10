// Standalone deterministic DSP filter checks for MurmurGPT clock path.
// These check the designed filter response, not listening equivalence.
#include "DspUtil.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace murmur;

static double toneGain (float fs, float cutoff, float qFreq)
{
    LowpassBiquad first, second;
    first.setCutoff (cutoff, fs, 0.5411961f);
    second.setCutoff (cutoff, fs, 1.3065630f);
    double powerIn = 0.0, powerOut = 0.0;
    const int length = (int) (0.45f * fs);
    for (int i = 0; i < length; ++i)
    {
        const float x = std::sin (kTwoPi * qFreq * (float) i / fs);
        const float y = second.process (first.process (x));
        if (! std::isfinite (y)) return -1.0;
        if (i > (int) (0.1f * fs))
        {
            powerIn += (double) x * x;
            powerOut += (double) y * y;
        }
    }
    return std::sqrt (powerOut / std::max (1.0e-12, powerIn));
}

int main()
{
    for (const float fs : { 44100.0f, 48000.0f, 96000.0f })
    {
        for (const float core : { 2000.0f, 8000.0f, 16000.0f, 64000.0f })
        {
            const float cutoff = std::min (0.40f * core, 0.43f * fs);
            const float pass = std::min (cutoff * 0.15f, 3000.0f);
            const float stop = std::min (fs * 0.45f, cutoff * 3.0f);
            const double gPass = toneGain (fs, cutoff, pass);
            const double gStop = toneGain (fs, cutoff, stop);
            std::printf ("fs %5.0f core %5.0f: %5.0f Hz pass %.3f, %5.0f Hz stop %.3f\\n",
                         fs, core, pass, gPass, stop, gStop);
            if (! std::isfinite (gPass) || ! std::isfinite (gStop)
                || gPass < 0.88 || gPass > 1.12
                || gStop > 0.35)
            {
                std::puts ("FAIL: unexpected clock filter response");
                return 1;
            }
        }
    }

    // Spot check alias-control at 8 kHz core rate / 48 kHz host:
    // 9 kHz input is well above the 4 kHz core Nyquist.
    const double reject = toneGain (48000.0f, 3200.0f, 9000.0f);
    if (reject >= 0.04)
    {
        std::printf ("FAIL: 9 kHz leakage %.3f\\n", reject);
        return 1;
    }
    std::puts ("PASS: clock filter basic frequency-response checks");
    return 0;
}
