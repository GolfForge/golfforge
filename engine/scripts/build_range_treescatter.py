# build_range_treescatter.py
#
# RANGE-ONLY tree-scatter profile: FEWER + SHORTER trees than the course.
#
# WHY: the practice range's perimeter `Trees` strips sit right next to the player
# viewport, so the course density + full tree height (which look mint out on the
# open course) tower over the tee and tank GPU via overdraw. This authors the
# range's OWN graph (/Game/PCG/PCG_TreeScatter_Range) at a lower density + shorter
# height and delegates the actual graph construction to build_pcg_treescatter.py.
#
# COURSE-SAFE BY CONSTRUCTION: this only ever writes the range sibling graph; the
# course graph (/Game/PCG/PCG_TreeScatter) is never opened or saved, and the shared
# builder file is NOT modified -- we just pass it range overrides via globals(). So
# nothing here can change how the course trees look.
#
# Run inside the UE5.7 editor Python interpreter (UnrealClaudeMCP execute_unreal_python):
#
#   exec(compile(open(r"<repo>\engine\scripts\build_range_treescatter.py",
#                encoding="utf-8").read(), "build_range_treescatter.py", "exec"))
#
# That rebuilds the GRAPH. To then push it into the level, regenerate the range
# volume (scatter_range_trees.py) and save PracticeRange.umap (operator-gated, per
# project convention -- this script never saves a umap).
#
# TUNABLES: DENSITY_PPSM (trees/m^2), _SIZE (uniform scale of the whole tree vs the
# course), and _MIN_FRAC (lifts the LOW end of each size band -- 0.0 = full course
# variance, 1.0 = all at max). Current: 0.075 ppsm, _SIZE 0.50 (~half the course --
# shorter AND proportionally narrower, not squished), _MIN_FRAC 0.0 (full random size
# spread, so the trees don't read as same-height bands; at this _SIZE even the small
# end is a real tree, not a sapling). The old range 0.35 / full height read as "walls
# of trees" on the thin perimeter strips; these frame the tee instead. Easy dials.
#
# bridge note: build feedback goes through unreal.log() under LogPython.

import os
import unreal

GRAPH_PATH   = "/Game/PCG/PCG_TreeScatter_Range"
DENSITY_PPSM = 0.075   # thin perimeter strips; the old 0.35 read as "walls of trees"

# _BASE mirrors build_pcg_treescatter.py's course SPECIES table (full size). The
# range SPECIES below is _BASE with BOTH width (x,y) and height (z) multiplied by
# _SIZE -- a UNIFORM downscale of the whole asset, so shorter trees stay in
# proportion (scaling z alone made them look squished). Keep _BASE's non-scale
# fields in sync if the course default changes (dir/prefix/variants/seed).
_SIZE = 0.50
_BASE = {
    "Birch": {
        "dir": "/Game/Megaplant_Library/Tree_Silver_Birch/Tree_Silver_Birch_01/",
        "prefix": "SK_Silver_Birch_01_",
        "variants": [("A", 1), ("B", 1), ("C", 1), ("D", 1)],
        "scale_min": (0.90, 0.90, 0.85), "scale_max": (1.15, 1.15, 1.25),
        "lean_deg": 3.0, "seed": 30303,
    },
    "Pine": {
        "dir": "/Game/Megaplant_Library/Tree_Baltic_Pine/Tree_Baltic_Pine_01/",
        "prefix": "Baltic_Pine_01_",
        "variants": [("A", 1), ("B", 1), ("C", 1), ("D", 1)],
        "scale_min": (0.80, 0.80, 1.05), "scale_max": (1.05, 1.05, 1.45),
        "lean_deg": 2.0, "seed": 40404,
    },
}
# Raise the band's low end toward its max (less variance), then uniform-scale by _SIZE.
_MIN_FRAC = 0.0   # 0.0 = full course variance; 0.5 = band runs mid->max; 1.0 = all at max
SPECIES = {}
for _name, _cfg in _BASE.items():
    _c = dict(_cfg)
    _smin, _smax = _cfg["scale_min"], _cfg["scale_max"]
    _eff_min = tuple(lo + (hi - lo) * _MIN_FRAC for lo, hi in zip(_smin, _smax))
    _c["scale_min"] = tuple(round(v * _SIZE, 4) for v in _eff_min)
    _c["scale_max"] = tuple(round(v * _SIZE, 4) for v in _smax)
    SPECIES[_name] = _c

# Self-locate the shared builder next to this file. project_dir() is
# <repo>/engine/Golfsim/, so ../scripts holds these orchestrators -- portable
# across machines, no hardcoded absolute path.
_scripts_dir = os.path.normpath(os.path.join(unreal.Paths.project_dir(), "..", "scripts"))
_builder = os.path.join(_scripts_dir, "build_pcg_treescatter.py")
unreal.log("BUILD_RANGE_TREESCATTER: %s @ %.4f ppsm, uniform size x%.2f (course graph untouched)"
           % (GRAPH_PATH, DENSITY_PPSM, _SIZE))
exec(compile(open(_builder, encoding="utf-8").read(), "build_pcg_treescatter.py", "exec"))
