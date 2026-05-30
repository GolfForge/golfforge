# build_landscape_grass.py
#
# Reusable, idempotent constructor for the golfsim fairway grass scatter
# (Material-polish milestone). Creates/configures a LandscapeGrassType asset
# (Megascans short-grass 3D clump, TRADITIONAL instanced + cull distance, NOT
# Nanite) that `build_course_material.py`'s LandscapeGrassOutput node binds and
# auto-scatters wherever the painted Fairway weight is.
#
# Run in the UE5.7 editor Python interpreter via execute_unreal_python. THREE
# modes (mirrors scatter_full_landscape.py SCATTER_MODE):
#
#   MODE PROBE - resolve UNKNOWNS first, mutate nothing (run FIRST):
#     GRASS_MODE="probe"; exec(compile(open(r"<repo>"
#       r"\engine\scripts\build_landscape_grass.py",encoding="utf-8")
#       .read(),"build_landscape_grass.py","exec"))
#
#   MODE A - create + configure + save the grass type (default):
#     exec(compile(open(r"<repo>\engine\scripts"
#       r"\build_landscape_grass.py",encoding="utf-8").read(),
#       "build_landscape_grass.py","exec"))
#
#   MODE REPORT - de-risk perf gate, a SEPARATE LATER call after the grass
#   has been built (Build > Build Grass Maps / camera over Fairway):
#     GRASS_MODE="report"; exec(compile(open(r"...same...").read(),
#       "build_landscape_grass.py","exec"))
#
# bridge note: execute_unreal_python returns output:None. Feedback via
# unreal.log() under LogPython; read with get_log_lines. Idempotent: the
# grass type is found-and-reconfigured, never duplicated. GRASS_MESHES are
# Fab/marketplace assets (gitignored, per-machine, re-downloaded on a clone).

import unreal

# ---------------------------------------------------------------- parameters
GRASS_TYPE_PATH = "/Game/Landscape/Grass/LGT_FairwayGrass"
# Fab/Megascans grass clump (gitignored, per-machine). Thatching Grass is a
# Kentucky Bluegrass (PC3D pack) - Substrate-validated this session; the
# golf-turf species. Flowerless "Healthy" + "Slender" variants only (no
# seed heads = the white parts; mown fairways don't flower). Nanite meshes,
# ~33 cm natural. Multiple variants -> one GrassVariety each, for natural
# variation. Build FAILS LOUD if any does not load.
_KBG = "/Game/PC3D_Kentucky_Bluegrass_v14/Meshes"
GRASS_MESHES = [
    _KBG + "/Healthy/SM_Healthy_KentuckyBluegrass_01",
    _KBG + "/Healthy/SM_Healthy_KentuckyBluegrass_02",
    _KBG + "/Healthy/SM_Healthy_KentuckyBluegrass_03",
    _KBG + "/Slender/SM_Healthy_Slender_KentuckyBluegrass_01",
    _KBG + "/Slender/SM_Healthy_Slender_KentuckyBluegrass_02",
]

# Total target density (reference-sim dense); split evenly across the
# variants so total stays ~GRASS_DENSITY regardless of variant count.
# ~700 read as ~1/m^2 (way too sparse); bumped ~10x for a lush carpet -
# the de-risk gate (GRASS_MODE=report) checks the instance-count/perf cost.
GRASS_DENSITY        = 10000.0
# Near-camera cull (gate verdict: full-coverage dense 3D grass = perf wall).
# Confined to ~35 m it's still far cheaper than an 80 m cull (area ~ r^2),
# so we keep a lush density but only render it around the player/ball; the
# tiled turf covers beyond. Wider start/end band = softer fade-out (the
# abrupt cutoff looked weird). Standard golf-sim move.
GRASS_CULL_START_CM  = 1000.0
GRASS_CULL_END_CM    = 3500.0
# KBG mesh ~33 cm at scale 1.0; mown fairway is short -> 0.5-0.8 = ~16-26 cm.
GRASS_SCALE_MIN      = 0.5
GRASS_SCALE_MAX      = 0.8
GRASS_RANDOM_ROTATION  = True
GRASS_ALIGN_TO_SURFACE = True
LEVEL_HINT      = "GolfForgeDemoBlack"

# Gate sanity band: grass density is per-tiny-area so the count is orders of
# magnitude larger than the 29k trees - wide band, the gate is the real check.
EXPECT_MIN_INST = 50000
EXPECT_MAX_INST = 5000000

