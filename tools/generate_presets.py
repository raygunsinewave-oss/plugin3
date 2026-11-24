#!/usr/bin/env python3
"""Generates the 1,000 Mystery Box factory presets.

Usage:  python3 tools/generate_presets.py [--out presets/factory] [--bundle resources/factory_presets.json]

Every preset is written as an individual, human-readable JSON file (easy to diff / edit / version)
and all of them are bundled into one JSON array that the plug-in embeds. Values are the
parameters' plain (de-normalised) values; anything not listed stays at its default.

Presets are built from per-category *archetypes* (recipes) with seeded random variation, plus a
few hand-designed "hero" presets per category. Every preset gets meaningful macros
(1 = Brightness, 2 = Movement, 3 = Space, 4 = category-specific character).

Style tags describe sound-design direction only. Preset names are original word combinations;
no artist names appear anywhere.

After generation, run the level-matching pass (renders every preset and sets its master volume
so the demo phrase peaks at about -6 dBFS):
    build/MysteryBoxTests_artefacts/Release/MysteryBoxTests --level-presets resources/factory_presets.json
"""
import argparse, json, math, os, random, re, zlib

COUNTS = {"Bass": 180, "Leads": 160, "Pads": 150, "Plucks & Keys": 110, "Winds": 60,
          "Ambient": 90, "Drones": 60, "FX": 90, "Arps & Sequences": 60, "Misc": 40}

STYLES = {
    "ukg": ["UKG", "Emotional House", "Garage"],
    "melodic": ["Melodic House", "Progressive", "Euphoric"],
    "bigroom": ["Big Room", "Festival", "Polished"],
    "dubstep": ["Dubstep", "Bass Music", "Heavy"],
    "basshouse": ["Bass House", "Bassline", "Tearout"],
    "organic": ["Organic Electronica", "Folktronica", "Textural"],
    "hyper": ["Hyperpop", "Club Pop", "Glitch"],
    "chip": ["Chiptune", "8-Bit", "Hyperpop"],
    "cinematic": ["Cinematic", "Anime", "Emotional"],
    "lofi": ["Lo-Fi", "Warm", "Vintage"],
}

# ---------------------------------------------------------------- enum indices (mirror the C++ tables)
OSC_TYPE = dict(wt=0, analog=1, fm=2, granular=3, supersaw=4, noise=5)
SHAPE = dict(sine=0, tri=1, saw=2, square=3, pulse=4)
TABLE = dict(basic=0, harmonic=1, pwm=2, vowel=3, fold=4, sync=5)
WARP = dict(none=0, bendp=1, bendm=2, sync=3, mirror=4, pwm=5, formant=6, quant=7, crush=8, fm=9, am=10, ring=11)
FILT = dict(lp12=0, lp24=1, hp12=2, hp24=3, bp12=4, bp24=5, notch12=6, notch24=7, ladder=8, diode=9,
            combp=10, combm=11, formant=12, phaser=13, acid=14)
LFO_SHAPE = dict(sine=0, tri=1, sawup=2, sawdown=3, square=4, sh=5, smooth=6, custom=7)
DIV = {"1/64": 0, "1/32T": 1, "1/32": 2, "1/16T": 4, "1/16": 5, "1/16D": 6, "1/8T": 7, "1/8": 8, "1/8D": 9,
       "1/4T": 10, "1/4": 11, "1/4D": 12, "1/2": 14, "1bar": 16, "2bars": 17, "4bars": 18}
SRC = dict(env1=1, env2=2, env3=3, env4=4, mseg=5, lfo1=6, lfo2=7, lfo3=8, lfo4=9, vel=12, at=13, mw=14,
           pb=15, key=16, rnd=17, m1=18, m2=19, m3=20, m4=21, seq1=29, seq2=30)
FX = dict(off=0, dist=1, ott=2, comp=3, trans=4, chorus=5, flanger=6, phaser=7, ensemble=8, delay=9, reverb=10,
          stutter=11, formant=12, ring=13, fshift=14, vocoder=15, widener=16, ducker=17, limiter=18, tilt=19)

def d_osc(i, d):  # destination index helpers (see dsp::dest)
    return 1 + i * 9 + ["pitch", "level", "pan", "wtpos", "pw", "detune", "blend", "spread", "warp"].index(d)
def d_filt(f, d):
    return 55 + f * 5 + ["cutoff", "res", "drive", "mix", "fm"].index(d)
D_SUB, D_NOISE, D_RING, D_AMP, D_AMPPAN, D_LFORATE, D_PITCH = 65, 66, 67, 68, 69, 70, 76
D_FXMIX, D_SENDA, D_SENDB = 77, 87, 88

# ---------------------------------------------------------------- names (original word lists)
ADJ = ["Glass", "Rubber", "Neon", "Chrome", "Velvet", "Static", "Hollow", "Liquid", "Paper", "Midnight", "Copper", "Silk",
       "Feral", "Lucid", "Fractured", "Sunken", "Ghost", "Plasma", "Woolen", "Electric", "Tidal", "Rusted", "Pastel",
       "Hyper", "Cobalt", "Amber", "Vapor", "Graphite", "Orbital", "Crystal", "Molten", "Faded", "Wired", "Polar",
       "Satin", "Signal", "Mirror", "Dusty", "Burning", "Quiet", "Gravity", "Prism", "Rogue", "Sapphire", "Low",
       "Wobbly", "Tangled", "Bright", "Grainy", "Distant", "Opal", "Iron", "Floating", "Shattered", "Soft", "Sugar",
       "Thunder", "Ultra", "Echo", "Laser", "Pixel", "Lunar", "Solar", "Arcade", "Bitter", "Golden", "Hazy"]
NOUN = {
    "Bass": ["Gutter", "Engine", "Rumble", "Pressure", "Basement", "Tremor", "Grinder", "Subway", "Bulldozer", "Furnace",
             "Reactor", "Gravel", "Undertow", "Turbine", "Knuckle", "Machine", "Chassis", "Sludge", "Piston", "Cellar"],
    "Leads": ["Hallway", "Signal", "Comet", "Siren", "Beacon", "Skyline", "Spark", "Laser", "Anthem", "Arrow",
              "Voice", "Wire", "Halo", "Flare", "Rocket", "Kite", "Satellite", "Neon Sign", "Lantern", "Banner"],
    "Pads": ["Horizon", "Cathedral", "Blanket", "Aurora", "Harbor", "Nebula", "Lagoon", "Canopy", "Dawn", "Atrium",
             "Cloud", "Glacier", "Veil", "Meadow", "Tide", "Bloom", "Haze", "Expanse", "Sanctuary", "Drift"],
    "Plucks & Keys": ["Kalimba", "Droplet", "Music Box", "Marimba", "Keys", "Bell", "Pebble", "Chime", "Harp", "Pluck",
                      "Upright", "Ivory", "Tine", "Clockwork", "Lantern", "Petal", "Raindrop", "Mallet", "Snap", "Toy Piano"],
    "Winds": ["Flute", "Reed", "Breath", "Whistle", "Ocarina", "Pipe", "Horn", "Panpipe", "Shakuhachi", "Bugle",
              "Bamboo", "Gust", "Sigh", "Draft", "Bellows", "Recorder", "Duduk", "Fife", "Calliope", "Zephyr"],
    "Ambient": ["Room", "Memory", "Shoreline", "Dust", "Hallway", "Forest", "Snowfall", "Window", "Tape", "Signal",
                "Daydream", "Afterglow", "Fog", "Driftwood", "Lullaby", "Archive", "Echo", "Garden", "Field", "Static"],
    "Drones": ["Monolith", "Abyss", "Engine Room", "Hum", "Void", "Pylon", "Leviathan", "Obelisk", "Bunker", "Core",
               "Chasm", "Ritual", "Generator", "Crypt", "Dynamo", "Trench", "Orbit", "Mantle", "Tunnel", "Mass"],
    "FX": ["Riser", "Impact", "Sweep", "Zap", "Glitch", "Downlifter", "Warp", "Alarm", "Transmission", "Teleport",
           "Laser", "Swoosh", "Blast", "Static", "Beam", "Collapse", "Uplift", "Siren", "Scanner", "Power Down"],
    "Arps & Sequences": ["Runner", "Circuit", "Stairway", "Pulse", "Cascade", "Spiral", "Ticker", "Loop", "Shuffle", "Ladder",
                         "Sprinkler", "Conveyor", "Rotor", "Hopscotch", "Ripple", "Relay", "Gears", "Pinwheel", "Morse", "Carousel"],
    "Misc": ["Organ", "Stab", "Chop", "Choir", "Strings", "Brass", "Chord", "Vox", "Toy", "Hit",
             "Sweep Chord", "Steel Drum", "Theremin", "Clav", "Harmonica", "Stack", "Synth Choir", "Accordion", "Banjo", "Kazoo"],
}

