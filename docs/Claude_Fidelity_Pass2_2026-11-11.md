# Claude fidelity pass 2, 11 November 2026

Branch: `MurmurGPT`, on top of pass 1 (`58ef17f`, Windows preview 2). `main` is untouched.

Kevin's brief was unchanged: the functions are right, but it doesn't sound much like the pedals. Pass 2 went back to his two YouTube demos with better tools and found that one of pass 1's targets was wrong.

## What was new in the analysis

- **A rough transcript.** The Whisper model download was blocked, but `pocketsphinx` ships its English model inside the pip package. The transcripts are noisy but give the demo timelines:
  - MOOD MKII demo:
    - dry guitar at about 1:04 to 1:12
    - reverb about 1:13 to 1:30
    - delay (ping-pong) about 1:45 to 1:53
    - Slip about 1:53 to 2:11
    - looper from 2:30
    - Clock about 4:15
  - Bad Mood demo:
    - Radio about 1:45
    - Clock 2:15
    - Soup 3:00
    - Relay 5:00
    - Flip 5:45
    - direct comparisons with the original MOOD at about 7:00 to 8:45 (Slip against Flip, Delay against Relay, Reverb against Soup)
    - Glue about 9:15
- **The demo's own dry guitar as the plugin's input.** `dry_guitar.wav` (MKII demo, 64.2 to 71.75 s, mono, 44.1 kHz) was rendered through each version's engine, then measured exactly as the pedal's passages were.
- **Clean tails only.** The pedal was measured only after each note had stopped (for example 74.9 to 75.65, 76.6 to 77.95 and 81.2 to 83.65 s in the MKII demo; 502.0 to 503.15 s in the Bad Mood demo). Pass 1's automatic tail finder also took segments that still held the mono dry guitar. That made the pedal look far more correlated left to right than it is.

## Measurements

Levels are at the start of a clean tail, relative to the 400 to 1200 Hz band. Decay is per band. Correlation is between left and right.

| | 1.2 to 3 kHz | 3 to 7 kHz | Decay across bands | Correlation |
| --- | --- | --- | --- | --- |
| MOOD MKII demo reverb | -14 to -29 dB | -31 to -41 dB | about even, 12 to 20 s | 0.1 to 0.3 |
| Original MOOD in the Bad Mood demo | -32 to -38 dB | -38 to -47 dB | | 0.5 to 0.9 (short gaps) |
| The notes going in (both demos) | -4 to -9 dB | -4 to -15 dB | | 1.0 (mono) |
| v0.3.0 (demo guitar in) | -4 to -6 dB | -10 to -12 dB | even | 0.0 to 0.2 |
| Preview 2 | -9 to -11 dB | -16 to -17 dB | treble 3 to 4 times faster | 0.6 |
| **Pass 2** | **-14 to -16 dB** | **-33 to -37 dB** | **even** | **0.19 to 0.34** |

The pedal's reverb is dark from the first moment, like a 12 dB per octave low-pass near 1.2 kHz, then decays about evenly. Preview 2 got darker only over time, and was too narrow.

Other findings:

- **Delay** echoes came back about 7 to 12 dB softer above 2 kHz than the notes they repeated. The evidence is noisier, so the change is gentle.
- **Bad Mood Soup** passages measured -45 to -52 dB spectral flatness (clearly tonal) and correlation about -0.1 (wide).
  - They ran about 10 dB brighter than the original MOOD's reverb up to 3 kHz, but were just as dark above it.
  - Preview 2's Soup measured -21 dB (noise-like). A held 220 Hz note came out 254 cents off at -5 dB purity.
- **Glue:** crest factor was 10 to 13 dB in both pedals' passages on the same instrument. That's no evidence that Bad Mood's default Glue compresses harder, so Glue is unchanged.
- **Slip:** the plugin is in the same tonal range as the demo (-44 to -52 against -42 dB flatness). Unchanged.
- **Flip:** the plugin is 3 to 6 dB noisier than Bad Mood's Flip (-34 to -36 against -39 to -43 dB). This comes from the pitch shifter's crossfades. Left for the next pass.

## Changes (one commit each, each revertable)

1. **`3e53acc` Reverb and delay voicing** (`WetChannel.h`).
   - Reverb: a two-pole Butterworth low-pass at 1.2 kHz, `kHighDecay` 0.25 to 0.7, crossfeed 0.33 to 0.12.
   - Delay: a two-pole low-pass at 3 kHz on its output.
   - Both cutoffs are fixed in hertz and capped at 0.4 of the clock. Scaling them with the clock would put the reverb at about 75 Hz at low Clock settings. Low clocks still darken through their own band limit, as before.
   - Classic keeps the old voicing.
2. **`5900533` Soup** (`MurkWet.h`).
   - A phase vocoder: each band learns its true frequency while fed and keeps it as it decays.
   - Identity phase locking round each spectral peak.
   - Drift reduced to 0.015 pi per frame at the natural end, rising towards synthetic.
   - Random left/right starting phases, so the wash stays wide.
   - A four-pole low-pass at 2.6 kHz.
   - Held 220 Hz: -0.8 cents, 19 to 20 dB pure, each side within 0.02 dB of a smooth decay. Folded to mono it moves ±4 dB as the sides drift, as any wide reverb does.
3. **`3594c0a` Fidelity test.**
   - Reverb voicing (4 kHz 15 to 40 dB under 500 Hz), even decay (0.5 to 1.3) and correlation 0.1 to 0.45 replace pass 1's marks.
   - New Soup checks: pitch, purity and steadiness.
4. **`867f444` Stretch alignment CPU.** Pass 1's search had tripled Murmur's worst case (about 5% to 14 to 16%). A three-stage search with integer-indexed reads brings it to 6.8 to 8.9%, with identical Stretch results.

## Not established

- These changes match measurable traits of two YouTube demos (128 kbps MP3, unknown settings and recording chains). Settings such as Time and Modify in the demos are unknown, so absolute decay times aren't matched, only the shape.
- The second demo's original MOOD reverb is darker still than the first's. It may have been at a lower Clock, or this is the pedal's own range.
- Nothing here is a listening verdict. Kevin's A/B in Ableton decides.

## Next, in order

1. Kevin listens: preview 3 against v0.3.0, with the same phrase, in Reverb, Delay, Stretch, Soup and low Clock.
2. Flip: align the pitch shifter's read heads by correlation at each crossfade, as Stretch does now.
3. Best evidence for further matching: a short dry phrase plus the pedal's output at noted settings, as WAV.
4. Glue's character at high settings against the demo's 9:15 to 10:00 passage, if Kevin finds Murk too clean when pushed.

## Tools

The scripts used are reproducible from this description: `tailstats.py` (per-band decay, level against low mids, correlation), `crest.py` (crest factor and spectral flatness) and `ref.cpp` (renders a WAV through any checkout's engine with key=value settings). They lived in the session's scratch space and aren't committed.
