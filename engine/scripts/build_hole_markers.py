# build_hole_markers.py
#
# Dev-only visual reference for hole tee + green positions, read from a course's
# courses/<id>/hole.geojson. For each `golf=hole` LineString in the matching
# course-name filter, drops a 60 m tall pillar at the tee (start) and the green
# (end), plus a floating 3D text label ("Black 8 / Green / par 3 hcp 14"). The
# obvious eyeball-check after re-cooking the course: are all 18 Black pillars on
# the landscape, or did the bbox clip some endpoints? (This is exactly how
# GOL-108 surfaced: GOL-85's verify pass showed 5 Black endpoints off the edge.)
#
# Mirrors the engine/scripts conventions:
#  - Idempotent: every run destroys prior HoleMarker_*/HoleLabel_* actors before
#    spawning fresh (no duplicates). Re-run any time the bbox or hole.geojson
#    changes.
#  - Transient by design: the umap is NOT saved by this script. The markers live
#    in the editor session only. Just-in-case belt-and-suspenders: every spawned
#    actor is flagged is_editor_only_actor=True so even an accidental Save All +
#    cook will strip them from shipping builds.
#  - Same lon/lat -> world affine as build_water_actors.py (verified GOL-85: the
#    landscape is default-stretched at scale=(100, 100, ue5_z_scale_pct); only Z
#    is data-driven; X/Y stay 100). If GOL-108's bbox widening lands, bump
#    BBOX_WGS84 in BOTH this file AND build_water_actors.py.
#
# Run in the UE5.7 editor Python interpreter via execute_unreal_python. THREE
# modes:
#
#   MODE BUILD (default) - spawn fresh markers, killing any priors:
#     exec(compile(open(r"<repo>\engine\scripts\build_hole_markers.py",
#       encoding="utf-8").read(),"build_hole_markers.py","exec"))
#
#   MODE BUILD with custom course filter (e.g. all three courses):
#     COURSE_FILTER=("Black","Green","Red")
#     exec(compile(open(r"<repo>\engine\scripts\build_hole_markers.py",
#       encoding="utf-8").read(),"build_hole_markers.py","exec"))
#
#   MODE CLEAR - destroy every HoleMarker_*/HoleLabel_* actor, spawn nothing:
#     HOLE_MARKERS_MODE="clear"
#     exec(compile(open(r"<repo>\engine\scripts\build_hole_markers.py",
#       encoding="utf-8").read(),"build_hole_markers.py","exec"))
#
# bridge note: execute_unreal_python returns output:None. All feedback via
# unreal.log() under LogPython; read with get_log_lines.

import json
import os

import unreal

# ---------------------------------------------------------------- parameters
COURSE_ID  = "golfforge-demo-black"
LEVEL_HINT = "GolfForgeDemoBlack"

# Default: just the headline course. Override via `COURSE_FILTER=(...)` before
# exec. None == include every `golf=hole` Feature regardless of course-name tag.
COURSE_FILTER = globals().get("COURSE_FILTER", ("Black",))

# Repo root from the UE project location (repo/engine/Golfsim -> repo).
_REPO_ROOT   = os.path.normpath(os.path.join(
    unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()),
    "..", ".."))
GEOJSON_PATH = globals().get("GEOJSON_PATH") or os.path.join(
    _REPO_ROOT, "courses", COURSE_ID, "hole.geojson")

# heightmap.json georeference (minlon, minlat, maxlon, maxlat). Must match
# build_water_actors.py BBOX_WGS84. Widened on 2026-06-01 (GOL-108) N + W
# after GOL-85's verify caught 5 Black endpoints off the prior landscape.
BBOX_WGS84       = (-73.4555, 40.7423, -73.4345, 40.7571)
WORLD_HALF_XY_CM = 100800.0   # landscape default-stretched, see GOL-85
WORLD_ORIGIN_XY  = (0.0, 0.0)
FLIP_X, FLIP_Y, SWAP_XY = False, True, False   # verified against painted water