_PROBE_GT   = "/Game/Landscape/Grass/_PROBE_LGT_DELETE"
_PROBE_MAT  = "/Game/Materials/_PROBE_GRASSOUT_DELETE"


def _log(msg):
    unreal.log("GRASS_BUILD: " + str(msg))


def _eas():
    return unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def _editor_world():
    return unreal.get_editor_subsystem(
        unreal.UnrealEditorSubsystem).get_editor_world()


def _find_actor_by_class_name(cls_substr):
    for a in _eas().get_all_level_actors():
        try:
            if cls_substr in a.get_class().get_name():
                return a
        except Exception:
            continue
    return None


def _asset_tools():
    return unreal.AssetToolsHelpers.get_asset_tools()


# ---------------------------------------------------------------- probe
def _probe():
    _log("=== PROBE (mutates nothing persistent) ===")
    has_gt = hasattr(unreal, "LandscapeGrassType")
    has_go = hasattr(unreal, "MaterialExpressionLandscapeGrassOutput")
    has_ls = hasattr(unreal, "MaterialExpressionLandscapeLayerSample")
    has_gtf = hasattr(unreal, "LandscapeGrassTypeFactory")
    _log("LandscapeGrassType=%s GrassOutput=%s LayerSample=%s "
         "LandscapeGrassTypeFactory=%s" % (has_gt, has_go, has_ls, has_gtf))
    if not has_gt:
        _log("FATAL: LandscapeGrassType unbound - cannot proceed")
        return

    # --- creatability: try factory, then new_object, log the FIRST that works
    eal = unreal.EditorAssetLibrary
    if eal.does_asset_exist(_PROBE_GT):
        eal.delete_asset(_PROBE_GT)
    gt = None
    path = None
    if has_gtf:
        try:
            gt = _asset_tools().create_asset(
                "_PROBE_LGT_DELETE", "/Game/Landscape/Grass",
                unreal.LandscapeGrassType, unreal.LandscapeGrassTypeFactory())
            path = "AssetTools.create_asset + LandscapeGrassTypeFactory"
        except Exception as exc:
            _log("factory create_asset failed: %s" % exc)
    if gt is None:
        try:
            gt = _asset_tools().create_asset(
                "_PROBE_LGT_DELETE", "/Game/Landscape/Grass",
                unreal.LandscapeGrassType, None)
            path = "AssetTools.create_asset + None factory"
        except Exception as exc:
            _log("None-factory create_asset failed: %s" % exc)
    _log("RESOLVED grass-type creation path = %s" % path)

    if gt is not None:
        members = [m for m in dir(gt)
                   if any(k in m.lower() for k in
                          ("grass", "variet", "mesh", "density", "cull"))]
        _log("LandscapeGrassType members: %r" % members)
        for p in ("grass_varieties", "grass_mesh", "grass_density"):
            try:
                v = gt.get_editor_property(p)
                _log("  prop %s default=%r type=%s" % (p, v, type(v).__name__))
            except Exception as exc:
                _log("  prop %s ERR %s" % (p, str(exc)[:60]))
        # try to author one variety, read back
        try:
            gv = unreal.GrassVariety()
            gvm = [m for m in dir(gv) if not m.startswith("_")]
            _log("GrassVariety fields: %r" % gvm)
            try:
                gt.set_editor_property("grass_varieties", [gv])
                rb = gt.get_editor_property("grass_varieties")
                _log("grass_varieties set->readback len=%s (STRUCT-ARRAY "
                     "AUTHORABLE=%s)" % (len(rb) if hasattr(rb, "__len__")
                                         else "?", bool(rb)))
            except Exception as exc:
                _log("grass_varieties set ERR %s -> manual-config fallback"
                     % str(exc)[:80])
        except Exception as exc:
            _log("unreal.GrassVariety() ctor ERR %s" % str(exc)[:80])
        eal.delete_asset(_PROBE_GT)
        _log("probe grass type deleted")

    # --- GrassOutput class + grass_types on a throwaway material
    if has_go:
        if eal.does_asset_exist(_PROBE_MAT):
            eal.delete_asset(_PROBE_MAT)
        try:
            mat = _asset_tools().create_asset(
                "_PROBE_GRASSOUT_DELETE", "/Game/Materials",
                unreal.Material, unreal.MaterialFactoryNew())
            go = unreal.MaterialEditingLibrary.create_material_expression(
                mat, unreal.MaterialExpressionLandscapeGrassOutput, -300, 0)
            gtypes = go.get_editor_property("grass_types")
            _log("GrassOutput.grass_types default=%r type=%s"
                 % (gtypes, type(gtypes).__name__))
            gi = unreal.GrassInput() if hasattr(unreal, "GrassInput") else None
            if gi is not None:
                _log("GrassInput fields: %r"
                     % [m for m in dir(gi) if not m.startswith("_")])
            ok = unreal.MaterialEditingLibrary.recompile_material(mat)
            _log("throwaway material w/ GrassOutput recompiled=%r "
                 "(Substrate r.Substrate=1 compat check)" % ok)
            eal.delete_asset(_PROBE_MAT)
            _log("probe material deleted")
        except Exception as exc:
            _log("GrassOutput probe ERR %s" % str(exc)[:120])
    _log("=== PROBE DONE - read verdict above before MODE A ===")


