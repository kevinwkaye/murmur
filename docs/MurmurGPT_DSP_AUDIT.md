# MurmurGPT engineering audit and change log
Updated: 2026-11-10. Baseline: `main` (CMake project version **0.3.0**). Work branch: `MurmurGPT`.

## Purpose and guardrails
Murmur and Murk are two modes of **one JUCE VST3/standalone application**. Improve musical fidelity against documented MOOD MKII and BAD MOOD behaviour while retaining each mode's distinct voice. Do not merge or release this branch until tests and listening comparisons pass. Keep `main` unchanged. The project uses JUCE 8.0.4 and C++17. Work from the *current GitHub source*, not the old v0.1 source ZIP.

The repo and code were available for examination, but **no matching dry-input/hardware-output reference recordings** were supplied. None of these findings can substantiate a claim of perceptual equivalence to Chase Bliss hardware. Preserve the intentionally degraded character of Classic, Murk MASK/CRUSH and Glue.

## Current code architecture verified in GitHub
- `Source/engine/MurmurEngine.h`: shared 2–64 kHz core clock, host-to-core input filtering, clock slew, sample interpolation back to host rate, wet/loop routing and overall gain.
- `Source/engine/WetChannel.h`: Murmur wet reverb, delay and Slip processing.
- `Source/engine/LoopChannel.h`: always-listening micro-looper, Env/Tape/Stretch playback and Murk loop delegation.
- `Source/engine/MurkWet.h`: Murk wet SOUP/RELAY/FLIP.
- `Source/engine/MurkLoop.h`: Murk BURST/RADIO/MASK.
- `Source/engine/MurkFx.h`: Glue, Cross and Tilt processing.
- `Source/engine/DspUtil.h`: common delay line, linear fractional taps, filters and nonlinear helper.
- `Tests/RenderTest.cpp`: offline scenario rendering; `Tests/PluginTest.cpp`: integration checks.

## Confirmed code observations / remaining work

### P1: Core clock rate conversion is not high-fidelity SRC
In `MurmurEngine::processChunk`, input is low-passed with **one `OnePole` stage** with cutoff `min(0.45*coreRate, 0.45*hostRate)`. A phase accumulator advances the core at `coreRate/hostRate`; at core rates above the host rate the same input sample is reused for multiple ticks. Host output is linearly interpolated between the latest two core samples and passed through another one-pole filter. This is economical and plausibly responsible for aliasing, blur or roughness, especially when sweeping clock. This architecture was **confirmed in the current repository**. Whether it is the perceptual limiting factor remains a hypothesis until controlled tests.

**Next action:** build signal-generator/offline sweeps for 44.1, 48, 96 kHz host rates and 2–64 kHz core steps; quantify alias rejection, response, modulation sidebands and CPU. Design a correctly filtered fractional resampler for a polished processing path. Keep a separate intentionally lo-fi clock behaviour for Classic. Changing the clock touches buffer duration, musical pitch, latency, freeze and tempo relationships: do NOT blindly replace it.

### P1: Tape loop seam causes periodic attenuation
In `LoopChannel::capture`, buffer beginning/end samples are multiplied by a short fade. In `LoopChannel::readPlayer` Tape mode, output is *again* scaled by `min(tapePos, Lw-tapePos)/fadeLen`, capped at 1. This creates amplitude dips on repeated loop boundaries even when source material is continuous. At low clock rates, the 128-core-sample fade can be long in real time. This is an actual behaviour of current code, not necessarily an erroneous artistic choice.

**Next action:** capture dry and wet output with fixed loop lengths and compare RMS/envelope dips. Prototype overlap-add read heads with transparent level across the seam in a separately testable change. Preserve existing behaviour as a switch or documented character if preferred.

### P2: Env held slice was crossfaded against the preceding slice
Original current-repo code at `LoopChannel::readPlayer` blends `readAt(readPos - SS)` into the end of a held Env slice, although its comment promises to crossfade to *the start of the current slice*. Playback then wraps `intra` to zero. This reads different recorded material and can cause mismatched seams.

**Changed on `MurmurGPT`:** blend the tail with the head of the *same* slice, and resume from the end of the overlap region after wrap. This is a deliberately narrow implementation change with a standalone finite-output smoke test. **Not yet confirmed as a perceptual improvement.** A/B against `main`, especially with moving controls and rapid Env threshold changes. The altered overlap shortens the effective repeated segment by up to 64 core samples, which should be checked for timing impact.

### P2: Stretch has simple grain mixing and interpolation
`LoopChannel.h` uses two or four `sin^2`-window grains and simple fractional reads. It does not visibly normalise every active grain against the sum of its windows. Depending on phase, different numbers of overlapping grains can change level. This does not prove a bug in any particular preset.

**Next action:** measure constant-signal amplitude ripple and pitch stability; consider envelope-sum normalisation where it helps, and compare before/after with percussive input. Don't make grain choices identical between polished Murmur and deliberately erratic Murk modes.

### P2: Fractional delay and loop reads use linear interpolation
`DspUtil::DelayLine::tap` and `LoopChannel::readAt` use linear fractional interpolation. This is stable and cheap but has fractional-position-dependent high-frequency loss and variable-delay coloration.

**Next action:** benchmark higher-quality interpolation *only where it produces an audible benefit*, checking CPU and ringing; it is not a substitute for anti-alias filtering in clock SRC.

### P2: Existing render-test pass/fail may miss audible jumps
The bundled v0.3.0 release's `render-test.txt` reported `ALL PASSED`, yet some scenarios reported large adjacent-sample jumps: clock bounce ~0.423; Murk Mask crush ~0.265; Glue wrecked ~1.062. These jumps might be intended distortion, high-frequency audio or faults. They **cannot** be classified as clicks solely from max adjacent-sample difference.

