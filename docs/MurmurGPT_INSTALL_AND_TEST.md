# MurmurGPT installation, listening and regression plan

## What is being shipped
A private experimental Windows VST3 plus standalone JUCE application built from the `MurmurGPT` branch. The project is one plugin with two modes: Murmur and Murk. The preview keeps the existing plugin ID for host/preset compatibility, which means **it can replace the official v0.3.0 installation**.

The Windows build/publishing workflow is `.github/workflows/murmurgpt-windows.yml`. It creates a GitHub prerelease only after the Windows build, DSP tests, integration tests and CPU benchmark have completed successfully. The workflow runs on source/test changes to this branch, and does not overwrite the official `builds` branches.

## Install (Windows)
1. Download `MurmurGPT-Windows.zip` from the latest `MurmurGPT Windows preview` prerelease in the private GitHub repository.
2. Extract the ZIP. It includes `Murmur.vst3`, `MurmurGPT-standalone.exe` and `reports`.
3. **Back up** the current VST3 folder and save copies of important Ableton projects/presets before testing. Stop your DAW.
4. Copy the new `Murmur.vst3` folder to `C:\Program Files\Common Files\VST3\`, replacing the existing copy only after backup.
5. Rescan VST3 plugins in the host and confirm audio processing and the two Murmur/Murk mode selectors. Do not expect a second simultaneous plugin instance under a different name.
6. To revert, close the DAW and restore the backed-up `Murmur.vst3` folder.

## Acceptance checklist
- All compile and offline tests succeed on Windows.
- Host loads VST3 with no crash. Reload a saved pre-preview project; its parameters and presets should be retained.
- Both modes produce sound in wet-only, mixed and dry-bypass settings.
- At 44.1, 48, and 96 kHz, try 64, 128, 256, 512 and 1024 sample buffers.
- Record the same reamped/looped **dry** guitar performance through both the official v0.3.0 and the preview, with identical settings and matched output loudness.
- In Murmur, test Env hold on alternating loud/soft phrases and compare loop-seam clicks/level dips. Verify Envelope releases sensibly and does not latch unwanted slices.
- Test Delay, Slip, Tape and Stretch at high and low clocks; listen for buzz, pitch drift, brightness changes, and unintended pulses.
- Toggle Classic and make sure its rougher/lo-fi sound is still intentional.
- In Murk, test SOUP, RELAY, FLIP, BURST, RADIO, MASK, Glue and Cross, especially with high feedback.
- Compare CPU use, tails, preset recall, automation and mode switching to v0.3.0.
- Inspect `reports/clock-quality.txt`, `reports/env-slice.txt`, `reports/render-test.txt`, `reports/plugin-test.txt` and `reports/cpu-windows.txt` from the build ZIP.

## Controlled A/B with hardware
Any claim of fidelity to MOOD MKII/BAD MOOD requires lossless input and hardware-output WAV files at matched settings. Use level-matched blind listening and analyse frequency response, sidebands/aliasing, loop timing and transients. Do **not** interpret software test-pass status as sonic similarity.

## Known limitations of this preview
- This is not yet a band-limited polyphase resampler. Four-pole filtering and fractional host-input timing are measurable algorithmic improvements, but host/core conversion and coefficient smoothing may still introduce artefacts.
- A one-off Env overlap correction changes repeated slice duration slightly. Listen to its timing and compare against the stable build.
- No signed Windows installer is included. Standard Windows VST3 install requires manually copying a directory.
- Experimental software: retain the official v0.3.0 backup.

## For Claude
Read `docs/CLAUDE_HANDOVER_MurmurGPT.md` and `docs/MurmurGPT_DSP_AUDIT.md` before further DSP edits. Keep all work on `MurmurGPT`, use small commits, leave `main` and the official download branches untouched, and do not call the DSP a verified clone of proprietary hardware.
