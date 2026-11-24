# Mystery Box by Raygun — User Manual (v1.0)

Mystery Box is a six-oscillator synthesizer for VST3 and AU hosts, with a standalone app. It
includes a 10-slot FX rack, an arpeggiator and step sequencer, 1,000 factory presets, and 25
playable games that make music.

---

## 1. Installing

**macOS (12 or later, Apple Silicon and Intel)**

1. Run `MysteryBox-<version>-macOS.pkg`. It installs the following:
   - VST3: `/Library/Audio/Plug-Ins/VST3/Mystery Box.vst3`
   - AU: `/Library/Audio/Plug-Ins/Components/Mystery Box.component`
   - Standalone app: `/Applications/Mystery Box.app`
2. If the package isn't notarized, macOS may block the first launch. Right-click the `.pkg` and
   choose **Open**, or allow it in **System Settings → Privacy & Security**.
3. Rescan plug-ins in your DAW. In Logic Pro, the AU appears under *Raygun → Mystery Box*.

**Windows 10/11 (x64)**

Run `MysteryBox-<version>-Windows-Setup.exe`. The VST3 goes to
`C:\Program Files\Common Files\VST3`. The standalone app is optional.

---

## 2. Quick start

1. Load Mystery Box on an instrument track and play. The **INIT** patch is a single saw.
2. Open **02 PRESETS**, pick a category, and double-click a preset to load it. **PREVIEW**
   loads the selected preset and plays a short phrase.
3. Turn the four **MACROS** (SYNTH tab → MACROS). Every factory preset sets them up as follows:
   - Macro 1: Brightness.
   - Macro 2: Movement.
   - Macro 3: Space (reverb send).
   - Macro 4: a character control specific to the preset.
4. No MIDI keyboard? In the standalone app or plug-in window, the computer keys
   **A W S E D F T G Y H U J K** play notes. **Z / X** shift the octave.

---

## 3. The interface

The interface is a 1280×800 vector design. Drag the window corner, or set the scale under
**SETTINGS** (75–200%).

