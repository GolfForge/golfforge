# place_ambient_sfx.py
#
# GOL-166: the ambient SFX layer. Both maps were dead silent; this lays down an
# outdoor bed -- birdsong + a wind breeze + a low distant murmur -- so loading
# either map instantly reads as "outside." Pure content + placement, no C++.
#
# Two halves, both idempotent:
#   1) IMPORT (map-independent): import the CC0 .ogg sources from
#      Content/Audio/Ambient/_src/ into looping USoundWaves under /Game/Audio/
#      Ambient, route them through a shared SC_Ambient SoundClass (one master
#      ambient level + the seam to duck under future gameplay SFX), and author a
#      SA_AmbientBird SoundAttenuation (so bird zones fall off with distance).
#   2) PLACE (map-aware): spawn AAmbientSound actors for whichever map is open --
#      PracticeRange or GolfForgeDemoBlack -- bird zones spatialized (vary as you
#      move), wind + murmur as un-spatialized global beds mixed low.
#
# Mirrors engine/scripts conventions (see place_range_props.py):
#   - Idempotent: destroys prior Ambient_* actors before spawning; re-import uses
#     replace_existing. Audio sources are CC0 (BigSoundBank) -- provenance in
#     ATTRIBUTION.md.
#   - PERSISTENT actors: the script does NOT save. The operator saves
#     PracticeRange.umap / GolfForgeDemoBlack.umap or the actors are lost.
#
# Looping note: USoundWave has no `looping` in dir() but get/set_editor_property(
# "looping", True) works and persists -- an AmbientSound playing a looping wave
# loops without a SoundCue (SoundCue node graphs aren't constructible from Python
# in 5.7: no construct_sound_node).
#
# Run in the UE5.7 editor Python interpreter via execute_unreal_python. Open the
# target map first. Modes:
#   DEFAULT  - import (idempotent) + place for the open map:
#     exec(compile(open(r"<repo>\engine\scripts\place_ambient_sfx.py",
#       encoding="utf-8").read(),"place_ambient_sfx.py","exec"))
#   IMPORT ONLY (no placement, any map open):
#     AMBIENT_MODE="import"
#   CLEAR    - destroy every Ambient_* actor, place nothing:
#     AMBIENT_MODE="clear"
#   Force-place even if the open map is neither target:  AMBIENT_FORCE=True
#
# bridge note: execute_unreal_python returns output:None. All feedback via
# unreal.log() under LogPython; read with get_log_lines.

import unreal

# ---------------------------------------------------------------- parameters
LEVEL_RANGE  = "PracticeRange"
# Prefix-match so every demo course level qualifies (GolfForgeDemoBlack/Blue/Red/
# Green/Yellow). Bird zones are placed on each level's own scattered trees, so the
# same course path serves them all.
LEVEL_COURSE = "GolfForgeDemo"

AUDIO_DIR = "/Game/Audio/Ambient"
SRC_DIR   = (r"C:\Users\pucho\code\golfsim\engine\Golfsim"
             r"\Content\Audio\Ambient\_src")

# CC0 clips (BigSoundBank): (source file, USoundWave name, role).
# 3 bird variants so neighbouring zones differ; 1 wind bed; 1 murmur bed.
CLIPS = [
    ("bird_forest.ogg",      "SW_AmbientBird_Forest",      "bird"),
    ("bird_countryside.ogg", "SW_AmbientBird_Countryside", "bird"),
    ("bird_forest_edge.ogg", "SW_AmbientBird_ForestEdge",  "bird"),
    ("wind_trees.ogg",       "SW_AmbientWind_Trees",       "wind"),
    # (distant-traffic "murmur" bed removed -- it read as cars/road noise on the
    # course. Birds + wind only now. GOL-166 follow-up.)
]

SC_AMBIENT_PATH = AUDIO_DIR + "/SC_Ambient"
SA_BIRD_PATH    = AUDIO_DIR + "/SA_AmbientBird"

# Mixed low so the (future) launch-monitor + ball SFX sit clearly on top.
VOL = {"bird": 0.90, "wind": 0.35, "murmur": 0.18}

# Beds (wind + distant murmur) are global/un-spatialized. Toggle off to audition
# the spatialized bird zones alone: set AMBIENT_BEDS=False before exec.
PLACE_BEDS = globals().get("AMBIENT_BEDS", True)

