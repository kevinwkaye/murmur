# Murmur v0.3.0

A stereo VST3 inspired by the way a certain two-channel ambient pedal behaves: an always-listening micro-looper, a Wet Channel of spatial effects, and a Clock that resamples everything in musical steps. Since v0.3 it also has **Murk**, a second mode inspired by that pedal's grumpier sibling. All DSP here is original code written from public descriptions of the pedals' behaviour.

## What's new in v0.3.0

### Murk mode

Click the mode card next to the title, or hold footswitches B and C for a second. The panel stays the same shape, the controls take Murk's meanings and the cards relabel:

| Murk | What it does | Time or Length | Modify |
| --- | --- | --- | --- |
| SOUP | Spectral reverb: your playing re-synthesised across 256 narrow bands | Decay, 0.4 s to 20 s | Natural to synthetic |
| RELAY | A delay that doesn't fade: every repeat at the same level | Delay time | 1 to 16 repeats, endless at the top |
| FLIP | Chordcaster: up to four pitch-shifted voices, each entering one lag later | Lag between the notes | 48 chords of 4ths, 5ths and octaves, up on the right, down on the left |
| BURST | The loop as a pattern of up to 8 steps, sliced at its own hits. Loud playing scrambles it into fills | Step speed and size | Chaos: fully down never fills |
| RADIO | Five stations play the loop: Tape, Ambient, Orchestral, Shoegaze, Dance. Between stations, light shortwave interference | Each station's own control | Tunes between stations |
| MASK | Wherever the loop is loud, it switches to a transformed copy | Sweeps five characters: octave down, reverse, crush, ring, stutter | Threshold: fully down is the plain loop |

- **Glue** sits after both channels: warm and binding low down, thrashed at the top. **Dry Glue** puts the dry signal through it too.
- **Cross** lets your playing (or one channel) make the other dip and wobble, from gentle movement to stutters and dropouts.
- **EQ tilt** replaces Tone, on the wet, the loop or both. **Blend** is Direct loop, and **Volume** sits on the Deep page.
- Each mode keeps its own settings. Presets A and B, and Ableton's own saved sets, store the mode and both modes' settings.
- Pebble gets grumpy in Murk: a murkier blue, heavy brows and a flat frown.

### Also new

- **Footswitches and expression**, with no setup: see below.
- **Loop record.** The LOOP button and footswitch B now record a fresh loop: tap to record, tap to play, tap to stop. Hold to overdub. Tempo synced, the loop rounds to whole beats. A+B together still loops what Murmur has just heard, like the original pedal.
- **Keeper.** Click KEEP (or hold C+D) to log this moment with the last 8 seconds of audio. Right-click KEEP for notes and Export.
- **Original pedal MIDI map** now answers only on its own channel, off by default, so it can't collide with the footswitch defaults.

## What was new in v0.2.1

- **Tempo display follows Sync.** The tempo card now always shows the tempo Murmur is running at. It changes the moment you switch Sync on or off, follows Ableton's tempo, and is right when a set loads with Sync on.
- **Slip and Tape pitch match the MOOD MKII.** Slip's Modify runs reverse on the left half and forward on the right, each with every semitone from half speed (-12) to double speed (+12). Tape's Modify has the eight octave steps. A flip card under Modify shows the step, for example FWD +5TH +7.

## What's in v0.2