# ---------------------------------------------------------------- build
def _ensure_grass_type():
    eal = unreal.EditorAssetLibrary
    if eal.does_asset_exist(GRASS_TYPE_PATH):
        gt = unreal.load_asset(GRASS_TYPE_PATH)
        _log("reusing %s" % GRASS_TYPE_PATH)
        return gt
    name = GRASS_TYPE_PATH.rsplit("/", 1)[1]
    pkg = GRASS_TYPE_PATH.rsplit("/", 1)[0]
    fac = (unreal.LandscapeGrassTypeFactory()
           if hasattr(unreal, "LandscapeGrassTypeFactory") else None)
    try:
        gt = _asset_tools().create_asset(
            name, pkg, unreal.LandscapeGrassType, fac)
    except Exception as exc:
        _log("FATAL: could not create LandscapeGrassType (%s). MANUAL "
             "HANDOFF: Content Browser -> %s -> right-click -> Foliage -> "
             "Landscape Grass Type, name '%s'; add one Grass Variety per "
             "mesh in %r; Density~%.0f each, StartCull=%s, EndCull=%s, "
             "Scale min=%s max=%s, RandomRotation=%s, AlignToSurface=%s; "
             "save; then re-run MODE A (it will find+reuse it)."
             % (str(exc)[:80], pkg, name, GRASS_MESHES,
                GRASS_DENSITY / max(1, len(GRASS_MESHES)),
                GRASS_CULL_START_CM, GRASS_CULL_END_CM, GRASS_SCALE_MIN,
                GRASS_SCALE_MAX, GRASS_RANDOM_ROTATION,
                GRASS_ALIGN_TO_SURFACE))
        return None
    _log("created %s" % GRASS_TYPE_PATH)
    return gt


def _configure_grass_type(gt):
    meshes = []
    for p in GRASS_MESHES:
        m = unreal.load_asset(p)
        if m is None:
            _log("FATAL: grass mesh does not load: %s (not flat-faking it)"
                 % p)
            return False
        meshes.append(m)
    per = float(GRASS_DENSITY) / max(1, len(meshes))  # total ~= GRASS_DENSITY
    varieties = []
    for m in meshes:
        gv = unreal.GrassVariety()
        gv.set_editor_property("grass_mesh", m)
        gv.set_editor_property("random_rotation", GRASS_RANDOM_ROTATION)
        gv.set_editor_property("align_to_surface", GRASS_ALIGN_TO_SURFACE)
        # UE5.7: grass_density is FPerPlatformFloat; cull distances are
        # FPerPlatformInt (plain int/float rejected) - set the .default.
        ppd = unreal.PerPlatformFloat()
        ppd.set_editor_property("default", per)
        gv.set_editor_property("grass_density", ppd)
        for prop, cm in (("start_cull_distance", GRASS_CULL_START_CM),
                         ("end_cull_distance", GRASS_CULL_END_CM)):
            ppi = unreal.PerPlatformInt()
            ppi.set_editor_property("default", int(cm))
            gv.set_editor_property(prop, ppi)
        for prop in ("scale_x", "scale_y", "scale_z"):
            fi = unreal.FloatInterval()
            fi.set_editor_property("min", GRASS_SCALE_MIN)
            fi.set_editor_property("max", GRASS_SCALE_MAX)
            try:
                gv.set_editor_property(prop, fi)
            except Exception as exc:
                _log("GrassVariety.%s note: %s" % (prop, str(exc)[:50]))
        varieties.append(gv)
    try:
        gt.set_editor_property("grass_varieties", varieties)
    except Exception as exc:
        _log("FATAL: grass_varieties not settable (%s). MANUAL: open %s, "
             "add %d varieties for %r, density~%.0f each, cull %s/%s, "
             "scale %s-%s." % (str(exc)[:60], GRASS_TYPE_PATH, len(meshes),
                               GRASS_MESHES, per, GRASS_CULL_START_CM,
                               GRASS_CULL_END_CM, GRASS_SCALE_MIN,
                               GRASS_SCALE_MAX))
        return False
    _log("configured %d varieties @ %.0f density each (~%.0f total)"
         % (len(varieties), per, GRASS_DENSITY))
    return True