# Bird attenuation (cm), LINEAR: full volume within START, then linear to silence
# over FALLOFF. Tuned to the target curve: 100% at the tree (<=15 m), 50% at
# 70 m, silent by ~125 m -- gentle enough that mid-forest spots between emitters
# don't drop out. (50% point = START + FALLOFF/2 = 15 + 55 = 70 m.)
BIRD_FALLOFF_START_CM = 1500.0    # ~15 m full volume
BIRD_FALLOFF_DIST_CM  = 11000.0   # ~110 m falloff -> 50% at 70 m, silent ~125 m

# Range geometry (matches place_range_props.py / GolfRangeHUD.cpp).
CM_PER_YD = 91.44
RANGE_BIRD_Y_CM = 5000.0          # off the fairway, into the tree wall
# (yards downrange, side) for the range bird zones.
RANGE_BIRDS = [(100, -1), (200, +1), (300, -1)]
RANGE_BED_YD = 150                # where the global beds nominally sit (X only)

# Course bird-zone ring (cm) around the playable interior, out toward the
# perimeter trees; 8 zones so birdsong shifts as you move between holes.
COURSE_RING_R_CM = 60000.0
COURSE_RING_N    = 8

TRACE_TOP_Z_CM = 50000.0
TRACE_BOT_Z_CM = -50000.0
BIRD_Z_ABOVE_CM = 500.0           # lift bird emitters off the deck
BED_Z_CM        = 1500.0          # beds are 2D; Z is cosmetic

LABEL_PREFIX = "Ambient_"
LAYER_NAME   = "AmbientSFX"


def _log(msg):
    unreal.log("AMBIENT_SFX: " + str(msg))


def _eas():
    return unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def _layers():
    return unreal.get_editor_subsystem(unreal.LayersSubsystem)


def _editor_world():
    return unreal.get_editor_subsystem(
        unreal.UnrealEditorSubsystem).get_editor_world()


def _ground_z(world_x, world_y):
    """Landscape Z (cm) under (x,y); 0.0 on a clean miss. (place_range_props.py)"""
    world = _editor_world()
    start = unreal.Vector(world_x, world_y, TRACE_TOP_Z_CM)
    end   = unreal.Vector(world_x, world_y, TRACE_BOT_Z_CM)
    for chan in (unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,
                 unreal.TraceTypeQuery.TRACE_TYPE_QUERY2):
        try:
            hit = unreal.SystemLibrary.line_trace_single(
                world, start, end, chan, True, [],
                unreal.DrawDebugTrace.NONE, True)
            if hit is None:
                continue
            t = hit.to_tuple()
            if t[0]:
                return float(t[5].z)
        except Exception:
            continue
    return 0.0


def _tee_x_cm():
    """Range tee origin X (cm) from the PlayerStart; lane -X end as fallback."""
    for a in _eas().get_all_level_actors():
        try:
            if "PlayerStart" in a.get_class().get_name():
                return float(a.get_actor_location().x)
        except Exception:
            continue
    return -18000.0


# ---------------------------------------------------------------- assets
def _import_waves():
    """Import every CC0 .ogg -> a looping USoundWave routed through SC_Ambient.
    Idempotent (replace_existing). Returns {sound_wave_name: USoundWave}."""
    at = unreal.AssetToolsHelpers.get_asset_tools()
    eal = unreal.EditorAssetLibrary
    sc = _ensure_sound_class()
    out = {}
    for fname, name, _role in CLIPS:
        src = SRC_DIR + "\\" + fname
        task = unreal.AssetImportTask()
        task.set_editor_property("filename", src)
        task.set_editor_property("destination_path", AUDIO_DIR)
        task.set_editor_property("destination_name", name)
        task.set_editor_property("automated", True)
        task.set_editor_property("replace_existing", True)
        task.set_editor_property("save", True)
        at.import_asset_tasks([task])
        path = "%s/%s" % (AUDIO_DIR, name)
        sw = eal.load_asset(path)
        if sw is None:
            _log("FATAL: import failed for %s" % path)
            continue
        sw.set_editor_property("looping", True)          # AmbientSound -> seamless loop
        if sc is not None:
            sw.set_editor_property("sound_class_object", sc)
        eal.save_asset(path)
        out[name] = sw
        _log("imported %s (looping=%s, %.1fs)"
             % (name, sw.get_editor_property("looping"),
                sw.get_editor_property("duration")))
    return out


def _ensure_sound_class():
    eal = unreal.EditorAssetLibrary
    sc = unreal.load_asset(SC_AMBIENT_PATH)
    if sc is None:
        sc = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            "SC_Ambient", AUDIO_DIR, unreal.SoundClass, unreal.SoundClassFactory())
        eal.save_asset(SC_AMBIENT_PATH)
        _log("authored %s" % SC_AMBIENT_PATH)
    return sc


