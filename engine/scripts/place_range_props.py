# place_range_props.py
#
# GOL-168 (first pass): painted-stake yardage markers down the Practice Range
# corridor at 50 / 100 / 150 / 200 / 250 yd. The range had no distance reference
# beyond the movable target pin; these fixed stakes give it the "this is a
# driving range" read for screenshots.
#
# Each marker is a thin white post + a colored cap + a 3D yardage number facing
# back toward the tee. Markers sit at the FAIRWAY EDGE (both sides), never on the
# centerline, so they stay clear of ball flight and the centered target pin (the
# ticket's "keep props out of the playable corridor" pitfall).
#
# Placement reuses the exact range yards->world convention from
# AGolfRangeHUD::ApplyPinDistance (GolfRangeHUD.cpp): world +X downrange from the
# tee, X = TeeX + Yards * 91.44 cm, so the stakes line up with the target pin at
# the same yardage. TeeX is read from the range PlayerStart (the tee origin the
# pin anchors to). Ground Z is sourced with the standard downward line trace
# (same as build_water_actors.py / build_hole_markers.py).
#
# Mirrors the engine/scripts conventions:
#  - Idempotent: every run destroys prior RangeYard* actors before spawning fresh
#    (no duplicates). Re-run any time geometry constants change.
#  - PERSISTENT (unlike build_hole_markers' editor-only dev pillars): these are
#    real range scenery and must survive the cook. The script does NOT save -- the
#    operator saves PracticeRange.umap afterward or the markers are lost.
#
# Run in the UE5.7 editor Python interpreter via execute_unreal_python. Open
# PracticeRange.umap first (the script guards against the wrong level). TWO modes:
#
#   MODE BUILD (default) - spawn fresh markers, killing any priors:
#     exec(compile(open(r"<repo>\engine\scripts\place_range_props.py",
#       encoding="utf-8").read(),"place_range_props.py","exec"))
#
#   MODE CLEAR - destroy every RangeYard* actor, spawn nothing:
#     RANGE_PROPS_MODE="clear"
#     exec(compile(open(r"<repo>\engine\scripts\place_range_props.py",
#       encoding="utf-8").read(),"place_range_props.py","exec"))
#
#   Force-build even if the current level isn't PracticeRange:
#     RANGE_PROPS_FORCE=True
#
# bridge note: execute_unreal_python returns output:None. All feedback via
# unreal.log() under LogPython; read with get_log_lines.

import unreal

# ---------------------------------------------------------------- parameters
LEVEL_HINT = "PracticeRange"

# Distances down the corridor (yd). Range corridor is 400 yd (LANE_LEN_YD), so
# 250 is comfortably inside the tree wall.
MARKER_YARDS = (50, 100, 150, 200, 250)

# Range geometry. Keep in sync with build_range_splatmap.py + GolfRangeHUD.cpp.
CM_PER_YD      = 91.44       # exact (matches AGolfRangeHUD::ApplyPinDistance)
YD             = 0.9144      # m per yard (exact)
LANE_LEN_YD    = 400.0       # X extent of the open lane (tee at -X end)
FAIRWAY_WID_YD = 50.0        # Y width of the centered mown strip
# Markers sit just inside the fairway edge so they read as "edge of the short
# grass" without intruding on the centerline ball path.
SIDE_Y_CM      = (FAIRWAY_WID_YD * YD / 2.0) * 100.0   # ~= 2286 cm
SIDES          = ("left", "right")                     # left = -Y, right = +Y

# Vertical line-trace span for landscape height (matches build_water_actors.py).
TRACE_TOP_Z_CM = 50000.0
TRACE_BOT_Z_CM = -50000.0

# Stake geometry (cm). Cylinder/Cube BasicShapes are 100 uu; scale = size/100.
STAKE_HEIGHT_CM = 140.0
STAKE_DIAM_CM   = 8.0
CAP_SIZE_CM     = 16.0
TEXT_ABOVE_CAP_CM = 30.0
TEXT_WORLD_SIZE = 120.0      # readable from the tee; bump if too small in PIE
TEXT_YAW        = 180.0      # face glyphs back toward the tee (-X)

# Colors (linear 0..1 for materials; 0..255 for TextRender color).
POST_COLOR  = (0.92, 0.92, 0.92)   # white
CAP_COLOR   = (0.85, 0.05, 0.05)   # painted red top
TEXT_COLOR  = (255, 255, 255)      # white

CYLINDER_MESH_PATH = "/Engine/BasicShapes/Cylinder"
CUBE_MESH_PATH     = "/Engine/BasicShapes/Cube"