class Preset:
    def __init__(self, name, category, styles, desc, author="Raygun Factory"):
        self.name, self.category, self.desc, self.author = name, category, desc, author
        self.tags = []
        for s in styles:
            for t in STYLES.get(s, [s]):
                if t not in self.tags:
                    self.tags.append(t)
        self.p = {}
        self.assets = {}
        self.slot = 0
        self.fxslot = 0
        self.osc_used = 0

    def set(self, k, v):
        self.p[k] = round(float(v), 5)
        return self

    # ------------------------------------------------------------ building blocks
    def osc(self, i, kind="analog", shape="saw", table="basic", octave=2, coarse=0, fine=0.0, level=0.75, unison=1,
            detune=15.0, blend=0.75, spread=0.6, wtpos=0.0, pw=0.5, pan=0.0, route=0, warp="none", warp_amt=0.0,
            mod_src=0, phase_rand=1.0, noise=0):
        o = f"osc{i+1}_"
        self.set(o + "on", 1)
        self.set(o + "type", OSC_TYPE[kind]); self.set(o + "shape", SHAPE[shape]); self.set(o + "table", TABLE[table])
        self.set(o + "octave", octave); self.set(o + "coarse", coarse); self.set(o + "fine", fine)
        self.set(o + "level", level); self.set(o + "unison", unison); self.set(o + "detune", detune)
        self.set(o + "blend", blend); self.set(o + "spread", spread); self.set(o + "wt_pos", wtpos)
        self.set(o + "pw", pw); self.set(o + "pan", pan); self.set(o + "route", route)
        self.set(o + "warp", WARP[warp]); self.set(o + "warp_amt", warp_amt); self.set(o + "mod_src", mod_src)
        self.set(o + "phase_rand", phase_rand); self.set(o + "noise", noise)
        return self

    def off(self, i):
        return self.set(f"osc{i+1}_on", 0)

    def scan(self, i, rate=0.3, start=0.0, mid=0.5, end=1.0, loop=2, sync=False, div="1bar"):
        o = f"osc{i+1}_"
        self.set(o + "scan_on", 1); self.set(o + "scan_rate", rate); self.set(o + "scan_start", start)
        self.set(o + "scan_mid", mid); self.set(o + "scan_end", end); self.set(o + "scan_loop", loop)
        self.set(o + "scan_sync", 1 if sync else 0); self.set(o + "scan_div", DIV[div])
        return self

    def filt(self, f=0, kind="lp24", cutoff=8000, res=0.15, drive=0.0, env=0.0, key=0.0, vel=0.0, fm=0.0, mix=1.0, on=True):
        o = f"filter{f+1}_"
        self.set(o + "on", 1 if on else 0); self.set(o + "type", FILT[kind]); self.set(o + "cutoff", cutoff)
        self.set(o + "res", res); self.set(o + "drive", drive); self.set(o + "env", env); self.set(o + "key", key)
        self.set(o + "vel", vel); self.set(o + "fm", fm); self.set(o + "mix", mix)
        return self

    def env(self, e=0, a=0.005, d=0.3, s=0.8, r=0.25, h=0.0, delay=0.0, ac=0.0, dc=0.6, rc=0.6, vel=None, loop=False, punch=None):
        o = f"env{e+1}_"
        self.set(o + "attack", a); self.set(o + "decay", d); self.set(o + "sustain", s); self.set(o + "release", r)
        self.set(o + "hold", h); self.set(o + "delay", delay); self.set(o + "a_curve", ac); self.set(o + "d_curve", dc)
        self.set(o + "r_curve", rc); self.set(o + "loop", 1 if loop else 0)
        if vel is not None: self.set(o + "velocity", vel)
        if punch is not None and e == 0: self.set("env1_punch", punch)
        return self

    def lfo(self, l=0, shape="sine", rate=2.0, sync=False, div="1/4", mode=0, bipolar=True, fade=0.0, smooth=0.0, phase=0.0):
        o = f"lfo{l+1}_"
        self.set(o + "shape", LFO_SHAPE[shape]); self.set(o + "rate", rate); self.set(o + "sync", 1 if sync else 0)
        self.set(o + "div", DIV[div]); self.set(o + "mode", mode); self.set(o + "bipolar", 1 if bipolar else 0)
        self.set(o + "fade", fade); self.set(o + "smooth", smooth); self.set(o + "phase", phase)
        return self

    def mod(self, src, dest, amt, via=0, curve=0.0):
        k = self.slot
        if k >= 32:
            return self
        self.slot += 1
        o = f"mod{k+1}_"
        self.set(o + "src", SRC[src] if isinstance(src, str) else src); self.set(o + "dest", dest)
        self.set(o + "amt", max(-1.0, min(1.0, amt))); self.set(o + "via", SRC[via] if isinstance(via, str) else via)
        self.set(o + "curve", curve)
        return self

    def fx(self, kind, params, mix=None):
        if self.fxslot >= 10:
            return self
        k = self.fxslot
        self.fxslot += 1
        o = f"fx{k+1}_"
        self.set(o + "type", FX[kind]); self.set(o + "on", 1)
        default_mix = {"chorus": 0.5, "flanger": 0.5, "phaser": 0.6, "ensemble": 0.6, "delay": 0.3, "reverb": 0.25,
                       "ring": 0.5, "fshift": 0.5, "formant": 0.8}.get(kind, 1.0)
        self.set(o + "mix", default_mix if mix is None else mix)
        for i, v in enumerate(params):
            self.set(o + f"p{i+1}", v)
        return k

    def sends(self, reverb=0.0, delay=0.0, rv=(0.6, 0.5, 0.4, 0.1, 0.0, 0.8), dl=(0.58, 0.5, 0.4, 0.5, 0.2, 1.0)):
        if reverb > 0:
            self.set("fxA_send", reverb)
            for i, v in enumerate(rv): self.set(f"fxA_p{i+1}", v)
        if delay > 0:
            self.set("fxB_send", delay)
            for i, v in enumerate(dl): self.set(f"fxB_p{i+1}", v)
        return self

    def macros(self, names, values=(0.5, 0.0, 0.3, 0.0)):
        for m, n in enumerate(names):
            self.assets[f"macro{m+1}_name"] = n.upper()
        for m, v in enumerate(values[:len(names)]):
            self.set(f"macro{m+1}", v)
        return self

    def standard_macros(self, rng, fourth="Grit", f=0, cutoff_amt=0.35, move_dest=None, move_lfo="lfo2", grit_dest=None):
        """Brightness -> cutoff; Movement -> LFO depth (via); Space -> reverb send; 4th -> character."""
        self.mod("m1", d_filt(f, "cutoff"), cutoff_amt)
        self.mod(move_lfo, move_dest if move_dest is not None else d_filt(f, "cutoff"), rng.uniform(0.2, 0.45), via="m2")
        self.mod("m3", D_SENDA, 0.45)
        if grit_dest is not None:
            self.mod("m4", grit_dest, 0.6)
        self.macros(["Brightness", "Movement", "Space", fourth], (0.0, 0.0, 0.0, 0.0))
        return self

    def to_json(self):
        return {"format": "mysterybox-preset", "version": 1, "name": self.name, "category": self.category,
                "author": self.author, "description": self.desc, "tags": self.tags, "params": self.p,
                "assets": self.assets}