def _ensure_bird_attenuation():
    eal = unreal.EditorAssetLibrary
    sa = unreal.load_asset(SA_BIRD_PATH)
    if sa is None:
        sa = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            "SA_AmbientBird", AUDIO_DIR, unreal.SoundAttenuation,
            unreal.SoundAttenuationFactory())
    st = sa.get_editor_property("attenuation")
    st.set_editor_property("attenuate", True)
    st.set_editor_property("spatialize", True)
    st.set_editor_property("distance_algorithm", unreal.AttenuationDistanceModel.LINEAR)
    st.set_editor_property("attenuation_shape", unreal.AttenuationShape.SPHERE)
    # sphere: extents.X = inner radius (full volume); falloff_distance beyond it.
    st.set_editor_property("attenuation_shape_extents",
                           unreal.Vector(BIRD_FALLOFF_START_CM, 0.0, 0.0))
    st.set_editor_property("falloff_distance", BIRD_FALLOFF_DIST_CM)
    sa.set_editor_property("attenuation", st)
    eal.save_asset(SA_BIRD_PATH)
    return sa


# ---------------------------------------------------------------- placement
def _assign_layer(actor):
    try:
        _layers().add_actor_to_layer(actor, LAYER_NAME)
    except Exception as exc:
        _log("WARNING add_actor_to_layer on %s failed: %s"
             % (actor.get_actor_label(), str(exc)[:80]))


def _kill_prior():
    eas = _eas()
    killed = 0
    for a in list(eas.get_all_level_actors()):
        try:
            lbl = a.get_actor_label()
        except Exception:
            continue
        if lbl.startswith(LABEL_PREFIX):
            try:
                eas.destroy_actor(a)
                killed += 1
            except Exception:
                continue
    return killed


def _spawn_ambient(wave, x, y, z, label, volume, attenuation):
    """Spawn an AAmbientSound. attenuation=None -> un-spatialized global bed."""
    eas = _eas()
    actor = eas.spawn_actor_from_class(
        unreal.AmbientSound, unreal.Vector(x, y, z), unreal.Rotator(0, 0, 0))
    actor.set_actor_label(label)
    ac = actor.get_editor_property("audio_component")
    ac.set_sound(wave)
    ac.set_volume_multiplier(volume)
    ac.set_auto_activate(True)
    if attenuation is not None:
        ac.set_editor_property("attenuation_settings", attenuation)
        ac.set_editor_property("allow_spatialization", True)
    else:
        ac.set_editor_property("allow_spatialization", False)  # 2D global bed
    _assign_layer(actor)
    return actor


