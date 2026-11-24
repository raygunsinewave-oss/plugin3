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

## Phase 2: full engine (DONE)

Engine:
- Two filters with serial, parallel and split (F1 left / F2 right) routing. 15 types: SVF LP/HP/BP/Notch
  12 and 24 dB, ZDF ladder, diode, comb +/-, formant (vowel sweep), phaser, and an 18 dB acid filter with a
  feedback high-pass. Each has self-oscillating resonance, drive, ENV 2 amount, keytrack, velocity, audio-rate
  FM from OSC 1, mix, and a live response curve measured with the real DSP.
- Per-oscillator routing: Filter 1, Filter 2, both, or bypass.
- Warp modes: bend +/-, sync, mirror, PWM, formant, quantize, bit crush, FM, AM, ring. FM/AM/ring take any
  oscillator as the source, including itself (feedback FM). Render order is resolved automatically so
  modulators run first.
- Wavetable scan: start/mid/end, one-shot/loop/ping-pong, Hz or tempo sync.
- WAV/AIFF/FLAC wavetable import (2048-sample frames detected) and a single-cycle wavetable drawer.
- Granular oscillator: size, density, spray, width; position via WT POS, scan, or modulation.
- Sub oscillator, noise layer and ring modulator, each with its own routing.
- Envelopes 2–4 (DAHDSR, curves, velocity, loop) plus an MSEG with a drawable breakpoint editor, sync and loop.
- 6 LFOs: 8 shapes including a drawable custom shape; free or sync (1/64 to 8 bars, triplet/dotted);
  retrigger, free (follows the host transport) or envelope mode; phase, fade, smooth, bipolar/unipolar.
- 32-slot mod matrix (source, via, amount, curve, destination, bypass), with 28 sources and 76 destinations.
  8 nameable macros.
- Voice modes: poly, mono and legato (last-note priority) with glide (always or legato-only, linear to
  exponential curve), plus a unison voice mode stacking up to 8 voices per note.
- MIDI: MPE (per-note bend, pressure and slide), poly and channel aftertouch, and the mod wheel.
- State: 554 parameters, plus drawn shapes, macro names, and imported/drawn wavetables and samples. All are
  saved with the project.

UI:
- More technical HUD styling: tick-ring knobs that show modulation depth (magenta arc) and the live modulated
  value (lime dot), section tags with hex codes, a glitch logo, a top-bar sweep with mini scope, output meter
  and CPU readout, a live status bar, and a circuit-trace background.
- Right-click any modulatable knob and choose "Add modulation slot".

Verified:
- 31 unit tests pass.
- pluginval strictness 10 passes (Linux VST3).
- `processBlock` stays allocation-free with every Phase 2 feature enabled.
- CPU: 6 oscillators × 8 voices at 48 kHz with 2x oversampling and Filter 1 on is about 4.3% of one core
  (best of 5 runs). This is a shared cloud VM, so single runs occasionally read up to ~7%.

Deferred to Phase 4 (UI pass): drag-and-drop modulation from source icons, MIDI Learn, and the MPE zone editor.
Granular uses a file path, so a moved sample must be re-located; embedding samples comes with the preset system.

## Phase 3: FX, arp/seq, metering (DONE)
- 19 effects in 10 reorderable slots, plus send buses A/B; master 6-band EQ with analyser; output limiter.
- Arpeggiator (6 patterns) and a 1–64 step sequencer (pitch, velocity, gate, probability, ratchet, slide,
  2 mod lanes).
- Meters: peak, RMS, LUFS (M/S), true peak, clip; goniometer and correlation; slim always-on meters;
  sidechain input meter.

## Phase 4: full UI (DONE)
- Themes: Neon Cyan, Magenta Night, Acid Green, Mono, Colorblind Safe.
- Embedded OFL fonts; optional OpenGL; UI scale 75–200%; Eco animation mode.
- ADVANCED tab: drift, per-voice random, steal modes, mod smoothing, velocity/keytrack curves, DC blocker,
  saturation models, Scala microtuning, searchable parameter browser with MIDI CC learn.
- Drag-and-drop modulation chips, keyboard navigation, computer-keyboard playing.

## Phase 5: presets (DONE)
- JSON preset format, a browser (search, tags, favourites, ratings, recents, preview, A/B, import/export).
- 1,000 generated, level-matched factory presets with meaningful macros and style tags (no artist names).

## Phase 6: XD games (DONE)
- Framework: scale/key lock, quantize, tempo sync, sound target, 4 Mod Out sources, high scores.
- 25 original games, each with a performance-mode AI.

## Phase 7: ship (DONE)
- Profiling and optimisation: ~4.3% of one core for 6 osc × 8 voices at 48 kHz / 2×.
- Sanitizer CI; pluginval strictness 10 on all platforms; auval.
- Installers (`.pkg`, Inno Setup); optional signing and notarization; tagged GitHub Releases.
- User manual and release test plan.

Still requires you or your hardware:
- Apple Developer ID signing and notarization (add the secrets).
- A JUCE commercial licence before selling.
- The manual DAW recall pass in [TESTING.md](TESTING.md).
- An Apple Silicon CI runner if your GitHub plan provides one (the Intel runner already builds the
  Universal binary).