# ---------------------------------------------------------------- helpers
def seq_pattern(rng, length=16, scale=(0, 3, 5, 7, 10, 12), density=0.75, ratchets=False, slides=False):
    steps = []
    for i in range(64):
        j = i % length
        on = 1 if (j % 4 == 0 or rng.random() < density) else 0
        pitch = rng.choice(scale) if j % 4 else 0
        vel = 1.0 if j % 4 == 0 else rng.uniform(0.55, 0.9)
        gate = rng.choice([0.3, 0.5, 0.5, 0.75])
        rat = rng.choice([1, 1, 1, 2]) if ratchets else 1
        sl = 1 if (slides and rng.random() < 0.2) else 0
        steps.append(f"{on},{pitch:.2f},{vel:.3f},{gate:.3f},1.000,{rat},{sl},{(j / max(1, length - 1)):.3f},{(1.0 if j % 2 == 0 else 0.0):.3f}")
    return ";".join(steps)

def shape_string(points):
    return ";".join(f"{x:.4f},{y:.4f},{c:.3f}" for x, y, c in points)

# ---------------------------------------------------------------- archetypes
def bass_reese(p, r):
    p.desc = "Two detuned saw stacks beating against each other through a warm ladder filter. Macro 2 animates the detune."
    p.osc(0, "analog", "saw", unison=4, detune=r.uniform(18, 35), blend=0.6, spread=0.3, octave=1)
    p.osc(1, "analog", "saw", unison=2, detune=r.uniform(8, 18), fine=r.uniform(8, 20), octave=1, level=0.6)
    p.set("sub_on", 1).set("sub_level", r.uniform(0.4, 0.7))
    p.filt(0, "ladder", cutoff=r.uniform(300, 1200), res=r.uniform(0.1, 0.35), drive=r.uniform(0.2, 0.5))
    p.env(0, a=0.003, d=0.5, s=0.9, r=0.15)
    p.lfo(1, "sine", rate=r.uniform(0.1, 0.4))
    p.set("voice_mode", 2).set("glide_time", r.uniform(0.03, 0.12))
    p.standard_macros(r, "Grit", move_dest=d_osc(0, "detune"), grit_dest=d_filt(0, "drive"))
    p.fx("dist", [0.3, 0.0, 0.6, 0.5, 0.0, 0.45]); p.fx("ott", [0.6, 0.4, 0.8, 0.9, 0.5, 0.5])

def bass_growl(p, r):
    p.desc = "Formant wavetable growl with FM warp, tempo-synced LFO on position and filter, distortion and OTT. Macro 2 speeds the talk."
    p.osc(0, "wt", table=r.choice(["vowel", "fold", "sync"]), wtpos=0.3, octave=1, unison=2, detune=8, warp="fm", warp_amt=r.uniform(0.15, 0.4), mod_src=2)
    p.osc(1, "fm", octave=r.choice([1, 2]), coarse=r.choice([0, 7, 12]), level=0.0)
    p.set("sub_on", 1).set("sub_level", 0.5)
    p.filt(0, r.choice(["lp24", "formant", "diode"]), cutoff=r.uniform(700, 2500), res=r.uniform(0.3, 0.6), drive=0.4)
    p.env(0, a=0.002, d=0.4, s=1.0, r=0.1)
    p.lfo(0, r.choice(["sine", "tri", "sawdown", "custom"]), sync=True, div=r.choice(["1/8", "1/16", "1/8T", "1/4"]))
    p.mod("lfo1", d_osc(0, "wtpos"), r.uniform(0.4, 0.7)); p.mod("lfo1", d_filt(0, "cutoff"), r.uniform(0.2, 0.4))
    p.mod("m2", D_LFORATE, 0.25)
    p.set("voice_mode", 2).set("glide_time", 0.05)
    p.standard_macros(r, "Growl", move_dest=d_osc(0, "warp"), grit_dest=d_osc(0, "warp"))
    p.fx("dist", [0.5, r.choice([0.0, 0.45, 0.65]), 0.65, 0.55, 0.0, 0.4]); p.fx("ott", [1.0, 0.35, 1.0, 1.0, 0.5, 0.45])
    p.assets["lfo1_custom"] = shape_string([(0, 0.5, 0), (0.15, 1, 0.4), (0.4, 0.2, -0.3), (0.65, 0.9, 0.5), (1, 0.5, 0)])

def bass_wobble(p, r):
    p.desc = "Classic tempo-synced wobble: saw + square into a resonant filter swept by LFO 1. Macro 2 doubles the wobble rate."
    p.osc(0, "analog", "saw", octave=1, unison=3, detune=12)
    p.osc(1, "analog", "square", octave=1, level=0.55)
    p.set("sub_on", 1).set("sub_level", 0.6)
    p.filt(0, r.choice(["lp24", "ladder", "lp12"]), cutoff=r.uniform(150, 400), res=r.uniform(0.4, 0.7), drive=0.3)
    p.lfo(0, "sine", sync=True, div=r.choice(["1/8", "1/4", "1/8T", "1/16"]), bipolar=False)
    p.mod("lfo1", d_filt(0, "cutoff"), r.uniform(0.45, 0.7))
    p.mod("m2", D_LFORATE, 0.25)
    p.env(0, a=0.002, d=0.3, s=1.0, r=0.1)
    p.set("voice_mode", 2).set("glide_time", 0.04)
    p.standard_macros(r, "Grit", grit_dest=d_filt(0, "drive"))
    p.fx("dist", [0.35, 0.0, 0.7, 0.5, 0.0, 0.45])

def bass_ukg(p, r):
    p.desc = "Bouncy rubber bass for garage grooves: mono with glide, short filter envelope and a touch of saturation. Macro 4 adds bounce."
    p.osc(0, "analog", r.choice(["square", "saw", "pulse"]), octave=1, pw=r.uniform(0.3, 0.5))
    p.osc(1, "analog", "sine", octave=1, level=0.6)
    p.filt(0, "lp24", cutoff=r.uniform(250, 700), res=r.uniform(0.2, 0.45), env=r.uniform(0.25, 0.45), drive=0.2)
    p.env(1, a=0.001, d=r.uniform(0.08, 0.2), s=0.0, r=0.1)
    p.env(0, a=0.002, d=0.25, s=0.75, r=0.08, punch=0.4)
    p.set("voice_mode", 2).set("glide_time", r.uniform(0.04, 0.09)).set("glide_mode", 1)
    p.lfo(1, "tri", rate=4.0)
    p.standard_macros(r, "Bounce", grit_dest=d_filt(0, "res"))
    p.mod("m4", d_filt(0, "cutoff"), 0.2, via="env2")
    p.fx("comp", [0.55, 0.35, 0.2, 0.3, 0.3, 0.25])

def bass_808(p, r):
    p.desc = "Distorted 808-style bass: sine with a pitch drop from ENV 3, long tail and tube drive. Macro 4 adds more dirt."
    p.osc(0, "analog", "sine", octave=r.choice([1, 2]), level=0.9)
    p.osc(1, "analog", "tri", octave=2, level=0.25)
    p.env(2, a=0.0, d=r.uniform(0.04, 0.12), s=0.0, r=0.05)
    p.mod("env3", D_PITCH, r.uniform(0.25, 0.5))
    p.env(0, a=0.001, d=r.uniform(1.2, 3.0), s=0.0, r=0.4, punch=0.5)
    p.filt(0, "lp12", cutoff=4000, res=0.05)
    p.set("voice_mode", 1).set("glide_time", r.uniform(0.0, 0.1))
    p.standard_macros(r, "Dirt", grit_dest=D_FXMIX)
    p.fx("dist", [r.uniform(0.35, 0.6), 0.0, 0.55, 0.6, 0.0, 0.4], mix=0.6)