def _sample_tree_points(max_samples=600):
    """World (x,y) of scattered PCG trees (InstancedSkinnedMeshComponent
    instances) in the open level, strided to ~max_samples points. [] if none.
    Skinned ISM: count via instance_data, transform via export_text() regex
    (get_instance_transform doesn't work on skinned ISM -- see cookbook)."""
    import re
    datas = []
    total = 0
    for a in _eas().get_all_level_actors():
        try:
            comps = a.get_components_by_class(unreal.InstancedSkinnedMeshComponent)
        except Exception:
            continue
        for c in comps:
            try:
                d = c.get_editor_property("instance_data")
                datas.append(d)
                total += len(d)
            except Exception:
                continue
    if total == 0:
        return []
    stride = max(1, total // max_samples)
    pts = []
    for d in datas:
        for i in range(0, len(d), stride):
            try:
                txt = d[i].get_editor_property("transform").export_text()
            except Exception:
                continue
            m = re.search(r"Translation=\(X=(-?[0-9.]+),Y=(-?[0-9.]+)", txt)
            if m:
                pts.append((float(m.group(1)), float(m.group(2))))
    return pts


def _spread_angular(pts, n):
    """Up to n tree points spread around the centroid (one per angular sector,
    median radius) so the bird zones ring the perimeter on real trees."""
    import math
    if not pts:
        return []
    cx = sum(p[0] for p in pts) / len(pts)
    cy = sum(p[1] for p in pts) / len(pts)
    sectors = {}
    for (x, y) in pts:
        s = int((math.atan2(y - cy, x - cx) + math.pi) / (2.0 * math.pi) * n) % n
        sectors.setdefault(s, []).append((math.hypot(x - cx, y - cy), x, y))
    out = []
    for s in range(n):
        lst = sectors.get(s)
        if lst:
            lst.sort()
            _r, x, y = lst[len(lst) // 2]
            out.append((x, y))
    return out


def _spread_by_x(pts, n):
    """n tree points spread along +X (down the range lane)."""
    if not pts:
        return []
    s = sorted(pts)
    return [s[min(int((i + 0.5) / n * len(s)), len(s) - 1)] for i in range(n)]


def _place_bird_zones(waves, bird_atten, pts):
    birds = [nm for nm, _w in _by_role(waves, "bird")]
    n = 0
    for i, (x, y) in enumerate(pts):
        wave = waves[birds[i % len(birds)]]
        z = _ground_z(x, y) + BIRD_Z_ABOVE_CM
        _spawn_ambient(wave, x, y, z, "%sBird_%d" % (LABEL_PREFIX, i),
                       VOL["bird"], bird_atten)
        n += 1
    return n


def _place_range(waves, bird_atten):
    tee_x = _tee_x_cm()
    pts = _spread_by_x(_sample_tree_points(), len(RANGE_BIRDS))
    if pts:
        _log("range: %d bird zone(s) on sampled tree clusters" % len(pts))
    else:
        pts = [(tee_x + yd * CM_PER_YD, side * RANGE_BIRD_Y_CM)
               for yd, side in RANGE_BIRDS]
        _log("range: no tree instances; bird zones fall back to fixed tree-wall coords")
    n = _place_bird_zones(waves, bird_atten, pts)
    if PLACE_BEDS:
        n += _place_beds(waves, tee_x + RANGE_BED_YD * CM_PER_YD, 0.0)
    return n


def _place_course(waves, bird_atten):
    import math
    pts = _spread_angular(_sample_tree_points(), COURSE_RING_N)
    if pts:
        _log("course: %d bird zone(s) on sampled tree clusters" % len(pts))
    else:
        pts = [(COURSE_RING_R_CM * math.cos(2.0 * math.pi * i / COURSE_RING_N),
                COURSE_RING_R_CM * math.sin(2.0 * math.pi * i / COURSE_RING_N))
               for i in range(COURSE_RING_N)]
        _log("course: no tree instances; bird zones fall back to a %.0fm ring"
             % (COURSE_RING_R_CM / 100.0))
    n = _place_bird_zones(waves, bird_atten, pts)
    if PLACE_BEDS:
        n += _place_beds(waves, 0.0, 0.0)
    return n


def _place_beds(waves, x, y):
    """The global (un-spatialized) wind bed. (Distant-murmur bed removed.)"""
    n = 0
    for role in ("wind",):
        named = _by_role(waves, role)
        if not named:
            continue
        wave = named[0][1]
        _spawn_ambient(wave, x, y, BED_Z_CM,
                       "%s%s_Bed" % (LABEL_PREFIX, role.capitalize()),
                       VOL[role], None)
        n += 1
    return n


def _by_role(waves, role):
    return [(name, waves[name]) for _f, name, r in CLIPS if r == role and name in waves]


# ---------------------------------------------------------------- entry
def main():
    mode = globals().get("AMBIENT_MODE", "place")
    world = _editor_world()
    world_name = world.get_name() if world else "<none>"
    _log("=== START (mode=%s, level=%s) ===" % (mode, world_name))

    if mode == "clear":
        _log("cleared %d Ambient_* actor(s)" % _kill_prior())
        _log("=== CLEAR DONE ===")
        return

    waves = _import_waves()
    bird_atten = _ensure_bird_attenuation()
    _log("imported %d wave(s); SC_Ambient + SA_AmbientBird ready" % len(waves))
    if mode == "import":
        _log("=== IMPORT-ONLY DONE ===")
        return

    is_range  = LEVEL_RANGE.lower()  in world_name.lower()
    is_course = LEVEL_COURSE.lower() in world_name.lower()
    if not (is_range or is_course) and not globals().get("AMBIENT_FORCE", False):
        _log("ABORT placement: '%s' is neither %s nor %s. Open a target map, set "
             "AMBIENT_FORCE=True, or use AMBIENT_MODE='import'."
             % (world_name, LEVEL_RANGE, LEVEL_COURSE))
        return

    _log("killed %d prior Ambient_* actor(s)" % _kill_prior())
    placed = _place_range(waves, bird_atten) if is_range else _place_course(waves, bird_atten)
    try:
        _layers().editor_refresh_layer_browser()
    except Exception:
        pass
    _log("placed %d ambient actor(s) on %s" % (placed, world_name))
    _log("PERSISTENT: SAVE %s.umap or the actors are lost. Layer: '%s'."
         % (LEVEL_RANGE if is_range else LEVEL_COURSE, LAYER_NAME))
    _log("=== DONE ===")


main()
