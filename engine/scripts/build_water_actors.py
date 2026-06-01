# build_water_actors.py
#
# Reusable, idempotent constructor for golfsim water bodies (Milestone 0.9).
#
# APPROACH (pivoted 2026-05-16): one FLAT TRANSLUCENT WATER MESH per Feature
# in courses/<id>/water.geojson, georeferenced onto the course Landscape,
# at the median-shoreline water Z. This is how the golf-sim course-authoring
# ecosystem represents water in general - a visual +
# collision plane, NOT simulated fluid. The earlier UE Water-plugin path was
# abandoned: WaterBodyLake needs AWaterZone::MarkForRebuild (C++/editor-only,
# not Python-bindable in 5.7) to render, plus WaterZone/collision-profile
# friction - all dissolved by the flat-mesh approach. The lon/lat -> world
# affine was verified exact against the painted water (4 bbox corners hit
# the landscape corners to the cm; centroids land on the painted regions).
#
# Built with DynamicMeshActor + GeometryScript (no Water plugin, no plugin
# enable, no editor restart). Collision is generated from the mesh
# (ball-in-water / walk-blocking ready). Mesh actors are PERSISTENT level
# actors - this script does NOT save; the operator saves GolfForgeDemoBlack.umap
# afterward or the water is lost.
#
#   Run in the UE5.7 editor Python interpreter via execute_unreal_python:
#     exec(compile(open(r"<repo>\engine\scripts"
#       r"\build_water_actors.py",encoding="utf-8").read(),
#       "build_water_actors.py","exec"))
#
# bridge note: execute_unreal_python returns output:None. All feedback via
# unreal.log() under LogPython; read with get_log_lines. Idempotent: actors
# found-by-label are destroyed + rebuilt, never duplicated.

import json
import math
import os

import unreal

# ---------------------------------------------------------------- parameters
COURSE_ID        = "golfforge-demo-black"
# Repo root from the UE project location (repo/engine/Golfsim -> repo) so this
# runs for any contributor / checkout. Set GEOJSON_PATH before exec to override.
_REPO_ROOT       = os.path.normpath(os.path.join(
    unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()),
    "..", ".."))
GEOJSON_PATH     = globals().get("GEOJSON_PATH") or os.path.join(
    _REPO_ROOT, "courses", COURSE_ID, "water.geojson")
LEVEL_HINT       = "GolfForgeDemoBlack"
LANDSCAPE_LABEL_HINT = "Landscape"

# heightmap.json georeference (minlon, minlat, maxlon, maxlat). Bbox was widened
# east on 2026-05-31 (GOL-33) from maxlon -73.4374 -> -73.4350 to capture Black
# hole 8 on the eastern edge of the course.
BBOX_WGS84       = (-73.454, 40.7423, -73.435, 40.7549)
WORLD_HALF_XY_CM = 100800.0
WORLD_ORIGIN_XY  = (0.0, 0.0)

# Orientation (verified correct 2026-05-16: bbox corners map exactly to the
# landscape corners, centroids land on the painted water). Knobs kept for
# other courses / re-verification.
FLIP_X           = False
FLIP_Y           = True
SWAP_XY          = False

# Vertical line-trace span for landscape height sampling. Surface is
# ~ +/-2150 cm about world Z~5; wide span is strictly safer for a
# straight-down trace.
TRACE_TOP_Z_CM   = 50000.0
TRACE_BOT_Z_CM   = -50000.0
# Lake surface Z = median SHORELINE (ring-vertex) ground height + offset.
WATER_Z_OFFSET_CM = -20.0

LABEL_PREFIX     = "Water_"            # -> Water_<osm_way_id>
MIN_RING_VERTS   = 4                   # closed ring => >=4 pts
MIN_AREA_M2      = 25.0                # smaller polygons logged + skipped

MATERIAL_PATH    = "/Game/Materials/M_GolfsimWater"
WATER_COLOR      = (0.015, 0.12, 0.18)  # deep bluish-green
WATER_OPACITY    = 0.72
WATER_ROUGHNESS  = 0.08
COLLISION_PROFILE = "BlockAll"          # ball-in-water / walk-blocking ready


def _log(msg):
    unreal.log("WATER_BUILD: " + str(msg))


def _eas():
    return unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def _editor_world():
    return unreal.get_editor_subsystem(
        unreal.UnrealEditorSubsystem).get_editor_world()


def _find_actor_by_label(label):
    for a in _eas().get_all_level_actors():
        try:
            if a.get_actor_label() == label:
                return a
        except Exception:
            continue
    return None


def _find_actor_by_class_name(cls_substr):
    for a in _eas().get_all_level_actors():
        try:
            if cls_substr in a.get_class().get_name():
                return a
        except Exception:
            continue
    return None