| Area | Included |
| --- | --- |
| Clock | 2 kHz to 64 kHz engine rate. Default steps copy the pedal's 11 stops (64, 48, 32, 24, 16, 12, 8, 6, 4, 3, 2 kHz). Octave, fourths and fifths, semitone and Smooth options |
| Wet Channel | Reverb (multi-tap cluster smeared into a reverb), Delay (clean time jumps, ping-pong that mirrors your panning), Slip (chunk delay: echoes play back pitched or reversed, -12 to +12 semitones either way), Freeze for each |
| Micro-Looper | Always listening, Env, Tape and Stretch (the MKII's stepped speeds), overdub, fade, Refresh (No Dub) |
| Interaction | Routing (In, Both, Loop), the looper records the Wet Channel while listening, Direct micro-loop blend, Channel Sync both ways |
| Footswitches | A: tap for wet, hold to freeze. B: tap to record, play and stop a loop, hold to overdub. Latch makes hold a toggle |
| Motion | Ramp or bounce any of Time, Wet Modify, Clock, Loop Modify and Length, with the five morphing waveforms, Top or Bottom sweep, or drive them from an expression value |
| Tempo | Sync Delay and Slip time to note values and the looper to one bar, from Ableton's tempo or tap tempo |
| Synth Mode | MIDI notes play the Clock in semitones. Drone (the pedal's default), Gate or ADSR, glide, pitch bend, and a movable original-pitch note. Move Clock to leave |
| MIDI | The pedal's MIDI CC map on its own channel (Pedal page, off by default), and program change 0, 1 and 2 for Live, B and A |
| Presets | A / LIVE / B toggle like the pedal. Hold A or B to save into it |
| Character | Classic mode: clock noise, deteriorating loops, ringing reverb, aliased Slip, bendy Delay, chopping Tape, wispy Stretch |
| Look | Flip Fella style: flip cards, Karlsson clock dial, and Pebble, who reacts to what the looper is doing |

To play Synth Mode in Ableton, put Murmur on an audio track, then set a MIDI track's "MIDI To" to that track and choose Murmur.

## Footswitches and expression (optional)

Everything works from the screen, so you don't need any pedals. If you have a MIDI footswitch:

1. Set its first four switches to send **momentary CC 20, 21, 22 and 23 on MIDI channel 16** (127 when pressed, 0 when released). Or use **Learn** on the Pedal page to use whatever yours sends.
2. Set an expression pedal input to **CC 11 on channel 16**, or press Learn under **EXPRESSION MOVES** on the Pedal page.
3. Press **CALIBRATE** on the Pedal page, then rock the pedal from heel to toe once.

These settings are shared by every Flip Fella plugin on your computer, so you set them up once.

Every footswitch does the same job in both modes; only the sound changes. Press **SHOW ACTIONS** on the Pedal page for this table in plain words.

| Footswitch | Murmur | Murk | Needs |
| --- | --- | --- | --- |
| A tap | Wet channel on or off | Same | One press |
| A hold | Freeze (latches with Latch) | Same | Hold 0.45 s |
| B tap | Loop: record, then play, then stop | Same | **One press per step: three to record, play and stop** |
| B hold | Overdub (latches with Latch) | Same | Hold 0.45 s |
| C tap | Tap tempo | Same | **Two or more taps to set a tempo** |
| C hold | Save to the current preset slot (A or B) | Same | Hold 0.45 s |
| D tap | Next preset slot (A, Live, B) | Same | One press |
| D hold | Previous preset slot | Same | Hold 0.45 s |
| A+B tap | Both channels on or off | Same | **Two switches together** |
| B+C hold | Switch to Murk | Switch to Murmur | **Two switches, held 1 s** |
| C+D tap | Hand the footswitches to the next Murmur | Same | **Two switches together** |
| C+D hold | Keeper | Same | **Two switches, held 1 s** |

Nothing needs a double-tap, and nothing needs a hold of more than one second. The timings match the other Flip Fella plugins: hold 0.45 s, combo window 0.12 s, combo hold 1 s.

The expression pedal moves **Clock** in Murmur, so heel to toe walks down the 11 stops, and **Glue** in Murk, from warm to wrecked. Pick anything else under **EXPRESSION MOVES**; the choice is saved with each mode and each preset. "Ramp targets" gives the original pedal's behaviour, sweeping whichever knobs the Motion page's ramp switches pick.

### Getting footswitch MIDI to Murmur in Ableton

Murmur reads your footswitch straight from its USB cable, so every copy you load works with no MIDI track. The dot on the Pedal page turns green when it's connected, and Pebble says FOOTSWITCHES READY.

1. In Ableton's Preferences, Link Tempo MIDI, turn **Track**, **Sync** and **Remote** off for your footswitch's input. Windows lets only one program use a MIDI input at a time, so Ableton has to let go of it.
2. Load Murmur. That's it.

If the dot stays grey, press **READ FROM** on the Pedal page. The menu says what's wrong, and you can pick your footswitch from the list if it wasn't found automatically.

With several copies loaded, the footswitches drive the one you opened or clicked last. Tap C+D to hand them to the next copy.

Prefer routing through Ableton? Choose **Only from a MIDI track in Ableton** under READ FROM, then turn **Track** back on for the footswitch, make a MIDI track with **MIDI From** set to the footswitch and **MIDI To** set to Murmur, and arm it. The copy that track feeds takes the footswitches automatically.

One limit for now: Windows lets only one plugin file hold the footswitch's input, so if Prism Patch already has it, Murmur needs the Ableton MIDI track route above.

### Kevin's setup (M-Vave Chocolate Plus and M-Audio EX-P)

In CubeSuite: footswitches A to D as momentary CC 20 to 23, the pedal jack set to expression input on CC 11. It always sends on channel 1 whatever CubeSuite says; read directly from its cable, Murmur accepts any channel. Put the Chocolate Plus switch on **U** and connect with the USB cable, not Bluetooth. The EX-P needs a 3.5 mm to 6.35 mm TRS adapter and reads backwards on the Chocolate Plus, which Calibrate fixes.

## Original pedal MIDI map

The original pedal's MIDI CC chart still works, on a channel you choose under **ORIGINAL PEDAL MIDI MAP** on the Pedal page. It's off by default, because its CC 11 and CC 20 to 23 would otherwise clash with the expression and footswitch defaults. In Murk, the mode CCs pick Murk's modes, CC 27 moves the EQ tilt, CC 71 is Dry Glue and CC 77 is Half.

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
2. Click **LOOP** and play a phrase, then click **LOOP** again to hear it back.
3. Turn **CLOCK** down a few steps and listen to the loop drop in fourths and fifths.
4. Click **WET**, pick Reverb and raise **MODIFY** to smear the loop.
5. Set Routing to **Loop** so your live playing stays clean over the top.
6. Hold **LOOP** to add layers, or open the **Deep** page and switch on **Latch** to tap it instead.
7. Click the **MURMUR** card to switch to **Murk**, and turn up **GLUE**.

## Project layout

| Path | What it holds |
| --- | --- |
| `Source/engine/` | The DSP engine, plain C++17 with no JUCE dependency. `MurkWet.h`, `MurkLoop.h` and `MurkFx.h` hold Murk |
| `Source/PedalControl.*`, `Source/Keeper.*` | Foot control and Keeper, copied from Prism Patch (Murmur adds the B+C combo) |
| `Source/Params.h` | Every parameter, its range and default |
| `Source/PluginProcessor.*` | Host wrapper, state saving |
| `Source/PluginEditor.*` | The Panel, Deep, Motion and Pedal pages |
| `Tests/RenderTest.cpp` | Offline engine test, both modes |
| `Tests/PluginTest.cpp` | Processor and editor tests: tempo, pitch, footswitches, expression, modes, presets |
| `Tests/Benchmark.cpp` | CPU per copy at 48 kHz, both modes |
| `.github/workflows/build.yml` | Cloud build for Windows and macOS |