**Top bar**
- Logo, **UNDO / REDO**, and the six tabs.
- The current preset, with **< >** (previous/next in the browser's current list), **A/B**
  (compare two snapshots), and **SAVE**.
- A mini scope, the output meter, and CPU load.

**Status bar:** sample rate, oversampling, latency, active voices, host tempo, and parameter
count.

**Slim meters (right edge):** output level at a glance. Click them to open the full meters.

**Every knob**
| Action | Result |
|---|---|
| Drag up/down | Change the value |
| Ctrl/Cmd + drag | Fine adjustment |
| Double-click, or Alt+click | Reset to default |
| Hover | Tooltip with the live value |
| Right-click | Reset, randomize, MIDI Learn, *Add modulation slot*, host automation menu |
| Tab, then arrow keys | Keyboard control (Shift = fine) |

Modulatable knobs show the following:
- A **magenta arc**: the total mod-matrix depth routed to that knob.
- A **lime dot**: the live modulated value of the newest voice.

---

## 4. SYNTH tab

### Oscillators (OSC.01 – OSC.06)

Each oscillator has a power LED. Click a tab to edit that oscillator. Its **TYPE** can be:

| Type | Notes |
|---|---|
| Wavetable | 6 factory tables. **LOAD** or drag and drop a WAV/AIFF/FLAC; 2048-sample frames are detected. **DRAW** paints a single cycle. |
| Analog | Sine, triangle, saw, square, and pulse with PWM. Anti-aliased (polyBLEP/BLAMP). |
| FM | A sine operator. Set **WARP = FM** and a **SOURCE** oscillator to phase-modulate it. Choosing itself as the source gives feedback FM. |
| Granular | Load a sample. Controls: size, density, spray, width. **WT POS** or the scanner sets the position. |
| Supersaw | Seven or more detuned saws with a non-linear spread. |
| Noise | White, pink, brown, digital (pitched), and vinyl. |

**Per-oscillator controls**
- Pitch: octave (32'–2'), coarse, fine.
- Mix: level and pan.
- Start phase: phase and phase randomization.
- Unison: 1–16 voices, with detune, blend, and stereo spread.
- **WT POS** and **PW**.
- **ROUTE**: Filter 1, Filter 2, both, or bypass.

**Warp modes:** bend ±, sync, mirror, PWM, formant, quantize, bit crush, and FM/AM/ring (using
the **SOURCE** oscillator).

**Scan:** when **SCAN** is on, the wavetable or grain position travels START → MID → END. It runs
one-shot, looping, or ping-pong, in Hz or synced to tempo.

**Display:** click it to switch between 3D waterfall, 2D, and harmonic spectrum views.

### Mixer and Voice

**MIXER**
- Sub oscillator: sine, triangle, square, or saw, one or two octaves down.
- Noise layer.
- Ring modulator: any two oscillators.
- Each has its own level and filter route.

**VOICE**
- Voice mode:
  - **Poly** (up to 32 voices).
  - **Mono** and **Legato** (last-note priority).
  - **Unison**: stacks 1–8 voices per note with detune.
- Glide: time, Always or Legato-only, and a linear-to-exponential curve.
- Pitch-bend range.
- **MPE**: per-note bend, pressure, and slide (CC74) on member channels 2–16.
- Master volume and oversampling (1x/2x/4x/8x).

### Filters

There are two filters. Set the routing on Filter 1:
- **Serial**: F1 → F2.
- **Parallel**: F1 + F2.
- **Split**: F1 on the left channel, F2 on the right.

| Group | Types |
|---|---|
| SVF | LP/HP/BP/notch at 12 and 24 dB |
| Analog-style | Ladder 24, Diode 24, Acid 18 (303-style, with a feedback high-pass) |
| Character | Comb +, Comb −, Formant (the cutoff sweeps a-e-i-o-u), Phaser |

**Controls**
- Cutoff and resonance. Resonance self-oscillates near maximum.
- Drive. The saturation model is set under ADVANCED.
- **ENV**: Envelope 2 amount, ±96 semitones.
- **KEY**: keytracking.
- **VEL**: velocity.
- **FM**: audio-rate cutoff modulation from OSC 1.
- **MIX**: dry/wet.

The display shows the true frequency response, measured by running the actual filter.

### Modulation section

| Tab | What it is |
|---|---|
| ENV 1–4 | DAHDSR with per-stage curves, velocity, and loop. ENV 1 is the amp (with **PUNCH**); ENV 2 drives the filter **ENV** knobs. |
| MSEG | Drawable multi-segment envelope. Click to add a point, drag to move, double-click to delete, drag a segment to curve it. Right-click for presets. Time or tempo sync, with loop. |
| LFO 1–6 | Sine, triangle, saw up, saw down, square, S&H, smooth random, and a drawable custom shape. Free or synced (1/64 to 8 bars, with triplets and dotted values). Retrigger, free (follows the transport), or one-shot envelope mode. Phase, fade-in, smoothing, bipolar/unipolar. |
| MATRIX | 32 slots: source, **via** (scales the amount by a second source), amount, curve, destination, bypass. |
| MACROS | 8 knobs. Double-click a name to rename it. |
| ARP/SEQ | See section 6. |

**Quickest way to modulate:** drag a **DRAG …** chip (on every ENV, MSEG, LFO, and macro page)
onto any knob that has a magenta-capable ring. Alternatively, right-click a knob and choose
**Add modulation slot**. Each page's **ROUTED TO** list shows where that source goes.

**Matrix sources:**
- Envelopes 1–4, MSEG, LFOs 1–6.
- Velocity, aftertouch (channel and poly), mod wheel, pitch bend, keytrack, random.
- Macros 1–8.
- MPE pressure, slide, and glide.
- Sequencer lanes 1–2.
- Game 1–4.

**Matrix destinations:**
- Per oscillator: pitch, level, pan, wavetable position, pulse width, detune, blend, spread,
  warp.
- Per filter: cutoff, resonance, drive, mix, FM.
- Sub, noise, and ring levels; amp level and pan; LFO rates; global pitch.
- The 10 FX slot mixes and both send levels. These respond to global sources such as macros and
  the mod wheel.

---

## 5. FX tab

**RACK:** ten serial slots plus two parallel **send buses** (A and B).
- Choosing a type loads that effect's defaults, including a sensible dry/wet mix.
- Drag a slot's header onto another slot to move it in the chain.
- Each slot has ON (crossfaded), MIX, and six parameters.

**Effects**
- Dynamics: Distortion (tube, tape, fold, hard, crush), OTT (3-band up/down compression),
  Compressor, Transient shaper, Limiter, Ducker (tempo-synced sidechain-style shaper).
- Modulation: Chorus, Flanger, Phaser, Ensemble.
- Time-based: Delay (synced, ping-pong, tape), Reverb (hall, plate, room, shimmer).
- Creative: Stutter/Freeze, Formant filter, Ring mod, Frequency shifter, Vocoder (uses the
  plug-in's **sidechain** input when connected, otherwise a built-in vowel pattern).
- Utility: Widener, Tilt EQ.

**MASTER**
- 6-band EQ: drag the numbered nodes, use the mouse wheel to change Q, and double-click for 0 dB.
  A live FFT analyser with peak hold runs behind it.
- Output meters: peak, RMS, LUFS (momentary and short-term), true peak, and a clip LED (click to
  reset).
- Goniometer with correlation.
- Output safety limiter (ceiling) and master volume.

---

## 6. Arpeggiator and sequencer (SYNTH → ARP/SEQ)

**MODE**
- **Arpeggiator**: Up, Down, Up/Down, Random, As Played, or Chord. Rate, gate, swing, 1–4
  octaves, hold (latch), and retrigger.
- **Sequencer**: 1–64 steps across 4 pages. Edit one lane at a time: **pitch, velocity, gate,
  probability, ratchet (1–4), slide, on/off, mod 1, mod 2**. Hold a key to transpose
  (**FOLLOW**). Lanes *mod 1/2* are the matrix sources *Seq Lane 1/2*.

Both lock to the host transport when it's playing and run at the host tempo otherwise.
**MIDI OUT** also sends the generated notes to your DAW.

---

## 7. Presets

- **Library:** all presets, the ten categories, Favourites, Recent, and User. Tag chips filter by
  style (UKG, Dubstep, Hyperpop, Cinematic, Lo-Fi, …).
- **Browser:** search by name, tag, or description. Click ☆ to favourite and the bars to rate.
  Double-click to load.
- **Save as:** name, category, tags, and description. User presets are `.mbxpreset` JSON files in
  the preset folder (set in SETTINGS). **EXPORT / IMPORT** move single files.
- **A/B** in the top bar: toggles between two independent snapshots of the patch while you tweak.

The 1,000 factory presets are level-matched to about −6 dBFS peak on a short audition phrase.

---

## 8. XD games

Pick one of 25 games and press **PLAY**. Games play the current patch: everything they do becomes
notes that are tempo-synced, locked to the chosen **scale** and **key**, and **quantized** to
16ths by the amount you set.

**PERFORMANCE MODE** lets the game play itself, turning it into a generative sequencer.

**Sound target**
- The current patch.
- The current patch plus MIDI out.
- One of several curated presets.

**Mod Out:** each game outputs four signals, the matrix sources **Game 1–4**. For example,
Block Drop's stack height, Pulse Pong's paddle height, Synthwave Racer's steering and speed, and
Glitch Whack's hit pulse. Route them anywhere, including FX mixes.

**High scores:** the top five per game are kept per user. Each game's controls are shown under
**HOW TO PLAY**.

**The games:**
1. Block Drop
2. Neon Snake
3. Pulse Pong
4. Brick Breaker
5. Asteroid Drift
6. Grid Raiders
7. Maze Muncher
8. Wave Rider
9. Life Cells
10. Echo Memory (ear training)
11. Rhythm Lanes
12. Signal Sweep
13. Merge Tones
14. Neon Pinball
15. Orbit
16. Bounce Box
17. Lane Cross
18. Stack Four
19. Gem Chords
20. Neon Run
21. Glitch Whack
22. Synthwave Racer
23. Frequency Defense
24. Beat Typer
25. Push Box Chords

---

## 9. ADVANCED tab

- **Voice stealing:** quietest, oldest, lowest note, or highest note.
- **Filter saturation model:** clean tanh, tube, diode, or tape.
- **Oversampling.**
- **DC blocker.**
- **Drift:** slow per-voice analog pitch wander.
- **Voice RND:** per-note random pitch, cutoff, and pan.
- **Mod smooth:** smoothing on every matrix destination.
- **Velocity and keytrack curves,** each with a live curve view.
- **Microtuning:** load a Scala `.scl`, plus an optional `.kbm` keyboard map. The tuning is saved
  with the project.
- **Parameter browser:** all 712 parameters, searchable and editable. **LEARN** maps a MIDI CC;
  **MAPPED ONLY** shows the CC table.

## 10. SETTINGS

These are saved per user and apply to every instance:
- Theme: Neon Cyan, Magenta Night, Acid Green, Mono, or **Colorblind Safe**.
- UI scale.
- Animation level: Full, Reduced, or **Eco / Low GPU**.
- Default oversampling for new instances.
- Tooltips.
- GPU (OpenGL) rendering.
- Preset folder.
- Clear MIDI Learn.
- Keyboard shortcuts, check for updates, and about/licensing.

---

## 11. Tips and troubleshooting

- **CPU:** a 6-oscillator, 8-voice patch at 48 kHz with 2× oversampling uses about 4% of one
  modern core. Lower oversampling, unison, or polyphony if needed; 8× is for mixing and
  rendering.
- **Latency:** oversampling adds a few samples of latency, which is reported to the host
  automatically.
- **No sound from a game:** make sure **PLAY** is on and the patch makes sound. Check the scale
  and octave.
- **Vocoder silent:** enable the plug-in's sidechain input in your DAW, or set SOURCE to the
  built-in pattern.
- **A moved sample or wavetable no longer loads:** imported files are referenced by path. Drawn
  wavetables are stored inside the project.

---

## 12. Credits and licences

- Mystery Box by Raygun. All factory wavetables, presets, and games are original content.
- Built with JUCE 8. Commercial distribution requires a JUCE licence or AGPLv3 compliance.
- VST is a registered trademark of Steinberg Media Technologies GmbH.
- Fonts: Orbitron and JetBrains Mono, under the SIL Open Font License 1.1. The licence texts are
  in `resources/fonts/`.