# ---------------------------------------------------------------- geojson
def _load_features(path):
    """-> list of (osm_way_id, osm_tags, ring[(lon,lat)...])."""
    with open(path, "r", encoding="utf-8") as fh:
        gj = json.load(fh)
    out = []
    if gj.get("type") != "FeatureCollection":
        _log("FATAL: %s is not a FeatureCollection" % path)
        return out
    for i, feat in enumerate(gj.get("features", [])):
        props = feat.get("properties", {}) or {}
        wid = props.get("osm_way_id")
        tags = props.get("osm_tags", {}) or {}
        geom = feat.get("geometry", {}) or {}
        if geom.get("type") != "Polygon":
            _log("skip feature %d: geometry %r not Polygon"
                 % (i, geom.get("type")))
            continue
        if wid is None:
            _log("WARNING: feature %d has NO osm_way_id (data bug)" % i)
            wid = "idx%d" % i
        rings = geom.get("coordinates", [])
        if not rings or len(rings[0]) < MIN_RING_VERTS:
            _log("skip Water_%s: outer ring < %d verts"
                 % (wid, MIN_RING_VERTS))
            continue
        ring = [(float(c[0]), float(c[1])) for c in rings[0]]
        if ring[0] != ring[-1]:
            ring.append(ring[0])
        out.append((wid, tags, ring))
    return out


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


def _polygon_area_m2(ring_lonlat):
    if len(ring_lonlat) < 4:
        return 0.0
    mean_lat = sum(p[1] for p in ring_lonlat) / len(ring_lonlat)
    k = math.cos(math.radians(mean_lat))
    pts = [(lon * 111320.0 * k, lat * 110540.0) for lon, lat in ring_lonlat]
    s = 0.0
    for j in range(len(pts) - 1):
        s += pts[j][0] * pts[j + 1][1] - pts[j + 1][0] * pts[j][1]
    return abs(s) * 0.5


def _ring_centroid_world(world_xy):
    a = cx = cy = 0.0
    for j in range(len(world_xy) - 1):
        x0, y0 = world_xy[j]
        x1, y1 = world_xy[j + 1]
        cr = x0 * y1 - x1 * y0
        a += cr
        cx += (x0 + x1) * cr
        cy += (y0 + y1) * cr
    if abs(a) < 1e-6:
        n = max(1, len(world_xy) - 1)
        return (sum(p[0] for p in world_xy[:-1]) / n,
                sum(p[1] for p in world_xy[:-1]) / n)
    a *= 0.5
    return (cx / (6.0 * a), cy / (6.0 * a))


# ---------------------------------------------------------------- height
def _ground_z(world_x, world_y):
    """Landscape Z (cm) under (x,y). Verified UE5.7 specifics: trace must be
    COMPLEX; HitResult is read via to_tuple() (idx 0 = bBlockingHit, idx 5 =
    ImpactPoint); a None return = off-landscape clean miss."""
    world = _editor_world()
    start = unreal.Vector(world_x, world_y, TRACE_TOP_Z_CM)
    end = unreal.Vector(world_x, world_y, TRACE_BOT_Z_CM)
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
        except Exception as exc:
            _log("trace error (%.0f,%.0f): %s" % (world_x, world_y, exc))
            continue
    return None