# Vertical line-trace span for landscape height (matches build_water_actors.py).
TRACE_TOP_Z_CM = 50000.0
TRACE_BOT_Z_CM = -50000.0

# Pillar geometry (cm). 60 m tall, 2 m thick -> visible from any editor camera
# height across the ~2 km course.
PILLAR_HEIGHT_CM = 6000.0
PILLAR_THICK_CM  = 200.0
# Text floats ~5 m above the pillar top, scaled up so it's readable from
# distance (TextRender world_size=800 + actor scale=5 -> ~40 m letters).
TEXT_HEIGHT_ABOVE_PILLAR_CM = 500.0
TEXT_WORLD_SIZE = 800.0
TEXT_ACTOR_SCALE = 5.0

CUBE_MESH_PATH = "/Engine/BasicShapes/Cube"

LABEL_PREFIX_MARKER = "HoleMarker_"    # -> HoleMarker_<course>_<ref>_<Tee|Green>
LABEL_PREFIX_TEXT   = "HoleLabel_"     # -> HoleLabel_<course>_<ref>_<Tee|Green>


def _log(msg):
    unreal.log("HOLE_MARKERS: " + str(msg))


def _eas():
    return unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def _editor_world():
    return unreal.get_editor_subsystem(
        unreal.UnrealEditorSubsystem).get_editor_world()


# ---------------------------------------------------------------- georef
def _lonlat_to_world_xy(lon, lat):
    minlon, minlat, maxlon, maxlat = BBOX_WGS84
    u = (lon - minlon) / (maxlon - minlon)
    v = (lat - minlat) / (maxlat - minlat)
    ux = (1.0 - u) if FLIP_X else u
    vy = (1.0 - v) if FLIP_Y else v
    if SWAP_XY:
        ux, vy = vy, ux
    x = WORLD_ORIGIN_XY[0] + (ux * 2.0 - 1.0) * WORLD_HALF_XY_CM
    y = WORLD_ORIGIN_XY[1] + (vy * 2.0 - 1.0) * WORLD_HALF_XY_CM
    return float(x), float(y)


def _ground_z(world_x, world_y):
    """Landscape Z (cm) under (x,y). Returns 0.0 for off-landscape clean miss
    so the pillar still spawns (visibly hanging in the air = the signal that
    this hole's endpoint is OFF the current bbox)."""
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


# ---------------------------------------------------------------- helpers
def _set_editor_only(actor):
    """Mark spawned markers EditorOnly so even an accidental Save All + cook
    strips them from shipping builds. UPROPERTY name in 5.7 is
    `is_editor_only_actor` (snake_case of `bIsEditorOnlyActor`)."""
    try:
        actor.set_editor_property("is_editor_only_actor", True)
        return True
    except Exception as exc:
        _log("WARNING set_editor_only on %s failed: %s"
             % (actor.get_actor_label(), str(exc)[:80]))
        return False


def _kill_prior():
    eas = _eas()
    killed = 0
    for a in list(eas.get_all_level_actors()):
        try:
            lbl = a.get_actor_label()
        except Exception:
            continue
        if lbl.startswith(LABEL_PREFIX_MARKER) or lbl.startswith(LABEL_PREFIX_TEXT):
            try:
                eas.destroy_actor(a)
                killed += 1
            except Exception:
                continue
    return killed


def _spawn_pillar(x, y, ground_z_cm, label, cube_mesh):
    eas = _eas()
    pillar_z = ground_z_cm + PILLAR_HEIGHT_CM * 0.5
    actor = eas.spawn_actor_from_class(
        unreal.StaticMeshActor,
        unreal.Vector(x, y, pillar_z),
        unreal.Rotator(0.0, 0.0, 0.0))
    actor.set_actor_label(label)
    smc = actor.static_mesh_component
    smc.set_static_mesh(cube_mesh)
    smc.set_mobility(unreal.ComponentMobility.MOVABLE)
    actor.set_actor_scale3d(unreal.Vector(
        PILLAR_THICK_CM / 100.0,
        PILLAR_THICK_CM / 100.0,
        PILLAR_HEIGHT_CM / 100.0))
    _set_editor_only(actor)
    return actor