def bass_basshouse(p, r):
    p.desc = "Punchy bass-house stab: pulse + saw, organ-ish octave layer, filter envelope and pumping ducker."
    p.osc(0, "analog", "pulse", pw=r.uniform(0.2, 0.4), octave=1)
    p.osc(1, "analog", "saw", octave=2, level=0.45)
    p.osc(2, "analog", "square", octave=3, level=0.25)
    p.filt(0, "diode", cutoff=r.uniform(300, 900), res=r.uniform(0.35, 0.6), env=0.4, drive=0.35)
    p.env(1, a=0.001, d=0.15, s=0.1, r=0.1)
    p.env(0, a=0.001, d=0.3, s=0.6, r=0.06, punch=0.6)
    p.set("voice_mode", 1)
    p.lfo(1, "square", sync=True, div="1/16")
    p.standard_macros(r, "Pump", grit_dest=D_FXMIX + 1)
    p.fx("dist", [0.3, 0.3, 0.6, 0.5, 0.0, 0.45]); p.fx("ducker", [0.6, 0.5, 0.6, 0.1, 0.0, 0], mix=0.0)

def bass_fm(p, r):
    p.desc = "Metallic FM bass: sine carrier phase-modulated by a ratio operator, index decaying with ENV 2."
    p.osc(0, "fm", octave=1, warp="fm", warp_amt=r.uniform(0.2, 0.45), mod_src=2)
    p.osc(1, "fm", octave=r.choice([1, 2, 3]), coarse=r.choice([0, 7, 12, 19]), level=0.0)
    p.env(1, a=0.0, d=r.uniform(0.15, 0.5), s=r.uniform(0.0, 0.3), r=0.1)
    p.mod("env2", d_osc(0, "warp"), r.uniform(0.3, 0.5))
    p.filt(0, "lp24", cutoff=r.uniform(1500, 5000), res=0.2)
    p.env(0, a=0.001, d=0.6, s=0.7, r=0.12)
    p.set("voice_mode", 2).set("glide_time", 0.03)
    p.standard_macros(r, "Metal", move_dest=d_osc(0, "warp"), grit_dest=d_osc(0, "warp"))
    p.fx("ott", [0.5, 0.4, 0.7, 0.8, 0.5, 0.5])

def bass_acid(p, r):
    p.desc = "Squelchy acid line voice: saw into the 18 dB acid filter with accent-style envelope and slides."
    p.osc(0, "analog", r.choice(["saw", "square"]), octave=1)
    p.filt(0, "acid", cutoff=r.uniform(180, 450), res=r.uniform(0.7, 0.9), env=r.uniform(0.35, 0.55), drive=0.3)
    p.env(1, a=0.0, d=r.uniform(0.12, 0.3), s=0.0, r=0.1)
    p.env(0, a=0.002, d=0.3, s=0.7, r=0.05)
    p.set("voice_mode", 1).set("glide_time", 0.05).set("glide_mode", 1)
    p.standard_macros(r, "Squelch", grit_dest=d_filt(0, "res"))
    p.fx("dist", [0.25, 0.0, 0.6, 0.5, 0.0, 0.5]); p.fx("delay", [0.6, 0.5, 0.3, 0.9, 0.3, 1.0], mix=0.2)

def bass_screech(p, r):
    p.desc = "Aggressive mid-range screech: sync-warped saw, comb filter and frequency shifter. Use the upper octaves."
    p.osc(0, "analog", "saw", octave=2, warp="sync", warp_amt=r.uniform(0.3, 0.6), unison=2, detune=10)
    p.filt(0, r.choice(["combp", "combm", "bp24"]), cutoff=r.uniform(400, 1500), res=r.uniform(0.5, 0.8), drive=0.4)
    p.lfo(0, "sawdown", sync=True, div=r.choice(["1/16", "1/8"]))
    p.mod("lfo1", d_osc(0, "warp"), 0.4); p.mod("lfo1", d_filt(0, "cutoff"), 0.3)
    p.env(0, a=0.002, d=0.3, s=1.0, r=0.08)
    p.set("voice_mode", 2)
    p.standard_macros(r, "Scream", move_dest=d_osc(0, "warp"), grit_dest=D_FXMIX + 1)
    p.fx("dist", [0.55, 0.65, 0.7, 0.5, 0.0, 0.4]); p.fx("fshift", [0.53, 0.2, 0.5, 1.0, 0, 0], mix=0.3)
    p.fx("ott", [0.9, 0.3, 1.0, 1.0, 0.5, 0.45])

def lead_supersaw(p, r):
    p.desc = "Huge supersaw lead with stereo spread, gentle filter envelope, long reverb and synced delay."
    p.osc(0, "supersaw", unison=r.choice([7, 9, 11]), detune=r.uniform(20, 35), spread=0.9, octave=r.choice([2, 3]))
    p.osc(1, "analog", "saw", octave=r.choice([2, 3]), coarse=12, level=0.3)
    p.filt(0, "lp24", cutoff=r.uniform(3000, 9000), res=0.15, env=r.uniform(0.0, 0.2))
    p.env(0, a=r.uniform(0.003, 0.02), d=0.5, s=0.8, r=r.uniform(0.2, 0.5))
    p.env(1, a=0.0, d=0.4, s=0.3, r=0.3)
    p.lfo(1, "sine", rate=5.5, fade=0.4)
    p.mod("mw", d_osc(0, "pitch"), 0.02, via="lfo2")
    p.standard_macros(r, "Width", move_dest=d_osc(0, "detune"), grit_dest=d_osc(0, "spread"))
    p.sends(reverb=r.uniform(0.2, 0.4), delay=r.uniform(0.15, 0.3))

def lead_chip(p, r):
    p.desc = "8-bit style lead: pulse with PWM, quantized/bit-crushed warp and quick vibrato. Macro 4 crushes harder."
    p.osc(0, "analog", r.choice(["pulse", "square", "tri"]), octave=3, pw=r.choice([0.125, 0.25, 0.5]), warp=r.choice(["crush", "quant"]), warp_amt=r.uniform(0.3, 0.7))
    p.lfo(0, "tri", rate=6.0, fade=0.25)
    p.mod("lfo1", d_osc(0, "pitch"), 0.012)
    p.lfo(2, "square", sync=True, div="1/16")
    p.mod("lfo3", d_osc(0, "pw"), 0.2, via="m2")
    p.filt(0, "lp12", cutoff=12000, res=0.0, on=r.random() < 0.5)
    p.env(0, a=0.001, d=0.2, s=0.7, r=0.08)
    p.set("voice_mode", 1).set("glide_time", r.choice([0.0, 0.03]))
    p.macros(["Brightness", "Movement", "Space", "Crush"], (0.0, 0.0, 0.0, 0.0))
    p.mod("m1", d_filt(0, "cutoff"), 0.3); p.mod("m3", D_SENDB, 0.4); p.mod("m4", d_osc(0, "warp"), 0.5)
    p.sends(delay=0.2, dl=(0.45, 0.5, 0.35, 0.95, 0.0, 1.0))

def lead_hypervox(p, r):
    p.desc = "Vocal-like hyper lead: formant-warped wavetable, vowel filter, pitch-bend wobble and bright chorus."
    p.osc(0, "wt", table="vowel", wtpos=r.uniform(0.1, 0.9), octave=3, warp="formant", warp_amt=r.uniform(0.2, 0.5), unison=3, detune=10)
    p.filt(0, "formant", cutoff=r.uniform(300, 2000), res=r.uniform(0.12, 0.3), mix=0.7)
    p.lfo(0, "smooth", rate=r.uniform(1.5, 4.0))
    p.mod("lfo1", d_filt(0, "cutoff"), 0.25, via="m2")
    p.mod("at", d_osc(0, "warp"), 0.3)
    p.env(0, a=0.005, d=0.3, s=0.85, r=0.2)
    p.set("voice_mode", 2).set("glide_time", 0.06)
    p.standard_macros(r, "Vowel", move_dest=d_osc(0, "wtpos"), grit_dest=d_osc(0, "wtpos"))
    p.fx("chorus", [0.4, 0.5, 0.4, 0.15, 0.34, 0.9]); p.fx("ott", [0.6, 0.4, 0.8, 0.9, 0.6, 0.5])
    p.sends(reverb=0.2)