def main():
    _log("=== GRASS BUILD (MODE A) level=%s ===" % LEVEL_HINT)
    gt = _ensure_grass_type()
    if gt is None:
        return
    if not _configure_grass_type(gt):
        return
    unreal.EditorAssetLibrary.save_asset(GRASS_TYPE_PATH)
    _log("saved %s : %d KBG varieties, ~%.0f total density, cull=%s/%s, "
         "scale %s-%s" % (GRASS_TYPE_PATH, len(GRASS_MESHES), GRASS_DENSITY,
                          GRASS_CULL_START_CM, GRASS_CULL_END_CM,
                          GRASS_SCALE_MIN, GRASS_SCALE_MAX))
    _log("NEXT: build_course_material.py main() must run so its "
         "LandscapeGrassOutput binds this grass type; then USER triggers "
         "'Build > Build Grass Maps' / moves camera over Fairway; then run "
         "GRASS_MODE='report' (separate later call) for the de-risk gate.")
    _log("=== GRASS BUILD MODE A DONE ===")


def report():
    _log("=== DE-RISK GATE REPORT ===")
    meshes = set()
    for p in GRASS_MESHES:
        mm = unreal.load_asset(p)
        if mm is not None:
            meshes.add(mm)
    total = 0
    ncomp = 0
    for a in _eas().get_all_level_actors():
        cn = a.get_class().get_name()
        if "Landscape" not in cn:
            continue
        try:
            comps = a.get_components_by_class(
                unreal.HierarchicalInstancedStaticMeshComponent)
        except Exception:
            continue
        for c in comps:
            try:
                n = int(c.get_instance_count())
            except Exception:
                n = 0
            if n <= 0:
                continue
            sm = None
            try:
                sm = c.get_editor_property("static_mesh")
            except Exception:
                pass
            if meshes and sm is not None and sm not in meshes:
                continue
            ncomp += 1
            total += n
    _log("GRASS INSTANCE TOTAL = %d across %d HISM component(s)"
         % (total, ncomp))
    if total < EXPECT_MIN_INST:
        _log("WARNING: %d < floor %d - grass maps not built yet? (USER must "
             "Build Grass Maps / move camera over Fairway first)"
             % (total, EXPECT_MIN_INST))
    elif total > EXPECT_MAX_INST:
        _log("WARNING: %d > ceiling %d - density likely too high" % (
            total, EXPECT_MAX_INST))
    world = _editor_world()
    for cmd in ("stat unit", "stat fps", "stat streaming",
                "rhi.DumpMemory"):
        try:
            unreal.SystemLibrary.execute_console_command(world, cmd)
            _log("issued console: %s" % cmd)
        except Exception as exc:
            _log("console '%s' failed: %s" % (cmd, exc))
    _log("---- MATERIAL-POLISH FAIRWAY DE-RISK GATE ----")
    _log("  grass instances   = %d" % total)
    _log("  GPU total MB      = <read from LogRHI rhi.DumpMemory>")
    _log("  editor FPS        = <USER reads focused viewport stat fps>")
    _log("  texpool over MB   = <USER reads stat streaming>")
    _log("  scope = FAIRWAY ONLY; no density tune; not replicated; "
         "umap not yet saved")
    _log("=== GATE REPORT DONE ===")


_mode = globals().get("GRASS_MODE")
if _mode == "probe":
    _probe()
elif _mode == "report":
    report()
else:
    main()