POST_MAT_PATH = "/Game/Materials/M_RangeMarkerPost"
CAP_MAT_PATH  = "/Game/Materials/M_RangeMarkerCap"

LABEL_PREFIX_POST  = "RangeYardMarker_"   # -> RangeYardMarker_<yd>_<side>
LABEL_PREFIX_CAP   = "RangeYardCap_"      # -> RangeYardCap_<yd>_<side>
LABEL_PREFIX_LABEL = "RangeYardLabel_"    # -> RangeYardLabel_<yd>_<side>
LABEL_PREFIXES = (LABEL_PREFIX_POST, LABEL_PREFIX_CAP, LABEL_PREFIX_LABEL)

LAYER_NAME = "RangeProps"


def _log(msg):
    unreal.log("RANGE_PROPS: " + str(msg))


def _eas():
    return unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def _layers():
    return unreal.get_editor_subsystem(unreal.LayersSubsystem)


def _editor_world():
    return unreal.get_editor_subsystem(
        unreal.UnrealEditorSubsystem).get_editor_world()


# ---------------------------------------------------------------- tee origin
def _tee_x_cm():
    """The range tee origin X (cm): the PlayerStart's X, matching the tee anchor
    AGolfRangeHUD::ApplyPinDistance uses. Fallback to the lane's -X end derived
    from LANE_LEN_YD if no PlayerStart is present."""
    for a in _eas().get_all_level_actors():
        try:
            if "PlayerStart" in a.get_class().get_name():
                x = a.get_actor_location().x
                _log("tee origin from PlayerStart '%s': X=%.0f cm"
                     % (a.get_actor_label(), x))
                return float(x)
        except Exception:
            continue
    fallback = -(LANE_LEN_YD * YD / 2.0) * 100.0
    _log("WARNING no PlayerStart found; tee origin fallback X=%.0f cm" % fallback)
    return float(fallback)


# ---------------------------------------------------------------- height
def _ground_z(world_x, world_y):
    """Landscape Z (cm) under (x,y). Returns the tee Z (0 here, flat range) on a
    clean miss so the stake still spawns. Trace must be COMPLEX; HitResult read
    via to_tuple() (idx 0 = bBlockingHit, idx 5 = ImpactPoint)."""
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