def lead_sync(p, r):
    p.desc = "Screaming sync lead: sync warp swept by ENV 2, with portamento and tape delay."
    p.osc(0, "analog", "saw", octave=2, warp="sync", warp_amt=0.2, unison=2, detune=12)
    p.osc(1, "analog", "square", octave=2, level=0.35, coarse=-12)
    p.env(1, a=0.0, d=r.uniform(0.2, 0.6), s=0.2, r=0.3)
    p.mod("env2", d_osc(0, "warp"), r.uniform(0.4, 0.7))
    p.filt(0, "lp24", cutoff=6000, res=0.2)
    p.env(0, a=0.003, d=0.4, s=0.85, r=0.2)
    p.set("voice_mode", 2).set("glide_time", 0.07)
    p.standard_macros(r, "Sync", move_dest=d_osc(0, "warp"), grit_dest=d_osc(0, "warp"))
    p.sends(delay=0.25, dl=(0.58, 0.6, 0.45, 0.95, 0.5, 1.0))

def lead_anime(p, r):
    p.desc = "Emotional soaring lead: detuned saws, slow attack vibrato, warm ensemble, long reverb and delay throws."
    p.osc(0, "analog", "saw", octave=3, unison=5, detune=r.uniform(12, 22), spread=0.8)
    p.osc(1, "analog", "tri", octave=3, coarse=12, level=0.25)
    p.filt(0, "lp24", cutoff=r.uniform(4000, 10000), res=0.1, key=0.3)
    p.env(0, a=r.uniform(0.01, 0.05), d=0.6, s=0.85, r=r.uniform(0.4, 0.9))
    p.lfo(1, "sine", rate=5.0, fade=0.6)
    p.mod("lfo2", d_osc(0, "pitch"), 0.01); p.mod("mw", d_osc(0, "pitch"), 0.015, via="lfo2")
    p.set("voice_mode", 2).set("glide_time", 0.08).set("glide_mode", 1)
    p.standard_macros(r, "Air", grit_dest=D_SENDB)
    p.fx("ensemble", [0.4, 0.5, 0.75, 0.9]); p.sends(reverb=0.35, delay=0.25)

def lead_bigroom(p, r):
    p.desc = "Polished big-room lead: layered supersaw and square, OTT glue, sidechain-style ducking and hall reverb."
    p.osc(0, "supersaw", unison=9, detune=r.uniform(25, 40), spread=1.0, octave=3)
    p.osc(1, "analog", "square", octave=2, level=0.4)
    p.filt(0, "lp24", cutoff=r.uniform(5000, 12000), res=0.1)
    p.env(0, a=0.002, d=0.4, s=0.9, r=0.25, punch=0.3)
    p.standard_macros(r, "Pump", grit_dest=D_FXMIX + 1)
    p.fx("ott", [0.8, 0.4, 0.9, 0.9, 0.55, 0.5]); p.fx("ducker", [0.7, 0.5, 0.6, 0.1, 0.0, 0], mix=0.0)
    p.sends(reverb=0.3)

def pad_supersaw(p, r):
    p.desc = "Wide emotional supersaw chord pad with slow attack and a lush hall. Macro 2 adds filter motion."
    p.osc(0, "supersaw", unison=r.choice([7, 9, 12]), detune=r.uniform(18, 30), spread=1.0, octave=2)
    p.osc(1, "analog", "saw", unison=4, detune=12, octave=3, level=0.35)
    p.filt(0, "lp24", cutoff=r.uniform(1500, 4500), res=0.15, key=0.2)
    p.env(0, a=r.uniform(0.3, 1.2), d=1.0, s=0.9, r=r.uniform(1.0, 2.5))
    p.lfo(1, "sine", rate=r.uniform(0.1, 0.4))
    p.standard_macros(r, "Width", grit_dest=d_osc(0, "spread"))
    p.fx("chorus", [0.25, 0.45, 0.4, 0.1, 0.34, 1.0]); p.sends(reverb=0.45)

def pad_scan(p, r):
    p.desc = "Evolving wavetable pad: two tables slowly scanning in ping-pong, parallel filters and gentle movement."
    t = r.choice(["harmonic", "vowel", "fold", "basic", "sync"])
    p.osc(0, "wt", table=t, unison=4, detune=12, spread=0.8, octave=2)
    p.scan(0, rate=r.uniform(0.03, 0.2), start=0.0, mid=r.uniform(0.3, 0.7), end=r.uniform(0.6, 1.0), loop=2)
    p.osc(1, "wt", table=r.choice(["harmonic", "basic", "pwm"]), unison=2, detune=8, octave=3, level=0.4)
    p.scan(1, rate=r.uniform(0.05, 0.25), loop=2)
    p.filt(0, "lp24", cutoff=r.uniform(1200, 4000), res=0.2)
    p.env(0, a=r.uniform(0.5, 2.0), d=1.5, s=0.85, r=r.uniform(1.5, 3.5))
    p.lfo(1, "smooth", rate=r.uniform(0.1, 0.3))
    p.standard_macros(r, "Evolve", move_dest=d_osc(0, "wtpos"), grit_dest=d_osc(1, "level"))
    p.sends(reverb=0.5, rv=(0.8, 0.7, 0.5, 0.15, 0.0, 0.9))

def pad_sidechain(p, r):
    p.desc = "Sidechained garage/house pad: warm detuned chord with a pumping volume shaper on every beat."
    p.osc(0, "analog", "saw", unison=6, detune=r.uniform(15, 25), spread=0.9, octave=2)
    p.osc(1, "analog", "square", octave=2, level=0.3, unison=2, detune=10)
    p.filt(0, "lp24", cutoff=r.uniform(1500, 3500), res=0.15)
    p.env(0, a=0.05, d=1.0, s=0.9, r=0.6)
    p.lfo(1, "sine", rate=0.2)
    p.standard_macros(r, "Pump", grit_dest=D_FXMIX)
    p.fx("ducker", [r.uniform(0.6, 0.9), 0.5, 0.6, 0.08, 0.0, 0], mix=0.0)
    p.p["fx1_mix"] = 0.85
    p.sends(reverb=0.3)

def pad_lofi(p, r):
    p.desc = "Lo-fi warm pad: soft triangle/saw blend, tape wobble, vinyl noise layer and dark tape saturation."
    p.osc(0, "analog", "tri", unison=3, detune=10, octave=2)
    p.osc(1, "analog", "saw", unison=2, detune=14, octave=2, level=0.4)
    p.set("noise_on", 1).set("noise_type", 4).set("noise_level", r.uniform(0.08, 0.18))
    p.filt(0, "lp12", cutoff=r.uniform(1200, 2500), res=0.1)
    p.env(0, a=0.2, d=1.0, s=0.85, r=1.0)
    p.lfo(1, "smooth", rate=0.4)
    p.mod("lfo2", D_PITCH, 0.004)
    p.standard_macros(r, "Wear", grit_dest=D_NOISE)
    p.fx("dist", [0.15, 0.25, 0.4, 0.5, 0.0, 0.5], mix=0.7); p.fx("chorus", [0.15, 0.6, 0.5, 0.0, 0.0, 0.6])
    p.sends(reverb=0.25)

def pad_shimmer(p, r):
    p.desc = "Cinematic shimmer pad: bright partials into an octave-up shimmer reverb with slow swells."
    p.osc(0, "wt", table="harmonic", wtpos=r.uniform(0.2, 0.6), unison=5, detune=10, spread=1.0, octave=3)
    p.osc(1, "analog", "sine", octave=4, level=0.3)
    p.filt(0, "lp24", cutoff=r.uniform(4000, 9000), res=0.1)
    p.env(0, a=r.uniform(0.8, 2.0), d=1.0, s=0.9, r=3.0)
    p.lfo(1, "sine", rate=0.15)
    p.standard_macros(r, "Shimmer", move_dest=d_osc(0, "wtpos"), grit_dest=D_SENDA)
    p.sends(reverb=0.6, rv=(0.85, 0.75, 0.3, 0.1, 0.9, 1.0))