**Next action:** time-localise events and compute local spectral discontinuity / transient metrics, excluding intentional input and mode-switch transients; listen blind, compare to hardware and set mode-specific thresholds. Include a rate-sweep sine test and a host-block-size invariance test.

### P3: UI access crosses audio-thread-owned state
`LoopChannel` exposes non-atomic state/data for message-thread consumption, with a comment acknowledging benign races. C++ data races are undefined behaviour even when they appear benign. Address through atomic snapshots or a bounded lock-free UI snapshot, ensuring the audio thread never blocks or allocates.

## Additional improvements after the initial audit (2026-11-10)

- `Source/engine/DspUtil.h`: added a transposed-direct-form-II biquad low-pass, used as two stages with Butterworth Q values (0.5411961, 1.3065630). The normal clock path uses a four-pole input/output filter with a cutoff of the lower of `0.40*coreRate` and `0.43*hostRate`. This has significantly steeper out-of-band rejection **in the filter unit test**, but it is not an end-to-end polyphase band-limited SRC and does not prove the resulting guitar sound matches the reference.
- `Source/engine/MurmurEngine.h`: normal Murmur and Murk paths interpolate the filtered host input at the fractional time of each core tick, including cases in which the 64 kHz core runs more than once per 44.1/48 kHz host sample. The Murmur Classic path retains its previous simple sampling and one-pole filtering. Both filter families are processed so that switching modes does not start a completely cold filter.
- `Tests/ClockQualityTest.cpp`: added standalone frequency-response checks for 44.1/48/96 kHz host rates and 2/8/16/64 kHz core rates. This tests the filter implementation and a stopband target, not the whole engine's aliasing or perceptual quality.
- `CMakeLists.txt`: registered standalone Env and clock quality tests for full CMake builds.
- `.github/workflows/murmurgpt-windows.yml`: added a separate Windows VST3 and standalone preview build. It gates release publication on project build, DSP tests, plugin integration tests and CPU benchmark, then publishes a **GitHub prerelease** containing `MurmurGPT-Windows.zip`, rather than overwriting the official `builds` branch. Experimental Windows releases keep the original plugin identity: installations may replace the stable plugin. The job must report **success** before claiming a downloadable preview exists.

**Risk:** filter coefficient updates every 17 host samples as the clock moves; they are not continuously interpolated, and the new path's phase response and dry/wet interaction still require A/B listening and detailed sideband/sweep measurement. Treat the new path as a tested candidate, not a finished high-fidelity resampler.

## Preview 1 Windows build verification (2026-11-10)

**Release:** `MurmurGPT-preview-1`, built from commit `58db9c3102435af610c66d7bb481872e403901a1`. The Windows VST3 and standalone application compiled successfully. Clock response, Env slice regression, offline render tests, plugin integration tests and CPU benchmark all passed. The prerelease contains `MurmurGPT-Windows.zip`.

**CPU tradeoff:** Relative to the supplied v0.3.0 release reports, the preview's benchmark on Windows runners shows higher one-core CPU usage at 48 kHz (64-sample buffer): Murmur idle 1.60% -> 2.17%, Murmur busy 2.43% -> 4.29%, Murk idle with Glue 2.27% -> 5.52%, Murk worst-case Synth +1 octave 9.20% -> 11.71%. These runs may have used different runner hardware, so exact comparisons are indicative, not controlled. The new implementation deliberately processes both filter families to keep their states warm, which adds cost. **Next optimisation:** benchmark bypass-aware or crossfade-safe filter-path processing, maintaining audible transition quality without running redundant sections all the time.

The workflow did **not** run pluginval in this preview. Passing the included plugin integration tests is not equivalent to pluginval strictness-level-5 certification or actual DAW listening.

## Work completed on the branch
1. Created branch `MurmurGPT` from `main`.
2. Changed the Env held-slice wrap and within-slice overlap in `Source/engine/LoopChannel.h`. No preset mappings or plugin UI files changed.
3. Added `Tests/EnvSliceTest.cpp`, a separate finite-output/signal-presence regression smoke test with deliberately different source slices. This is not a full acoustic seam-quality test.
4. Added `.github/workflows/murmurgpt-validation.yml` with standalone tests, **without publishing a release or force-pushing the existing `builds` branches**.
5. Added these handover notes.

## Validation status
Use GitHub Actions > *MurmurGPT validation* for actual branch check results. A smoke test passing proves only basic runtime sanity. The original `main` build workflow publishes outputs to dedicated build branches: do **not** run it indiscriminately for this development branch. Avoid claiming a Windows VST build has been produced by `MurmurGPT` without a dedicated build run.

## Next implementation order
1. Stabilise and listen-test Env fix; add precise seam timing and click tests.
2. Create objective clock SRC sweep tests and benchmarks **before** changing the algorithm.
3. Prototype improved clock anti-aliasing/reconstruction in a reversible and measured way.
4. Measure Tape boundary amplitude dips, then add controlled crossfade alternatives.
5. Investigate Slip/grain normalization and reverb/diffusion in separate commits.
6. Hardware A/B with exact pedal model, reproducible settings, lossless dry and matched wet WAV files.

## Reference constraints
Official behavioural reference should come from Chase Bliss documentation and manuals for MOOD MKII and BAD MOOD. The exact proprietary algorithms are unknown. Do not copy copyrighted code or claim numerical recreation of undocumented internals.

## Handover notes for Claude
See `docs/CLAUDE_HANDOVER_MurmurGPT.md`. It is authoritative for changes completed on this branch and should be read together with `git diff main...MurmurGPT`, source code and actual CI logs.