# ---------------------------------------------------------------- material
def _ensure_const_material(path, color):
    """Find-or-create a minimal constant-color opaque material. Mirrors
    build_water_actors._ensure_water_material; Constant3Vector output pin is ''
    (empty), NOT 'RGB'."""
    mat = unreal.load_asset(path)
    if mat is not None:
        return mat
    pkg = path.rsplit("/", 1)[0]
    name = path.rsplit("/", 1)[1]
    mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        name, pkg, unreal.Material, unreal.MaterialFactoryNew())
    mel = unreal.MaterialEditingLibrary
    col = mel.create_material_expression(
        mat, unreal.MaterialExpressionConstant3Vector, -400, -100)
    col.set_editor_property(
        "constant", unreal.LinearColor(color[0], color[1], color[2], 1.0))
    mel.connect_material_property(col, "", unreal.MaterialProperty.MP_BASE_COLOR)
    rg = mel.create_material_expression(
        mat, unreal.MaterialExpressionConstant, -400, 120)
    rg.set_editor_property("r", 0.65)
    mel.connect_material_property(rg, "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset(path)
    _log("authored %s" % path)
    return mat


# ---------------------------------------------------------------- helpers
def _kill_prior():
    eas = _eas()
    killed = 0
    for a in list(eas.get_all_level_actors()):
        try:
            lbl = a.get_actor_label()
        except Exception:
            continue
        if any(lbl.startswith(p) for p in LABEL_PREFIXES):
            try:
                eas.destroy_actor(a)
                killed += 1
            except Exception:
                continue
    return killed


def _assign_layer(actor):
    try:
        _layers().add_actor_to_layer(actor, LAYER_NAME)
    except Exception as exc:
        _log("WARNING add_actor_to_layer on %s failed: %s"
             % (actor.get_actor_label(), str(exc)[:80]))


def _spawn_mesh(mesh, x, y, z, scale, label, mat):
    eas = _eas()
    actor = eas.spawn_actor_from_class(
        unreal.StaticMeshActor, unreal.Vector(x, y, z),
        unreal.Rotator(0.0, 0.0, 0.0))
    actor.set_actor_label(label)
    smc = actor.static_mesh_component
    smc.set_static_mesh(mesh)
    smc.set_mobility(unreal.ComponentMobility.MOVABLE)
    if mat is not None:
        smc.set_material(0, mat)
    actor.set_actor_scale3d(scale)
    # Decorative: never deflect an edge-clipping shot.
    smc.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    _assign_layer(actor)
    return actor


def _spawn_label(x, y, z, label, text):
    eas = _eas()
    actor = eas.spawn_actor_from_class(
        unreal.TextRenderActor, unreal.Vector(x, y, z),
        unreal.Rotator(0.0, 0.0, TEXT_YAW))
    actor.set_actor_label(label)
    trc = actor.text_render
    trc.set_text(text)
    trc.set_horizontal_alignment(unreal.HorizTextAligment.EHTA_CENTER)
    trc.set_vertical_alignment(unreal.VerticalTextAligment.EVRTA_TEXT_CENTER)
    trc.set_editor_property("world_size", TEXT_WORLD_SIZE)
    try:
        trc.set_text_render_color(
            unreal.Color(TEXT_COLOR[0], TEXT_COLOR[1], TEXT_COLOR[2], 255))
    except Exception:
        pass
    _assign_layer(actor)
    return actor


# ---------------------------------------------------------------- modes
def _build():
    world = _editor_world()
    world_name = world.get_name() if world else "<none>"
    _log("=== BUILD START (level: %s) ===" % world_name)
    if (LEVEL_HINT.lower() not in world_name.lower()
            and not globals().get("RANGE_PROPS_FORCE", False)):
        _log("ABORT: current level '%s' is not '%s'. Open PracticeRange.umap, or "
             "set RANGE_PROPS_FORCE=True to override." % (world_name, LEVEL_HINT))
        return

    killed = _kill_prior()
    _log("killed %d prior RangeYard* actor(s)" % killed)

    cyl = unreal.load_asset(CYLINDER_MESH_PATH)
    cube = unreal.load_asset(CUBE_MESH_PATH)
    if cyl is None or cube is None:
        _log("FATAL: BasicShapes mesh missing (cyl=%s cube=%s)" % (cyl, cube))
        return
    post_mat = _ensure_const_material(POST_MAT_PATH, POST_COLOR)
    cap_mat  = _ensure_const_material(CAP_MAT_PATH, CAP_COLOR)

    tee_x = _tee_x_cm()
    post_scale = unreal.Vector(STAKE_DIAM_CM / 100.0, STAKE_DIAM_CM / 100.0,
                               STAKE_HEIGHT_CM / 100.0)
    cap_scale = unreal.Vector(CAP_SIZE_CM / 100.0, CAP_SIZE_CM / 100.0,
                              CAP_SIZE_CM / 100.0)

    spawned = 0
    for yd in MARKER_YARDS:
        x = tee_x + yd * CM_PER_YD
        for side in SIDES:
            y = -SIDE_Y_CM if side == "left" else SIDE_Y_CM
            gz = _ground_z(x, y)
            tag = "%d_%s" % (yd, side)
            # Cylinder origin = center -> bottom on the ground.
            _spawn_mesh(cyl, x, y, gz + STAKE_HEIGHT_CM * 0.5,
                        post_scale, LABEL_PREFIX_POST + tag, post_mat)
            _spawn_mesh(cube, x, y, gz + STAKE_HEIGHT_CM + CAP_SIZE_CM * 0.5,
                        cap_scale, LABEL_PREFIX_CAP + tag, cap_mat)
            _spawn_label(x, y, gz + STAKE_HEIGHT_CM + TEXT_ABOVE_CAP_CM,
                         LABEL_PREFIX_LABEL + tag, str(yd))
            spawned += 1

    try:
        _layers().editor_refresh_layer_browser()
    except Exception:
        pass

    _log("placed %d marker(s) at %s yd x %d side(s) (tee X=%.0f cm)"
         % (spawned, ",".join(str(y) for y in MARKER_YARDS), len(SIDES), tee_x))
    _log("PERSISTENT actors: SAVE PracticeRange.umap or they are lost. Toggle "
         "in Window > Layers: '%s'." % LAYER_NAME)
    _log("=== BUILD DONE ===")


def _clear():
    _log("=== CLEAR START ===")
    killed = _kill_prior()
    _log("destroyed %d RangeYard* actor(s)" % killed)
    _log("=== CLEAR DONE ===")


_mode = globals().get("RANGE_PROPS_MODE", "build")
if _mode == "clear":
    _clear()
else:
    _build()