def pluck_basic(p, r):
    p.desc = "Bright pluck: fast filter envelope on saws, short amp decay and a dotted delay."
    p.osc(0, "analog", r.choice(["saw", "square"]), unison=r.choice([1, 3, 5]), detune=12, octave=r.choice([2, 3]))
    p.filt(0, "lp24", cutoff=r.uniform(300, 900), res=r.uniform(0.15, 0.35), env=r.uniform(0.3, 0.5), key=0.4, vel=0.2)
    p.env(1, a=0.0, d=r.uniform(0.12, 0.35), s=0.0, r=0.2)
    p.env(0, a=0.001, d=r.uniform(0.25, 0.6), s=0.0, r=0.25)
    p.standard_macros(r, "Snap", grit_dest=d_filt(0, "res"))
    p.mod("m4", d_filt(0, "cutoff"), 0.2, via="env2")
    p.sends(reverb=0.2, delay=0.2, dl=(0.62, 0.5, 0.35, 0.95, 0.1, 1.0))

def pluck_kalimba(p, r):
    p.desc = "Organic kalimba-like tine: FM bell partials with a woody decay and a small room."
    p.osc(0, "fm", octave=3, warp="fm", warp_amt=r.uniform(0.1, 0.25), mod_src=2)
    p.osc(1, "fm", octave=3, coarse=r.choice([12, 19, 24, 28]), level=0.0)
    p.env(1, a=0.0, d=r.uniform(0.05, 0.15), s=0.0, r=0.05)
    p.mod("env2", d_osc(0, "warp"), 0.3)
    p.env(0, a=0.0, d=r.uniform(0.6, 1.4), s=0.0, r=0.5, dc=0.8)
    p.filt(0, "lp12", cutoff=7000, res=0.05)
    p.standard_macros(r, "Wood", move_dest=d_osc(0, "warp"), grit_dest=d_osc(0, "warp"))
    p.sends(reverb=0.2, rv=(0.3, 0.3, 0.5, 0.05, 0.66, 0.7))

def keys_rhodes(p, r):
    p.desc = "Warm detuned electric-piano-like keys: soft FM tine with velocity bite, tremolo and chorus."
    p.osc(0, "fm", octave=2, warp="fm", warp_amt=0.15, mod_src=2)
    p.osc(1, "fm", octave=3, coarse=r.choice([0, 12]), level=0.0)
    p.osc(2, "analog", "sine", octave=2, fine=r.uniform(4, 9), level=0.4)
    p.mod("vel", d_osc(0, "warp"), 0.35)
    p.env(1, a=0.0, d=0.4, s=0.1, r=0.3); p.mod("env2", d_osc(0, "warp"), 0.2)
    p.env(0, a=0.002, d=r.uniform(1.5, 3.0), s=0.2, r=0.4)
    p.lfo(1, "sine", rate=r.uniform(3.0, 5.5))
    p.standard_macros(r, "Tremolo", move_dest=D_AMPPAN, grit_dest=D_AMP)
    p.fx("chorus", [0.3, 0.5, 0.45, 0.05, 0.34, 0.8]); p.sends(reverb=0.2)

def keys_lofipiano(p, r):
    p.desc = "Lo-fi saturated piano-ish keys: bright attack, quick decay, tape wobble and vinyl crackle."
    p.osc(0, "analog", "tri", octave=2)
    p.osc(1, "fm", octave=3, level=0.3, warp="fm", warp_amt=0.12, mod_src=1)
    p.set("noise_on", 1).set("noise_type", 4).set("noise_level", 0.1)
    p.filt(0, "lp12", cutoff=r.uniform(2000, 4500), vel=0.3, key=0.4)
    p.env(0, a=0.001, d=r.uniform(1.0, 2.0), s=0.15, r=0.5, punch=0.3)
    p.lfo(1, "smooth", rate=0.6); p.mod("lfo2", D_PITCH, 0.003)
    p.standard_macros(r, "Wear", grit_dest=D_NOISE)
    p.fx("dist", [0.2, 0.25, 0.4, 0.5, 0.0, 0.5], mix=0.6); p.sends(reverb=0.15)

def pluck_bell(p, r):
    p.desc = "Glassy bell: inharmonic FM ratio, long shimmering tail and stereo delay."
    p.osc(0, "fm", octave=3, warp="fm", warp_amt=r.uniform(0.2, 0.35), mod_src=2)
    p.osc(1, "fm", octave=3, coarse=r.choice([5, 9, 14, 17]), fine=r.uniform(-20, 20), level=0.0)
    p.env(1, a=0.0, d=r.uniform(0.3, 1.0), s=0.1, r=0.5); p.mod("env2", d_osc(0, "warp"), 0.25)
    p.env(0, a=0.001, d=r.uniform(2.0, 4.0), s=0.0, r=1.5)
    p.standard_macros(r, "Shine", move_dest=d_osc(0, "warp"), grit_dest=d_osc(0, "warp"))
    p.sends(reverb=0.3, delay=0.2)

def wind_breathy(p, r):
    p.desc = "Breathy wind: soft pulse/triangle core, pink noise breath through a formant filter, delayed vibrato."
    p.osc(0, "analog", r.choice(["tri", "sine", "pulse"]), octave=3, pw=0.3)
    p.set("noise_on", 1).set("noise_type", 1).set("noise_level", r.uniform(0.1, 0.25))
    p.filt(0, r.choice(["formant", "bp12", "lp12"]), cutoff=r.uniform(600, 2500), res=0.3, key=0.5)
    p.env(0, a=r.uniform(0.05, 0.2), d=0.3, s=0.85, r=r.uniform(0.15, 0.4))
    p.lfo(0, "sine", rate=r.uniform(4.5, 6.0), fade=0.5)
    p.mod("lfo1", d_osc(0, "pitch"), 0.008); p.mod("at", D_NOISE, 0.2)
    p.set("voice_mode", 2).set("glide_time", 0.04)
    p.standard_macros(r, "Breath", grit_dest=D_NOISE)
    p.sends(reverb=0.3)

def wind_brass(p, r):
    p.desc = "Synth brass: saw stack with a swelling filter envelope and slight pitch scoop."
    p.osc(0, "analog", "saw", unison=3, detune=10, octave=2)
    p.osc(1, "analog", "saw", octave=2, fine=6, level=0.6)
    p.filt(0, "lp24", cutoff=r.uniform(500, 1200), res=0.15, env=r.uniform(0.3, 0.5), vel=0.2)
    p.env(1, a=r.uniform(0.05, 0.15), d=0.5, s=0.5, r=0.3)
    p.env(2, a=0.0, d=0.08, s=0.0, r=0.05); p.mod("env3", D_PITCH, -0.02)
    p.env(0, a=0.03, d=0.4, s=0.85, r=0.3)
    p.standard_macros(r, "Bite", grit_dest=d_filt(0, "drive"))
    p.sends(reverb=0.25)

def ambient_texture(p, r):
    p.desc = "Slowly breathing texture: scanning tables, smooth random LFOs, noise layer and an enormous reverb."
    p.osc(0, "wt", table=r.choice(["harmonic", "fold", "vowel", "sync"]), unison=3, detune=15, octave=2)
    p.scan(0, rate=r.uniform(0.02, 0.1), loop=2, start=r.uniform(0, 0.3), end=r.uniform(0.7, 1.0))
    p.osc(1, "noise", noise=r.choice([1, 2, 4]), level=r.uniform(0.1, 0.3))
    p.filt(0, r.choice(["lp24", "bp12", "phaser"]), cutoff=r.uniform(800, 3000), res=0.3)
    p.lfo(1, "smooth", rate=r.uniform(0.05, 0.2)); p.lfo(2, "smooth", rate=r.uniform(0.03, 0.12))
    p.mod("lfo3", d_osc(1, "level"), 0.2)
    p.env(0, a=r.uniform(1.0, 3.0), d=2.0, s=0.9, r=r.uniform(3.0, 6.0))
    p.standard_macros(r, "Haze", grit_dest=d_osc(1, "level"))
    p.sends(reverb=0.6, delay=0.2, rv=(0.9, 0.85, 0.5, 0.2, r.choice([0.0, 0.9]), 1.0))

