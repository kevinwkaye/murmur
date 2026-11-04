# Murmur foot control

First written 4 November 2026 from a cross-plugin audit. Built in v0.3.0 (4 November 2026), together with Murk mode.

## What changed in v0.3.0

- **Shared module.** `Source/PedalControl.*` and `Source/Keeper.*` are copied from kevinwkaye/prism--patch, including `DirectMidiInput`, so every copy of Murmur hears the footswitch with no setup and no MIDI track.
- **B+C combo.** `FootswitchLogic` gains `ComboBC` and `ComboHoldBC` for the two middle switches, with the same timings as A+B and C+D. Murmur uses the hold to switch mode. Other plugins can copy the updated module; nothing they do changes, because the new events only fire for B+C.
- **MIDI routing.** Every message goes to the pedal code first. A footswitch, expression or Keeper message on its learned channel and CC is consumed there. Notes, pitch bend and program changes carry on as before.
- **Original pedal MIDI map.** The MOOD CC chart only answers on the channel picked under ORIGINAL PEDAL MIDI MAP on the Pedal page, off by default. In Murk the mode CCs pick Murk's modes.
- **Pedal page.** Four footswitch flip cards (letter, what it reads now, tap job, hold job, what it listens for, Learn), the two-switch combos with TRY buttons, the expression panel (target, heel to toe, Calibrate, Learn, curve), footswitch input, the original map channel and Keeper (Live or Studio, Export, Learn a Keeper switch). MURMUR and MURK pills show each mode's table. SHOW ACTIONS opens the plain-words overlay.
- **On-screen buttons.** WET and LOOP on the panel, and the letter cards on the Pedal page, press the footswitches exactly as the real ones do. The mode card, KEEP, the preset toggle and TAP TEMPO cover the rest.
- **Pebble** taps its foot on a press, leans with the expression pedal, nods after Keeper, scowls and flips into Murk and shakes with relief coming back.

## Footswitch table (both modes)

Every job is the same in Murmur and Murk, so there are no remaps. Bad Mood's own footswitches do the same jobs as MOOD MKII's, apart from extras that don't need a footswitch in a plugin (hidden options are the Deep page, hold-and-turn volume is a dial, triple-tap true bypass doesn't apply).

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
| A+B tap | Both channels on or off (on loops what it just heard) | Same | **Two switches together** |
| B+C hold | Switch to Murk | Switch to Murmur | **Two switches, held 1 s** |
| C+D tap | Hand the footswitches to the next Murmur | Same | **Two switches together** |
| C+D hold | Keeper | Same | **Two switches, held 1 s** |

Flagged in bold: anything needing more than one press, or two switches. Nothing needs a double-tap and nothing needs a hold of more than one second.

Changed from the first plan, at Kevin's request: B is loop record (tap to record, play, stop) rather than the MOOD's instant "loop what you just heard". A+B keeps the instant capture.

Timings, shared with every Flip Fella plugin: hold 0.45 s, combo window 0.12 s, combo hold 1 s.

## Expression

Assignable per mode and per preset (the Expression Target parameter is part of each mode's settings and each preset). Calibration, curve and the CC are global and shared by every Flip Fella plugin.

| Mode | Default target | Why |
| --- | --- | --- |
| Murmur | Clock | Heel to toe walks down the 11 stops, the pitch drop the demo loved |
| Murk | Glue | Warm at the heel, wrecked at the toe |

"Ramp targets" keeps the original pedal's behaviour: moving the pedal sets Driven By to Expression and sweeps whichever knobs the Motion page's ramp switches pick.

## Modes, presets and Keeper

- Switching mode stores the outgoing mode's settings and restores the incoming mode's (its defaults the first time). Channel on and off states, tempo sync and input level stay put. The loop buffer carries over.
- Moving the Mode parameter from automation does the same swap.
- Presets A and B, the Live slot and Ableton's saved sets store the mode and both modes' settings.
- Keeper entries record the mode, what the panel reads in words, the expression target and position, and every parameter.

## Tests

`Tests/PluginTest.cpp` covers tap, hold, A+B, B+C and C+D from plain 127 and 0 messages; B+C tapped or held 0.6 s never switching mode; the clash audit (CC 20 to 23 and CC 11 on channel 16 change only their own jobs, with the original map off and on channel 1); Learn; expression defaults per mode, calibration of a reversed short-range pedal and per-mode memory; mode stashes; presets and saved sets keeping the mode.

## Open question

Two different plugins (separate DLLs) can't both open the footswitch device on Windows. When Murmur and Prism Patch are both loaded, the one that opens second needs the Ableton MIDI track route until there's a shared way to pass the footswitches between plugins.