def _spawn_label(x, y, ground_z_cm, label, text):
    eas = _eas()
    text_z = ground_z_cm + PILLAR_HEIGHT_CM + TEXT_HEIGHT_ABOVE_PILLAR_CM
    actor = eas.spawn_actor_from_class(
        unreal.TextRenderActor,
        unreal.Vector(x, y, text_z),
        unreal.Rotator(0.0, 0.0, 0.0))
    actor.set_actor_label(label)
    trc = actor.text_render
    trc.set_text(text)
    trc.set_horizontal_alignment(unreal.HorizTextAligment.EHTA_CENTER)
    trc.set_vertical_alignment(unreal.VerticalTextAligment.EVRTA_TEXT_CENTER)
    trc.set_editor_property("world_size", TEXT_WORLD_SIZE)
    actor.set_actor_scale3d(unreal.Vector(
        TEXT_ACTOR_SCALE, TEXT_ACTOR_SCALE, TEXT_ACTOR_SCALE))
    _set_editor_only(actor)
    return actor


# ---------------------------------------------------------------- modes
def _build():
    _log("=== BUILD START (level hint: %s, course filter: %s) ==="
         % (LEVEL_HINT, COURSE_FILTER if COURSE_FILTER else "ALL"))
    killed = _kill_prior()
    _log("killed %d prior marker/label actor(s)" % killed)

    if not os.path.exists(GEOJSON_PATH):
        _log("FATAL: hole.geojson not found at %s" % GEOJSON_PATH)
        return
    with open(GEOJSON_PATH, "r", encoding="utf-8") as fh:
        gj = json.load(fh)

    cube = unreal.load_asset(CUBE_MESH_PATH)
    if cube is None:
        _log("FATAL: cube mesh not found at %s" % CUBE_MESH_PATH)
        return

    spawned_holes = 0
    spawned_actors = 0
    by_course = {}
    for feat in gj.get("features", []):
        props = feat.get("properties") or {}
        tags  = props.get("osm_tags") or {}
        course = tags.get("golf:course:name", "")
        if COURSE_FILTER is not None and course not in COURSE_FILTER:
            continue
        ref = tags.get("ref", "?")
        par = tags.get("par", "?")
        hcp = tags.get("handicap", "?")
        coords = (feat.get("geometry") or {}).get("coordinates") or []
        if not coords:
            continue
        spawned_holes += 1
        by_course[course] = by_course.get(course, 0) + 1

        for kind, (lon, lat) in (("Tee", coords[0]),
                                 ("Green", coords[-1])):
            x, y = _lonlat_to_world_xy(float(lon), float(lat))
            z    = _ground_z(x, y)
            base = "%s_%s_%s_%s" % (course or "Unk", ref, kind, "")
            marker_label = LABEL_PREFIX_MARKER + base.rstrip("_")
            label_label  = LABEL_PREFIX_TEXT   + base.rstrip("_")
            text = "%s %s\\n%s\\npar %s hcp %s" % (course, ref, kind, par, hcp)
            _spawn_pillar(x, y, z, marker_label, cube)
            _spawn_label(x, y, z, label_label, text)
            spawned_actors += 2

    summary = ", ".join("%s=%d" % (k or "Unk", v)
                        for k, v in sorted(by_course.items()))
    _log("spawned %d hole(s) [%s] -> %d marker+label actors total "
         "(2 per hole-end x 2 ends)" % (spawned_holes, summary, spawned_actors))
    _log("Markers are EditorOnly + transient: do NOT save the umap with them "
         "live; re-run anytime to refresh.")
    _log("=== BUILD DONE ===")


def _clear():
    _log("=== CLEAR START ===")
    killed = _kill_prior()
    _log("destroyed %d marker/label actor(s)" % killed)
    _log("=== CLEAR DONE ===")


_mode = globals().get("HOLE_MARKERS_MODE", "build")
if _mode == "clear":
    _clear()
else:
    _build()