def drone_dark(p, r):
    p.desc = "Dark evolving drone: detuned low oscillators, comb resonance, slow MSEG swell and frequency shifting."
    p.osc(0, "analog", "saw", octave=r.choice([0, 1]), unison=4, detune=r.uniform(15, 40))
    p.osc(1, "wt", table=r.choice(["fold", "sync", "harmonic"]), octave=1, level=0.6)
    p.scan(1, rate=r.uniform(0.01, 0.05), loop=2)
    p.filt(0, r.choice(["combp", "combm", "ladder"]), cutoff=r.uniform(60, 250), res=r.uniform(0.5, 0.85), drive=0.2)
    p.set("mseg_time", r.uniform(6, 20)).set("mseg_loop", 1)
    p.assets["mseg"] = shape_string([(0, 0.2, 0), (0.4, 0.9, 0.4), (0.7, 0.5, -0.3), (1, 0.2, 0.3)])
    p.mod("mseg", d_filt(0, "cutoff"), 0.3)
    p.env(0, a=2.0, d=2.0, s=1.0, r=5.0)
    p.standard_macros(r, "Abyss", grit_dest=D_FXMIX)
    p.fx("fshift", [0.52, 0.4, 0.5, 1.0, 0, 0], mix=0.35); p.sends(reverb=0.5)

def fx_riser(p, r):
    p.desc = "Riser: noise + supersaw swept upward by the MSEG over 8 seconds. Hold a note to build."
    p.osc(0, "supersaw", unison=7, detune=30, octave=2)
    p.osc(1, "noise", noise=0, level=0.5)
    p.filt(0, "hp12", cutoff=200, res=0.3)
    p.set("mseg_time", r.choice([4, 8, 12, 16]))
    p.assets["mseg"] = shape_string([(0, 0, 0), (1, 1, -0.6)])
    p.mod("mseg", d_filt(0, "cutoff"), 0.7); p.mod("mseg", D_PITCH, r.uniform(0.3, 0.5)); p.mod("mseg", D_SENDA, 0.5)
    p.env(0, a=1.0, d=1.0, s=1.0, r=1.5)
    p.standard_macros(r, "Tension", grit_dest=D_NOISE)
    p.sends(reverb=0.3)

def fx_impact(p, r):
    p.desc = "Impact: noise burst + sine thump with a fast pitch drop into a big hall."
    p.osc(0, "analog", "sine", octave=1)
    p.osc(1, "noise", noise=r.choice([0, 2]), level=0.6)
    p.env(2, a=0.0, d=r.uniform(0.1, 0.3), s=0.0, r=0.1); p.mod("env3", D_PITCH, 0.6)
    p.env(0, a=0.0, d=r.uniform(1.0, 2.5), s=0.0, r=1.0, punch=0.8)
    p.filt(0, "lp24", cutoff=3000, res=0.1, env=0.4); p.env(1, a=0, d=0.3, s=0, r=0.2)
    p.standard_macros(r, "Boom", grit_dest=D_FXMIX)
    p.fx("dist", [0.3, 0.0, 0.5, 0.5, 0.0, 0.45]); p.sends(reverb=0.6, rv=(0.9, 0.6, 0.4, 0.0, 0.0, 1.0))

def fx_zap(p, r):
    p.desc = "Laser zap: high square with a steep ENV 3 pitch drop, ring mod and ping-pong delay."
    p.osc(0, "analog", "square", octave=4)
    p.env(2, a=0.0, d=r.uniform(0.05, 0.25), s=0.0, r=0.05); p.mod("env3", D_PITCH, r.uniform(0.6, 1.0))
    p.env(0, a=0.0, d=0.3, s=0.0, r=0.2)
    p.standard_macros(r, "Zap", grit_dest=D_FXMIX)
    p.fx("ring", [0.6, 0.3, 0.3, 0.5, 0.3, 0], mix=0.4); p.sends(delay=0.3, dl=(0.4, 0.5, 0.5, 0.95, 0.0, 1.0))

def fx_glitch(p, r):
    p.desc = "Glitch texture: sample-and-hold pitch + filter jumps, bit crush and a stutter that Macro 4 engages."
    p.osc(0, "analog", "pulse", octave=3, warp="crush", warp_amt=0.5)
    p.lfo(0, "sh", sync=True, div=r.choice(["1/16", "1/32"]))
    p.mod("lfo1", D_PITCH, 0.3); p.mod("lfo1", d_filt(0, "cutoff"), 0.4)
    p.filt(0, "bp24", cutoff=1500, res=0.5)
    p.env(0, a=0.001, d=0.2, s=0.6, r=0.1)
    p.macros(["Brightness", "Movement", "Space", "Crush"], (0.0, 0.0, 0.0, 0.0))
    p.mod("m1", d_filt(0, "cutoff"), 0.3); p.mod("m2", D_LFORATE, 0.3); p.mod("m3", D_SENDA, 0.4)
    p.mod("m4", d_osc(0, "warp"), 0.5)
    p.fx("dist", [0.2, 0.8, 0.7, 0.5, 0.5, 0.5], mix=0.5)
    p.sends(reverb=0.15)

def arp_classic(p, r, pattern):
    p.desc = "Tempo-synced arpeggio voice. Hold a chord: the arpeggiator plays it across octaves."
    p.osc(0, "analog", r.choice(["saw", "square", "pulse"]), octave=2, unison=r.choice([1, 3]), detune=10)
    p.filt(0, "lp24", cutoff=r.uniform(800, 2000), res=0.3, env=0.35)
    p.env(1, a=0.0, d=0.15, s=0.0, r=0.1)
    p.env(0, a=0.001, d=0.25, s=0.2, r=0.15)
    p.set("arp_mode", 1).set("arp_pattern", pattern).set("arp_rate", r.choice([5, 5, 4, 8]))
    p.set("arp_octaves", r.choice([1, 2, 3])).set("arp_gate", r.uniform(0.3, 0.7)).set("arp_swing", r.choice([0.0, 0.0, 0.3]))
    p.standard_macros(r, "Gate", grit_dest=d_filt(0, "res"))
    p.sends(reverb=0.2, delay=0.2)

def seq_classic(p, r):
    p.desc = "Step-sequenced line with probability, ratchets and lane-driven filter. Hold one key to transpose."
    p.osc(0, "analog", r.choice(["saw", "square"]), octave=r.choice([1, 2]))
    p.filt(0, r.choice(["lp24", "acid", "diode"]), cutoff=r.uniform(300, 900), res=0.5, env=0.3)
    p.env(1, a=0.0, d=0.15, s=0.0, r=0.1)
    p.env(0, a=0.001, d=0.25, s=0.3, r=0.1)
    p.set("arp_mode", 2).set("seq_length", r.choice([8, 12, 16, 16, 32])).set("seq_rate", 5).set("seq_swing", r.choice([0.0, 0.2, 0.4]))
    p.assets["seq_pattern"] = seq_pattern(r, ratchets=True, slides=True)
    p.mod("seq1", d_filt(0, "cutoff"), 0.3)
    p.set("voice_mode", 1).set("glide_time", 0.04).set("glide_mode", 1)
    p.standard_macros(r, "Accent", grit_dest=d_filt(0, "res"))
    p.sends(delay=0.2)

def misc_organ(p, r):
    p.desc = "Drawbar-style organ: stacked sine/square footages with a rotary-ish chorus and fast key click."
    p.osc(0, "analog", "sine", octave=2)
    p.osc(1, "analog", "sine", octave=3, level=0.5)
    p.osc(2, "analog", "square", octave=4, level=0.2)
    p.osc(3, "analog", "sine", octave=2, coarse=19, level=0.25)
    p.env(0, a=0.002, d=0.1, s=1.0, r=0.06, punch=0.2)
    p.filt(0, "lp12", cutoff=8000)
    p.lfo(1, "sine", rate=6.0)
    p.standard_macros(r, "Rotor", move_dest=D_AMPPAN, grit_dest=D_FXMIX)
    p.fx("chorus", [0.55, 0.6, 0.3, 0.2, 0.67, 1.0]); p.sends(reverb=0.15)

def misc_chop(p, r):
    p.desc = "Vocal-chop-like stab: vowel wavetable with formant warp, fast gate envelope and a syncopated position LFO."
    p.osc(0, "wt", table="vowel", wtpos=r.uniform(0, 1), octave=3, warp="formant", warp_amt=r.uniform(0.1, 0.4))
    p.filt(0, "formant", cutoff=r.uniform(200, 3000), res=0.4)
    p.lfo(0, "sh", sync=True, div="1/8")
    p.mod("lfo1", d_osc(0, "wtpos"), 0.5)
    p.env(0, a=0.001, d=r.uniform(0.15, 0.3), s=0.0, r=0.1)
    p.standard_macros(r, "Vowel", move_dest=d_osc(0, "wtpos"), grit_dest=d_osc(0, "warp"))
    p.sends(reverb=0.2, delay=0.25)

