# Roadmap

## Phase 1: scaffold, parameters, oscillator, audio (DONE)

- CMake + JUCE 8 project producing VST3 / AU (macOS) / Standalone; CI for macOS Universal, Windows x64 and Linux.
- APVTS parameter system: 120 automatable parameters (6 oscillators x 18, amp envelope, global), undo/redo,
  versioned state save and recall.
- Oscillator types:
  - Analog: sine, triangle (polyBLAMP), saw (polyBLEP), square, and pulse with click-free PWM.
  - Wavetable: 6 procedurally generated factory tables, 64 frames each, 11 band-limited mip levels.
  - Supersaw.
  - Noise: white, pink, brown, digital, vinyl.
- Per oscillator: octave 32'–2', coarse, fine, level, pan, phase, phase randomisation, unison 1–16 with
  detune, blend and stereo spread, WT position, pulse width.
- Voice engine: up to 32 voices, sample-accurate MIDI, a ~3 ms de-click fade on voice steal, sustain pedal,
  pitch bend with range control, and parameter smoothing (no zipper noise).
- Amp envelope (ENV 1): delay / attack / hold / decay / sustain / release, per-stage curves, velocity, punch.
- Oversampling at 1x/2x/4x/8x with polyphase IIR filters, reported to the host as latency.
- UI: cyberpunk theme; scalable 75–200%; top-bar tabs; oscillator editor with a 3D waterfall / 2D / spectrum
  view; envelope display; output scope; CPU and voice meters.
- Controls: live-value tooltips, double-click reset, Ctrl/Cmd fine drag, and a right-click menu with
  reset, randomize, and the host automation menu.
- Verified: 18 unit tests pass and pluginval strictness 10 passes (Linux VST3). A 6-osc, 8-voice patch at
  48 kHz with 2x oversampling uses ~3.8% of one core.

Placeholders: FM currently renders its carrier only, because FM routing arrives in Phase 2. Granular is
silent until sample loading lands in Phase 2.

## Phase 2: full engine
Two filters with serial, parallel and split routing:
- Types: LP/HP/BP/notch at 12 and 24 dB, ladder, SVF, diode, comb, formant, phaser, acid.
- Self-oscillating resonance, drive, key tracking, filter FM, and a live response curve.
- Per-oscillator routing to Filter 1, Filter 2, both, or bypass.

Oscillators:
- FM operator matrix (any oscillator can modulate any other).
- Warp modes.
- Granular engine.
- Sub oscillator, noise layer, and ring mod.
- Wavetable scan (speed / start / mid / end / loop / sync).
- WAV wavetable import.

Modulation:
- ENV 2–4 with loop mode, plus a drawable MSEG.
- 6 LFOs.
- 32-slot mod matrix and 8 macros.
- Modulation-depth arcs on knobs.

Voices: mono, legato and unison modes, portamento, and MPE.

## Phase 3: FX, arp/seq, metering
- FX rack: 10 reorderable slots, the full effects list, and send buses A/B.
- Arpeggiator, plus a 16–64 step sequencer with modulation lanes.
- Meters: peak, RMS and LUFS, true-peak, and peak hold.
- Visualisers: spectrum analyser, goniometer and correlation meter.

## Phase 4: full UI
- Complete UI layout for every tab, with embedded OFL display and mono fonts.
- GPU rendering: OpenGL on Windows, Metal-backed on macOS.
- Drag-and-drop modulation and MIDI Learn.
- Settings: themes (including a colorblind-safe palette), Eco mode, and keyboard navigation.

## Phase 5: presets
- Preset browser with search, tags, favourites, ratings, recently used, random, and audio preview.
- JSON preset format.
- A generator script plus hand-designed hero presets, building to 1,000 factory presets, level-matched to -6 dBFS.

## Phase 6: XD games
- Game engine (tempo sync, scale lock, quantize, mod outputs, high scores, performance mode).
- 25 original games.

## Phase 7: ship
- Profiling and SIMD.
- DAW recall testing in Live, FL Studio, Logic, Bitwig, Cubase, Reaper and Studio One.
- Installers: Inno Setup .exe for Windows; signed and notarized .pkg for macOS. The macOS installer needs
  your Apple Developer ID.
- User manual.