def _lake_water_z(world_xy):
    """Median SHORELINE ground Z + offset (the ring traces the water edge,
    so its ground level IS the surface level). Median rejects a stray
    Megaplant-canopy hit / off-landscape miss. Returns (z, estimated)."""
    zs = []
    for (x, y) in world_xy[:-1]:
        z = _ground_z(x, y)
        if z is not None:
            zs.append(z)
    if zs:
        zs.sort()
        return zs[len(zs) // 2] + WATER_Z_OFFSET_CM, False
    _log("WARNING: no shoreline landscape hit - Z_ESTIMATED")
    return -1500.0 + WATER_Z_OFFSET_CM, True


# ---------------------------------------------------------------- material
def _ensure_water_material():
    """Find-or-create a minimal translucent water material (no Water-plugin
    dependency). Mirrors the M_GolfsimCourse Python authoring pattern; for
    Constant3Vector the output pin name is '' (empty), not 'RGB'."""
    mat = unreal.load_asset(MATERIAL_PATH)
    if mat is not None:
        return mat
    pkg = MATERIAL_PATH.rsplit("/", 1)[0]
    name = MATERIAL_PATH.rsplit("/", 1)[1]
    mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        name, pkg, unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property(
        "blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    try:
        mat.set_editor_property("two_sided", True)
    except Exception:
        pass
    mel = unreal.MaterialEditingLibrary
    col = mel.create_material_expression(
        mat, unreal.MaterialExpressionConstant3Vector, -400, -100)
    col.set_editor_property(
        "constant", unreal.LinearColor(WATER_COLOR[0], WATER_COLOR[1],
                                       WATER_COLOR[2], 1.0))
    mel.connect_material_property(col, "", unreal.MaterialProperty.MP_BASE_COLOR)
    op = mel.create_material_expression(
        mat, unreal.MaterialExpressionConstant, -400, 120)
    op.set_editor_property("r", WATER_OPACITY)
    mel.connect_material_property(op, "", unreal.MaterialProperty.MP_OPACITY)
    rg = mel.create_material_expression(
        mat, unreal.MaterialExpressionConstant, -400, 260)
    rg.set_editor_property("r", WATER_ROUGHNESS)
    mel.connect_material_property(
        rg, "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset(MATERIAL_PATH)
    _log("authored %s" % MATERIAL_PATH)
    return mat


# ---------------------------------------------------------------- mesh
def _build_lake(wid, ring_lonlat, water_mat):
    """Idempotent: destroy any prior Water_<wid> actor, spawn a fresh
    DynamicMeshActor at the polygon centroid/water-Z, triangulate the ring
    into a flat mesh (GeometryScript), assign the water material, generate
    collision from the mesh. Returns (verts, water_z, estimated)."""
    label = LABEL_PREFIX + str(wid)
    old = _find_actor_by_label(label)
    if old is not None:
        _eas().destroy_actor(old)

    world_xy = [_lonlat_to_world_xy(lon, lat) for lon, lat in ring_lonlat]
    water_z, est = _lake_water_z(world_xy)
    cx, cy = _ring_centroid_world(world_xy)

    actor = _eas().spawn_actor_from_class(
        unreal.DynamicMeshActor,
        unreal.Vector(cx, cy, water_z),
        unreal.Rotator(0.0, 0.0, 0.0))
    actor.set_actor_label(label)
    dmc = actor.dynamic_mesh_component
    mesh = dmc.get_dynamic_mesh()

    # Local 2D ring (drop closing dup; closed implied by triangulation).
    poly = [unreal.Vector2D(x - cx, y - cy) for (x, y) in world_xy[:-1]]
    opts = unreal.GeometryScriptPrimitiveOptions()
    xform = unreal.Transform()  # actor transform already places it in world
    try:
        unreal.GeometryScript_Primitives.append_triangulated_polygon(
            mesh, opts, xform, poly, True)
    except Exception as exc:
        _log("ERROR Water_%s append_triangulated_polygon: %s" % (wid, exc))
        return 0, water_z, est

    try:
        unreal.GeometryScript_Normals.recompute_normals(mesh)
    except Exception:
        pass

    dmc.set_material(0, water_mat)

    # Collision from the mesh (in-scope: ball-in-water / walk-blocking).
    try:
        copts = unreal.GeometryScriptCollisionFromMeshOptions()
        unreal.GeometryScript_Collision.set_dynamic_mesh_collision_from_mesh(
            mesh, dmc, copts)
        dmc.set_collision_profile_name(COLLISION_PROFILE)
        dmc.set_collision_enabled(
            unreal.CollisionEnabled.QUERY_AND_PHYSICS)
        dmc.update_collision(False)
    except Exception as exc:
        _log("Water_%s collision note: %s" % (wid, exc))

    return len(poly), water_z, est


def main():
    _log("=== M0.9 WATER (flat mesh) START - level %s ===" % LEVEL_HINT)
    ls = (_find_actor_by_label(LANDSCAPE_LABEL_HINT)
          or _find_actor_by_class_name("Landscape"))
    _log("landscape: %s" % (ls.get_actor_label() if ls else "NONE (!)"))

    water_mat = _ensure_water_material()
    feats = _load_features(GEOJSON_PATH)
    _log("loaded %d water feature(s)" % len(feats))

    built = skipped = estimated = 0
    for (wid, tags, ring) in feats:
        area = _polygon_area_m2(ring)
        if len(ring) < MIN_RING_VERTS or area < MIN_AREA_M2:
            _log("SKIP Water_%s: degenerate (verts=%d area=%.1f m2)"
                 % (wid, len(ring), area))
            skipped += 1
            continue
        n, z, est = _build_lake(wid, ring, water_mat)
        if n <= 0:
            skipped += 1
            continue
        built += 1
        if est:
            estimated += 1
        _log("  Water_%s: verts=%d z=%.0f%s area=%.0f m2"
             % (wid, n, z, " Z_ESTIMATED" if est else "", area))

    _log("=== M0.9 DONE: %d built, %d skipped, %d Z_ESTIMATED. "
         "umap NOT saved - operator must save GolfForgeDemoBlack.umap. ==="
         % (built, skipped, estimated))


main()