def misc_stab(p, r):
    p.desc = "Chord stab: stacked saws in unison voice mode, snappy filter envelope and plate reverb."
    p.osc(0, "analog", "saw", octave=2, unison=3, detune=15)
    p.set("voice_mode", 3).set("stack_voices", r.choice([2, 3, 4])).set("stack_detune", r.uniform(10, 30))
    p.filt(0, "lp24", cutoff=r.uniform(600, 1500), res=0.25, env=0.4)
    p.env(1, a=0.0, d=0.25, s=0.0, r=0.2)
    p.env(0, a=0.001, d=0.4, s=0.0, r=0.3)
    p.standard_macros(r, "Snap", grit_dest=d_filt(0, "res"))
    p.sends(reverb=0.3, rv=(0.4, 0.4, 0.3, 0.0, 0.34, 0.8))

ARCHETYPES = {
    "Bass": [(bass_reese, ["dubstep", "basshouse"]), (bass_growl, ["dubstep"]), (bass_wobble, ["dubstep"]),
             (bass_ukg, ["ukg", "basshouse"]), (bass_808, ["hyper", "lofi"]), (bass_basshouse, ["basshouse", "ukg"]),
             (bass_fm, ["dubstep", "hyper"]), (bass_acid, ["basshouse"]), (bass_screech, ["dubstep"])],
    "Leads": [(lead_supersaw, ["melodic", "bigroom"]), (lead_chip, ["chip", "hyper"]), (lead_hypervox, ["hyper"]),
              (lead_sync, ["dubstep", "bigroom"]), (lead_anime, ["cinematic", "melodic"]), (lead_bigroom, ["bigroom"])],
    "Pads": [(pad_supersaw, ["melodic", "cinematic"]), (pad_scan, ["organic", "cinematic"]), (pad_sidechain, ["ukg", "melodic"]),
             (pad_lofi, ["lofi", "ukg"]), (pad_shimmer, ["cinematic"])],
    "Plucks & Keys": [(pluck_basic, ["melodic", "bigroom"]), (pluck_kalimba, ["organic"]), (keys_rhodes, ["ukg", "lofi"]),
                      (keys_lofipiano, ["lofi", "ukg"]), (pluck_bell, ["cinematic", "organic"])],
    "Winds": [(wind_breathy, ["organic", "cinematic"]), (wind_brass, ["cinematic"])],
    "Ambient": [(ambient_texture, ["organic", "cinematic"]), (pad_scan, ["organic"]), (pad_shimmer, ["cinematic"])],
    "Drones": [(drone_dark, ["cinematic"]), (ambient_texture, ["organic"])],
    "FX": [(fx_riser, ["bigroom", "cinematic"]), (fx_impact, ["cinematic"]), (fx_zap, ["hyper", "chip"]), (fx_glitch, ["hyper"])],
    "Arps & Sequences": [(lambda p, r: arp_classic(p, r, r.choice([0, 1, 2, 3, 4])), ["melodic", "chip"]), (seq_classic, ["basshouse", "hyper"])],
    "Misc": [(misc_organ, ["basshouse", "lofi"]), (misc_chop, ["ukg", "hyper"]), (misc_stab, ["melodic", "ukg"])],
}

# Hand-designed heroes: (category, name, archetype, styles, overrides, description)
HEROES = [
    ("Bass", "Rubber Gutter Bass", bass_ukg, ["ukg", "basshouse"], {"glide_time": 0.07, "filter1_cutoff": 420.0, "filter1_res": 0.38}, None),
    ("Bass", "Basement Reese Engine", bass_reese, ["dubstep"], {"filter1_cutoff": 650.0}, None),
    ("Bass", "Feral Talkbox Growl", bass_growl, ["dubstep"], {}, None),
    ("Bass", "Thunder 808 Gravel", bass_808, ["hyper", "lofi"], {}, None),
    ("Leads", "Glass Hallway", lead_anime, ["cinematic", "melodic"], {}, None),
    ("Leads", "Festival Skyline", lead_bigroom, ["bigroom"], {}, None),
    ("Leads", "Pixel Arcade Hero", lead_chip, ["chip", "hyper"], {}, None),
    ("Pads", "Aurora Cathedral", pad_shimmer, ["cinematic"], {}, None),
    ("Pads", "Night Bus Garage Pad", pad_sidechain, ["ukg", "melodic"], {}, None),
    ("Plucks & Keys", "Raindrop Kalimba", pluck_kalimba, ["organic"], {}, None),
    ("Plucks & Keys", "Velvet Tine Keys", keys_rhodes, ["ukg", "lofi"], {}, None),
    ("Winds", "Bamboo Breath", wind_breathy, ["organic"], {}, None),
    ("Ambient", "Snowfall Archive", ambient_texture, ["organic", "cinematic"], {}, None),
    ("Drones", "Leviathan Engine Room", drone_dark, ["cinematic"], {}, None),
    ("FX", "Eight Bar Uplift", fx_riser, ["bigroom"], {"mseg_time": 16.0}, None),
    ("Arps & Sequences", "Neon Conveyor", seq_classic, ["basshouse"], {}, None),
    ("Misc", "Late Night Organ Stab", misc_organ, ["basshouse", "ukg"], {}, None),
]

def unique_name(rng, category, used):
    for _ in range(1000):
        n = f"{rng.choice(ADJ)} {rng.choice(NOUN[category])}"
        if n not in used:
            used.add(n)
            return n
    raise RuntimeError("name space exhausted")

def build_all(seed=2026):
    rng = random.Random(seed)
    used = set()
    presets = []
    for cat, total in COUNTS.items():
        made = 0
        for hcat, name, fn, styles, overrides, desc in HEROES:
            if hcat != cat:
                continue
            r = random.Random(zlib.crc32(name.encode()))
            p = Preset(name, cat, styles + ["Hero"], "")
            fn(p, r)
            for k, v in overrides.items():
                p.set(k, v)
            p.desc = (desc or p.desc) + " (Hand-designed hero preset.)"
            used.add(name)
            presets.append(p)
            made += 1
        archetypes = ARCHETYPES[cat]
        while made < total:
            fn, styles = archetypes[made % len(archetypes)]
            r = random.Random(rng.randrange(1 << 30))
            p = Preset(unique_name(r, cat, used), cat, styles, "")
            fn(p, r)
            # gentle global variation so siblings differ
            p.set("analog_drift", round(r.uniform(0.0, 0.3), 3))
            if r.random() < 0.3:
                p.set("voice_random", round(r.uniform(0.05, 0.3), 3))
            presets.append(p)
            made += 1
    return presets

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="presets/factory")
    ap.add_argument("--bundle", default="resources/factory_presets.json")
    args = ap.parse_args()
    presets = build_all()
    assert len(presets) == sum(COUNTS.values()), len(presets)
    for p in presets:
        for t in p.tags:
            assert not re.search(r"(fred|virji|zedd|skrillex|hamdi|four tet|ninajirachi|frost children|underscores|porter)", t, re.I)
    bundle = []
    for p in presets:
        j = p.to_json()
        bundle.append(j)
        d = os.path.join(args.out, p.category.replace("&", "and").replace(" ", "_"))
        os.makedirs(d, exist_ok=True)
        with open(os.path.join(d, re.sub(r"[^A-Za-z0-9 _-]", "", p.name) + ".mbxpreset"), "w") as f:
            json.dump(j, f, indent=1, sort_keys=True)
    os.makedirs(os.path.dirname(args.bundle), exist_ok=True)
    with open(args.bundle, "w") as f:
        json.dump(bundle, f, separators=(",", ":"))
    counts = {}
    for p in presets:
        counts[p.category] = counts.get(p.category, 0) + 1
    print("wrote", len(presets), "presets:", counts)

if __name__ == "__main__":
    main()
