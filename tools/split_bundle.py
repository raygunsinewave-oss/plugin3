#!/usr/bin/env python3
"""Writes the (level-matched) bundle back out to the individual preset files.

Pipeline:  generate_presets.py  ->  MysteryBoxTests --level-presets resources/factory_presets.json  ->  split_bundle.py
"""
import json, os, re, sys

bundle = sys.argv[1] if len(sys.argv) > 1 else "resources/factory_presets.json"
out = sys.argv[2] if len(sys.argv) > 2 else "presets/factory"
data = json.load(open(bundle))
for p in data:
    d = os.path.join(out, p["category"].replace("&", "and").replace(" ", "_"))
    os.makedirs(d, exist_ok=True)
    with open(os.path.join(d, re.sub(r"[^A-Za-z0-9 _-]", "", p["name"]) + ".mbxpreset"), "w") as f:
        json.dump(p, f, indent=1, sort_keys=True)
# compact the embedded copy again (the levelling tool writes it pretty-printed)
json.dump(data, open(bundle, "w"), separators=(",", ":"))
print("wrote", len(data), "preset files")
