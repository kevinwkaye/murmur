# Murmur v0.1

A stereo VST3 inspired by the way a certain two-channel ambient pedal behaves: an always-listening micro-looper, a Wet Channel of spatial effects, and a Clock that resamples everything in musical steps. All DSP here is original code written from public descriptions of the pedal's behaviour.

## What's in v0.1

| Area | Included |
| --- | --- |
| Clock | 2 kHz to 64 kHz engine rate, stepped in octaves, fourths and fifths, or semitones, plus Smooth |
| Wet Channel | Reverb (multi-tap cluster smeared into a reverb), Delay (clean time jumps), Slip (auto-sampler), Freeze for each |
| Micro-Looper | Always listening, Env, Tape and Stretch modes, overdub, fade, Refresh (No Dub) |
| Interaction | Routing (In, Both, Loop), the looper records the Wet Channel while listening, Direct micro-loop blend, Channel Sync both ways |
| Stereo | Spread per mode, MISO, Stereo Width, Spread Solo |
| Character | Classic mode: clock noise, deteriorating loops, ringing reverb, aliased Slip, bendy Delay, chopping Tape, wispy Stretch |
| Extras | Input and output gain, Max Buffer length (1 to 32 s at 64 kHz), Level Balance, Tone, Trails, Dry Kill, Latch |

Coming next (from the spec): ramp and bounce modulation, LFOs, envelope follower, macro, internal clock and tap tempo, step sequencer, Synth Mode, MIDI learn, presets with A/B morph, loop export, and saving the loop buffer with your set.

## Getting a Windows VST3 (no compiler needed)

The easiest route is GitHub Actions, which builds it for you in the cloud.

1. Create a new private repository on GitHub and upload this whole folder (including the hidden `.github` folder).
2. Open the repository's **Actions** tab. The "Build Murmur" workflow runs on every push, or press **Run workflow**.
3. When it finishes (about 10 to 15 minutes), download the **Murmur-Windows** artefact from the run page and unzip it.
4. Copy `Murmur.vst3` into `C:\Program Files\Common Files\VST3\`.
5. In Ableton Live, open Preferences, then Plug-Ins, make sure **Use VST3 Plug-In System Folders** is on, and press **Rescan**.

The same run also builds a macOS version (VST3 and AU) and a standalone app for quick testing outside Ableton.

## Building it yourself on Windows

You need Visual Studio 2022 (the free Community edition, with "Desktop development with C++") and CMake 3.22 or newer.

```
cmake -S . -B build
cmake --build build --config Release
```

The plugin lands in `build\Murmur_artefacts\Release\VST3\Murmur.vst3`. JUCE downloads automatically on the first configure.

## Testing the engine

`Tests/RenderTest.cpp` runs the engine with no plugin host. It plays a short phrase through 37 scenarios (every mode, freeze, overdub, refresh, routing, clock moves, spread and Classic), then checks for NaNs, runaway levels and clicks. Pass a folder to write a WAV for each scenario:

```
build\Release\murmur_render_test.exe C:\Temp\murmur-wavs
```

## Quick start in Ableton

1. Drop Murmur on an audio track with a guitar, synth or loop.
2. Play a few notes, then click **LOOP**. You'll hear what it just heard.
3. Turn **CLOCK** down a few steps and listen to the loop drop in fourths and fifths.
4. Click **WET**, pick Reverb and raise **MODIFY** to smear the loop.
5. Set Routing to **Loop** so your live playing stays clean over the top.
6. Hold **DUB** to add layers, or open the **Deep** page and switch on **Latch** to tap it instead.

## Project layout

| Path | What it holds |
| --- | --- |
| `Source/engine/` | The DSP engine, plain C++17 with no JUCE dependency |
| `Source/Params.h` | Every parameter, its range and default |
| `Source/PluginProcessor.*` | Host wrapper, state saving |
| `Source/PluginEditor.*` | The Panel and Deep pages |
| `Tests/RenderTest.cpp` | Offline engine test |
| `.github/workflows/build.yml` | Cloud build for Windows and macOS |
