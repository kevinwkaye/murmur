# Claude fidelity pass, 11 November 2026

Branch: `MurmurGPT`, on top of ChatGPT's work (head `9cb2099` when this pass started). `main` is untouched.

Kevin's brief: the functions are right but the plugin doesn't sound much like the pedals. He supplied two YouTube demos (128 kbps MP3): one on the Bad Mood with comparisons near the end, one on the MOOD MKII.

## How the demos were analysed

There's no dry signal and no transcript (the speech model download was blocked), so only things that don't depend on the source were measured. The MP3s cut everything above 16 kHz, so nothing up there is evidence.

- Pedal-only passages were found on spectrograms (speech shows as broadband stripes). The longest are MOOD MKII 6:17 to 6:55 and 8:17 to 8:52, and Bad Mood 9:38 to 9:51 and 12:30 to 13:01, plus many shorter breaks.
- Reverb tails were found automatically (a peak followed by a fall of 12 dB or more), then measured for echo density, decay time per band and left/right correlation.
- Steady partials were tracked for pitch wobble.

| Trait | MOOD MKII demo | Bad Mood demo | Murmur v0.3.0 and MurmurGPT before this pass |
| --- | --- | --- | --- |
| Tail echo density (1 = smooth, noise-like) | 1.05 to 1.31 | 1.10 to 1.13 | 1.08 to 1.09 (fine) |
| Tail decay, high band against low band | 0.3 to 0.45 (for example low 7.5 s, mid 3.3 s, high 2.2 s) | about even | 1.05 to 1.27: the highs rang as long as or longer than the lows |
| Tail left/right correlation | 0.55 to 0.94 | 0.19 to 1.0 | 0.01 to 0.11: two unrelated sides |
| Pitch wobble on steady partials | 10 to 17 cents at 1 to 4 Hz (some may be the demo synth) | similar, wider in places | 0.5 cents at 11 Hz |
| Band limit in clocked-down passages | a clean ceiling at about 7 to 8 kHz, gritty haze below it | broadband, saturated right up to the MP3's 16 kHz | v0.3.0: images up to about 10 kHz. MurmurGPT: a brick wall far below the clock's band |

Published descriptions back up the clock reading. Chase Bliss says the original MOOD's clock "deliberately includes noise and artifacts" and that "noise and aliasing become more noticeable at around 8k and lower" (chasebliss.com/mood). Guitar.com calls the MKII's low clock "grittily lo-fi... without adding hiss", its reverb "vast" and its delay "ultra-clean".

## What was measured in the plugin

Same machine, same renders, v0.3.0 (`main`) against MurmurGPT `9cb2099`:

- **CPU:** ChatGPT's filters cost almost nothing. Every benchmark scenario was within run-to-run noise of v0.3.0 (for example Murk idle 2.16 to 2.25% against 2.30 to 2.50%). The large rises in the earlier handover came from comparing different cloud machines.
- **Stretch:** a steady 330 Hz tone swung **22 dB** in level. The two overlapping grains started wherever the moving centre was, so they drifted in and out of phase and cancelled. This was the clearest "mechanical, digital" fault found.
- **Tape** seams: 0.4 to 0.9 dB, fine. **Env**: 0.4 to 0.5 dB, fine.
- **Murk Radio:** Ambient 3.3 dB, fine. Orchestral 11 dB comes from its deliberate voice swells. Shoegaze 25 dB comes from its random grain cloud. Both are left for listening.
- **Low Clock (about 4 kHz core):**

| Version | 1 kHz tone's image at 3 kHz | 3 kHz tone's alias at 1 kHz (the grit) |
| --- | --- | --- |
| v0.3.0 | -38.1 dBFS | -19.1 dBFS |
| MurmurGPT before this pass | -53.5 dBFS | -34.6 dBFS (grit mostly gone, top muffled) |
| This pass | -50.5 dBFS | -18.0 dBFS |

## Changes in this pass

Each change is a separate commit and can be reverted on its own.

1. **Stretch grains line up** (`LoopChannel.h`, `alignedGrainStart`). Each new grain starts where its waveform best matches what the outgoing grain is about to play: a normalised correlation search of up to 10 ms round the nominal start, coarse steps then fine. This is the same fix Backtrack v0.2 used, and is in `dsp-lessons.md`. Classic keeps its scattered, wispy grains. Steady-tone flutter went from 22 dB to 0.0 to 0.8 dB.
2. **Reverb tails darken as they decay** (`WetChannel.h`).
   - Each feedback-network pass loses more treble than bass, Jot-style. The gain at about 4 kHz (at the 64 kHz clock) is set so the treble's decay time is a quarter of the bass's per pass, which comes out at 0.51 to 0.56 overall.
   - The taps' own feedback loop is damped too.
   - Line modulation is slower and deeper: about 0.6 to 1.3 Hz instead of 7 to 12 Hz.
   - The output blends a third of each side into the other, giving about 0.65 correlation instead of 0.1. Spread keeps full width.
   - Classic keeps the old behaviour.
   - Measured: high/low decay 1.03 became 0.56 at Time 0.4, and 0.92 became 0.51 at Time 0.7.
3. **Clock keeps its grit, loses its fizz** (`MurmurEngine.h`).
   - On the way in, the gentle one-pole filter is used again, so low clocks alias like the pedal. ChatGPT's steep input filter is kept behind `kGrittyInput` for comparison.
   - On the way out, ChatGPT's four-pole filter is kept, so there are no mirror images above the clock's band. Its cutoff rose from 0.40 to 0.45 of the clock rate, because 0.40 was dulling the top of the band.
   - ChatGPT's fractional input timing is kept.
4. **`Tests/FidelityTest.cpp`**: all three behaviours are checked end to end through the whole engine, with pass marks. MurmurGPT before this pass fails 7 of its 10 checks. Now all pass. It's registered in CMake and both MurmurGPT workflows.
5. **Workflow:** the Windows preview zip is also pushed to its own `builds-murmurgpt` branch, so it can be fetched with plain git. The official `builds` branches are untouched.

ChatGPT's Env slice change and fractional input timing are kept as they were.

## Not established

- None of this is proof of sounding like the pedals. It removes measured differences and known artefacts. Kevin's ears in Ableton decide.
- The pitch wobble on the demo partials may partly be the demo synth's own chorus, so the new reverb modulation is a judgement, not a match.
- Bad Mood: the demo is far dirtier and more broadband than Murk at its default Glue. It needs a controlled comparison (the same phrase through Murk at a few Glue settings, against the demo's Burst and Soup passages) before changing Glue's curve.
- The demos' setups (mono or stereo, settings) are unknown, so the correlation target is a middle value, not a match.

## Next steps, in order

1. Kevin A/B listens: main against this preview, with the same phrase, in Reverb, Stretch and low Clock.
2. Best evidence for further matching: a short dry phrase plus the pedal's output at noted settings, recorded as WAV. Even one or two would let every mode be compared on the same material.
3. Bad Mood matching: Glue's drive curve and noise character, then Soup's damping, which is currently even across bands, like the demo.
4. Murk Shoegaze and Orchestral flutter: listen first, then decide whether to align grains.
