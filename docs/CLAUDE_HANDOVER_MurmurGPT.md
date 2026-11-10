# Claude handover: MurmurGPT

**Read this document first when Claude credits reset.** This file was prepared in ChatGPT on 2026-11-10 while auditing the project. The user asked for all GPT-led work on an isolated branch, named `MurmurGPT`, with a handover.

## Repo and source of truth
- Private repository: `kevinwkaye/murmur`
- Production baseline: `main`, project version 0.3.0
- GPT experimental branch: `MurmurGPT`
- The plugin is ONE JUCE VST3/standalone binary with Murmur (MOOD MKII inspired) and Murk (BAD MOOD inspired) modes.
- Previous uploaded binary ZIPs and a `v0.1` source archive were reviewed, but the up-to-date GitHub C++ source is authoritative.

## What GPT changed
- `Source/engine/LoopChannel.h`: Env held-slice crossfade now reads the *beginning of the same slice*, rather than a position in the previous slice. On wrap, playback advances to the end of the overlap rather than the beginning; this avoids an immediate second seam. This changes loop cadence slightly, so compare it with baseline by listening and timing.
- `Tests/EnvSliceTest.cpp`: added a basic runtime regression (finite values, signal present, no runaway), not an acoustic similarity test.
- `.github/workflows/murmurgpt-validation.yml`: new workflow scoped to MurmurGPT. It deliberately does not publish artifacts or modify the `builds` branches.
- `docs/MurmurGPT_DSP_AUDIT.md`: source-based audit and roadmap.
- This handover document.

## Critical instructions for resuming work
1. Review **both branches**, starting with `git fetch origin && git switch MurmurGPT && git pull --ff-only origin MurmurGPT`. If the user wants a later main change integrated, use a cautious merge/rebase with backups, not an overwrite.
2. Read `docs/MurmurGPT_DSP_AUDIT.md` and inspect `git diff origin/main...MurmurGPT` plus the GitHub Actions run for `MurmurGPT validation`. Do not assume tests have passed until verified.
3. Review the latest current `main` and branch history before edits. Do not replace GPT changes or silently reset this branch.
4. Keep source, tests and documentation in commits with a clear explanation and reproducible settings.
5. Do not merge into `main`, change existing presets or mapping, or publish new release installers without explicit user authorisation.
6. Never equate clean/dark EQ with high fidelity. Improve SRC, loop seams, granular normalisation and input-dependent behavior from measurements first.
7. Validate at 44.1/48/96 kHz and common buffer sizes, automation changes, clock sweeps, feedback extremes, bypass, state recall and both Murmur/Murk modes.
8. Preserve musical and intentionally lo-fi behaviors. Do not claim exact proprietary algorithm reconstruction.
9. GitHub's existing `.github/workflows/build.yml` force-pushes `builds` and `builds-linux-arm64` branches to publish releases. Prefer the dedicated MurmurGPT test workflow until expressly asked to make an experimental build.
10. Log each change with goal, implementation, evidence, CPU/latency impact, listening observations, CI result and next action.

## Pasteable opening request for Claude Desktop / Cowork

Please resume development of my Murmur/Murk JUCE plugin from GitHub repository `kevinwkaye/murmur`, **branch `MurmurGPT`**, not `main`. Read `docs/CLAUDE_HANDOVER_MurmurGPT.md` and `docs/MurmurGPT_DSP_AUDIT.md` first. Review `git diff main...MurmurGPT` and the dedicated CI status before proposing modifications. ChatGPT discovered the current source and made a narrow Env micro-loop overlap fix plus tests and audit documentation. Preserve those changes, confirm they compile and actually improve the sound, and continue the DSP quality plan in small, reversible, tested commits. Prioritise objective rate-conversion tests and crossfade validation before major DSP changes. Preserve Murmur and Murk as separate characters inside one VST3; never change or merge `main` without my approval. Explain progress in plain English, particularly what could audibly improve and how it was verified.

## If Claude is not authorised to access the repository
Ask the user to connect its GitHub access or clone the repository locally; never infer that Claude has a live copy or that it has read this file from a pasted description alone.
