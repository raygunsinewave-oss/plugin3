# Release test plan

## Automated (every push, see `.github/workflows/build.yml`)
| Check | Platforms |
|---|---|
| Unit tests (DSP, processor, presets, games; 41 tests) | macOS Universal, Windows x64, Linux |
| Same tests under AddressSanitizer + UndefinedBehaviorSanitizer | Linux |
| pluginval, strictness 10 (VST3) | macOS, Windows, Linux |
| `auval -v aumu MyBx Rygn` | macOS |
| Installers built (`.pkg`, Inno Setup `.exe`) | macOS, Windows |

The tests cover:
- Aliasing, pitch, envelopes, voice stealing, and unison level.
- Filter response and stability.
- LFO sync, the mod matrix, voice modes, FM/warp, and granular.
- FX finiteness and the limiter ceiling.
- Arpeggiator and sequencer timing, and LUFS reference levels.
- State round-trip (parameters, assets, presets), with zero heap allocations in `processBlock`.
- The CPU budget.
- All 1,000 presets loading and level-matched.
- All 25 games running in performance mode.

## Manual DAW recall checklist (run before each release)
For each host, do the following:
1. Load the plug-in, then load a factory preset (e.g. *Glass Hallway*).
2. Change an oscillator, a filter, an FX slot type, an MSEG point, a sequencer step, and a
   macro name. MIDI-learn one knob.
3. Automate cutoff and Macro 1.
4. Save the project, quit, reopen it, and check that every change is recalled and automation
   plays back.
5. Duplicate the track and check both instances run independently.
6. Bounce offline at 44.1 / 48 / 96 kHz and compare with real-time playback.

Hosts:

| Host | Formats | Result |
|---|---|---|
| Ableton Live 12 | VST3 / AU | ☐ |
| FL Studio 2025 | VST3 | ☐ |
| Logic Pro 11 | AU | ☐ |
| Bitwig Studio 5 | VST3 | ☐ |
| Cubase 14 | VST3 | ☐ |
| Reaper 7 | VST3 / AU | ☐ |
| Studio One 7 | VST3 / AU | ☐ |

Also check the following:
- HiDPI / Retina at 75%, 100%, and 200% scale.
- Eco animation mode with GPU off.
- MPE with a Seaboard or Linnstrument.
- Sidechain into the Vocoder.
