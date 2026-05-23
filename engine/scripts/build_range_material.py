# build_range_material.py
#
# Practice Range fork of build_course_material.py. Builds an idempotent,
# reusable rebuilder for /Game/Materials/M_PracticeRange -- a trimmed 4-layer
# (Fairway/Rough/Tee/Trees) textured landscape material for the flat synthetic
# range. It is a deliberate fork (NOT a shared import) so the range and the
# course materials can diverge freely; the build engine below is kept verbatim
# from build_course_material.py so the two stay trivially diffable. Do NOT
# clobber M_GolfsimCourse.
#
# SAFETY: same backup-then-nuke+recreate at the SAME path as the course script
# (canonical name + landscape soft-path binding preserved; reversible).
#
#   MODE PROBE - resolve class availability, mutate nothing persistent:
#     MATERIAL_MODE="probe"; exec(compile(open(r"C:\Users\pucho\code"
#       r"\golfsim\engine\scripts\build_range_material.py",
#       encoding="utf-8").read(),"build_range_material.py","exec"))
#
#   MODE A - backup + rebuild + save (default):
#     exec(compile(open(r"C:\Users\pucho\code\golfsim\engine\scripts"
#       r"\build_range_material.py",encoding="utf-8").read(),
#       "build_range_material.py","exec"))
#
# PRECONDITION: run with ONLY PracticeRange.umap open -- _reassign_landscape
# sets landscape_material on every Landscape actor in the world.
#
# bridge note: execute_unreal_python returns output:None. Feedback via
# unreal.log() under LogPython; read with get_log_lines. recompile_material
# returns None BY DESIGN (success == no exception). Textured-layer texture paths
# are the same Megascans/Fab downloads M_GolfsimCourse uses (gitignored,
# per-machine); the build FAILS LOUD on an unloadable path.

import unreal

# ---------------------------------------------------------------- parameters
MATERIAL_PATH = "/Game/Materials/M_PracticeRange"
BACKUP_PATH   = "/Game/Materials/M_PracticeRange_PreBackup"

# Fab/Megascans surface texture triplets (gitignored, per-machine, Bridge-
# downloaded). Each Quixel surface ships T_<code>_2K_{B,N,ORM}; ORM packing
# is R:occlusion G:roughness B:metallic so roughness = the G channel. The
# build FAILS LOUD if a path does not load (no silent flat-fallback).
_FAB = "/Game/Fab/Megascans/Surfaces"


def _surf(folder, code):
    b = "%s/%s/Medium/%s_tier_2/Textures/T_%s_2K_" % (_FAB, folder, code,
                                                      code)
    return {"albedo": b + "B", "normal": b + "N",
            "rough": b + "ORM", "rough_ch": "G"}


# Macro variation: blend the detail albedo with a much larger-scale sample
# of itself to break the obvious tiling repeat on flyovers (standard
# landscape technique). MACRO_SCALE_DIV = how many x larger the macro
# tile is vs detail; MACRO_BLEND = how much macro is mixed in (0..1).
MACRO_SCALE_DIV   = 9.0
MACRO_BLEND       = 0.45

# 4 painted layers, ordered as the range splatmap (build_range_splatmap.py)
# and reusing the same Fab surfaces + KBG tint as M_GolfsimCourse. tiling =
# LandscapeLayerCoords.mapping_scale (higher = finer / more repeats).
# grass=True keys the LandscapeGrassOutput (Fairway -> LGT_FairwayGrass).
LAYERS = [
    dict(name="Fairway",  mode="textured", grass=True, tiling=3.0,
         tint=(0.10, 0.26, 0.08),
         **_surf("Lawn_Grass_tkynejer", "tkynejer")),
    dict(name="Rough",    mode="textured", tiling=2.0,
         **_surf("Uncut_Grass_oilpt20", "oilpt20")),
    dict(name="Tee",      mode="textured", tiling=3.0,
         tint=(0.10, 0.26, 0.08),
         **_surf("Lawn_Grass_tkynejer", "tkynejer")),
    dict(name="Trees",    mode="textured", tiling=2.5,
         **_surf("Clover_Patches_on_Grass_sgmkajak", "sgmkajak")),
]
BASE_COLOR_FALLBACK = (0.05, 0.20, 0.05)   # chain terminator
ROUGH_FALLBACK      = 0.70
GRASS_TYPE_PATH     = "/Game/Landscape/Grass/LGT_FairwayGrass"
# The range ships WITHOUT 3D LandscapeGrass by default: dense camera-driven KBG
# clumps on the fairway you stand on dominated the GPU (halved fullscreen FPS),
# and a flat synthetic range reads fine on the textured fairway alone. Override
# BUILD_GRASS=True before exec to restore the LandscapeGrassOutput node. (The
# course material M_GolfsimCourse keeps its grass independently.)
BUILD_GRASS         = globals().get("BUILD_GRASS", False)

_PROBE_MAT = "/Game/Materials/_PROBE_RANGEMATBUILD_DELETE"


def _log(msg):
    unreal.log("RANGE_MAT: " + str(msg))


def _mel():
    return unreal.MaterialEditingLibrary


def _at():
    return unreal.AssetToolsHelpers.get_asset_tools()


# ---------------------------------------------------------------- probe
def _probe():
    _log("=== PROBE (mutates nothing persistent) ===")
    for cls in ("MaterialExpressionLandscapeLayerWeight",
                "MaterialExpressionLandscapeLayerSample",
                "MaterialExpressionLandscapeLayerBlend",
                "MaterialExpressionLandscapeGrassOutput",
                "MaterialExpressionTextureSample",
                "MaterialExpressionTextureCoordinate",
                "MaterialExpressionConstant3Vector",
                "MaterialExpressionConstant"):
        _log("class %s = %s" % (cls, hasattr(unreal, cls)))
    eal = unreal.EditorAssetLibrary
    if eal.does_asset_exist(_PROBE_MAT):
        eal.delete_asset(_PROBE_MAT)
    try:
        mat = _at().create_asset("_PROBE_RANGEMATBUILD_DELETE", "/Game/Materials",
                                 unreal.Material, unreal.MaterialFactoryNew())
        go = _mel().create_material_expression(
            mat, unreal.MaterialExpressionLandscapeGrassOutput, -300, 0)
        for p in ("grass_types",):
            try:
                v = go.get_editor_property(p)
                _log("GrassOutput.%s type=%s len=%s" % (
                    p, type(v).__name__, len(v) if hasattr(v, "__len__")
                    else "?"))
            except Exception as exc:
                _log("GrassOutput.%s ERR %s" % (p, str(exc)[:60]))
        gi = unreal.GrassInput()
        for f in ("name", "grass_type", "input"):
            try:
                gi.set_editor_property(f, gi.get_editor_property(f))
                _log("GrassInput field OK: %s" % f)
            except Exception as exc:
                _log("GrassInput field '%s' ERR %s" % (f, str(exc)[:60]))
        eal.delete_asset(_PROBE_MAT)
        _log("probe material deleted")
    except Exception as exc:
        _log("probe ERR %s" % str(exc)[:120])
    m = unreal.load_asset(MATERIAL_PATH)
    _log("M_PracticeRange loaded=%s (None on first run is expected)."
         % (m is not None))
    _log("=== PROBE DONE - read verdict before MODE A ===")


# ---------------------------------------------------------------- build
def _backup_then_recreate():
    eal = unreal.EditorAssetLibrary
    if not eal.does_asset_exist(MATERIAL_PATH):
        _log("WARNING: %s does not exist - creating fresh (no backup)"
             % MATERIAL_PATH)
    elif not eal.does_asset_exist(BACKUP_PATH):
        if eal.duplicate_asset(MATERIAL_PATH, BACKUP_PATH) is not None:
            eal.save_asset(BACKUP_PATH)
            _log("backed up -> %s" % BACKUP_PATH)
        else:
            _log("FATAL: backup duplicate failed - ABORT (won't risk the "
                 "working material without a fallback)")
            return None
    else:
        _log("backup %s already exists (kept)" % BACKUP_PATH)
    if eal.does_asset_exist(MATERIAL_PATH):
        eal.delete_asset(MATERIAL_PATH)
    name = MATERIAL_PATH.rsplit("/", 1)[1]
    pkg = MATERIAL_PATH.rsplit("/", 1)[0]
    mat = _at().create_asset(name, pkg, unreal.Material,
                             unreal.MaterialFactoryNew())
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
    mat.set_editor_property(
        "shading_model", unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    _log("recreated %s (opaque, default-lit)" % MATERIAL_PATH)
    return mat


def _tex(mat, path, sampler, x, y, tiling):
    t = unreal.load_asset(path)
    if t is None:
        _log("FATAL: texture does not load: %s (set the real Megascans "
             "path; not flat-faking it)" % path)
        return None
    ts = _mel().create_material_expression(
        mat, unreal.MaterialExpressionTextureSample, x, y)
    ts.set_editor_property("texture", t)
    try:
        ts.set_editor_property("sampler_type", sampler)
    except Exception as exc:
        _log("sampler_type note (%s): %s" % (path, str(exc)[:50]))
    # SHARED sampler: textured layers x 3 textures (+macro) exceed the 16
    # per-material sampler limit unless they share the world group sampler.
    # Without this the material won't compile at full replication.
    try:
        ts.set_editor_property(
            "sampler_source",
            unreal.SamplerSourceMode.SSM_WRAP_WORLD_GROUP_SETTINGS)
    except Exception as exc:
        _log("sampler_source note (%s): %s" % (path, str(exc)[:50]))
    # LANDSCAPE materials must use LandscapeLayerCoords for terrain-space
    # UVs - TextureCoordinate/TexCoord[0] does NOT tile across a landscape
    # (samples near-uniform; the grid shows through). mapping_scale is the
    # tiling knob (higher = more repeats / smaller features).
    lc = _mel().create_material_expression(
        mat, unreal.MaterialExpressionLandscapeLayerCoords, x - 250, y)
    try:
        lc.set_editor_property("mapping_scale", float(tiling))
    except Exception as exc:
        _log("LandscapeLayerCoords.mapping_scale note: %s" % str(exc)[:60])
    _mel().connect_material_expressions(lc, "", ts, "UVs")
    return ts


def _macro_albedo(mat, layer, x, y):
    """Detail albedo lerp'd with a far-larger-scale sample of itself to
    break the tiling repeat on flyovers. Returns (lerp_expr, '')."""
    detail = _tex(mat, layer["albedo"],
                  unreal.MaterialSamplerType.SAMPLERTYPE_COLOR,
                  x, y, layer["tiling"])
    if detail is None:
        return None, None
    macro = _tex(mat, layer["albedo"],
                 unreal.MaterialSamplerType.SAMPLERTYPE_COLOR,
                 x, y + 360, layer["tiling"] / MACRO_SCALE_DIV)
    if macro is None:
        src, pin = detail, "RGB"
    else:
        lerp = _mel().create_material_expression(
            mat, unreal.MaterialExpressionLinearInterpolate, x + 220, y)
        a = _mel().create_material_expression(
            mat, unreal.MaterialExpressionConstant, x + 60, y + 220)
        a.set_editor_property("r", float(MACRO_BLEND))
        _mel().connect_material_expressions(detail, "RGB", lerp, "A")
        _mel().connect_material_expressions(macro, "RGB", lerp, "B")
        _mel().connect_material_expressions(a, "", lerp, "Alpha")
        src, pin = lerp, ""
    # Optional per-layer recolor: DESATURATE the albedo to luminance, then
    # MULTIPLY by `tint` (a target color). This makes the tiled base adopt
    # the tint's HUE while keeping the texture's light/dark detail - a
    # plain multiply can only darken (can't hue-shift). `tint` here is the
    # KBG turf-green target so the base meshes with the KBG 3D grass.
    tint = layer.get("tint")
    if not tint:
        return src, pin
    des = _mel().create_material_expression(
        mat, unreal.MaterialExpressionDesaturation, x + 380, y)
    _mel().connect_material_expressions(src, pin, des, "")  # -> luminance
    mul = _mel().create_material_expression(
        mat, unreal.MaterialExpressionMultiply, x + 560, y)
    tc = _mel().create_material_expression(
        mat, unreal.MaterialExpressionConstant3Vector, x + 400, y + 220)
    tc.set_editor_property(
        "constant", unreal.LinearColor(tint[0], tint[1], tint[2], 1.0))
    _mel().connect_material_expressions(des, "", mul, "A")
    _mel().connect_material_expressions(tc, "", mul, "B")
    return mul, ""


def _layer_src(mat, layer, channel, x, y):
    """(expr, out_pin) feeding one layer for channel base|normal|rough."""
    if layer["mode"] == "textured":
        if channel == "base":
            return _macro_albedo(mat, layer, x, y)
        if channel == "normal":
            ts = _tex(mat, layer["normal"],
                      unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL,
                      x, y, layer["tiling"])
            return (ts, "RGB") if ts else (None, None)
        # ORM / packed-mask textures MUST sample as Masks, not LinearColor,
        # or the whole material fails to compile (-> default checker).
        ts = _tex(mat, layer["rough"],
                  unreal.MaterialSamplerType.SAMPLERTYPE_MASKS,
                  x, y, layer["tiling"])
        return (ts, layer.get("rough_ch", "R")) if ts else (None, None)
    # flat
    if channel == "base":
        c = _mel().create_material_expression(
            mat, unreal.MaterialExpressionConstant3Vector, x, y)
        col = layer["color"]
        c.set_editor_property(
            "constant", unreal.LinearColor(col[0], col[1], col[2], 1.0))
        return c, ""
    if channel == "normal":
        c = _mel().create_material_expression(
            mat, unreal.MaterialExpressionConstant3Vector, x, y)
        c.set_editor_property("constant", unreal.LinearColor(0.0, 0.0, 1.0, 1.0))
        return c, ""
    c = _mel().create_material_expression(
        mat, unreal.MaterialExpressionConstant, x, y)
    c.set_editor_property("r", float(layer.get("roughness", ROUGH_FALLBACK)))
    return c, ""


def _term(mat, channel, x, y):
    if channel == "base":
        c = _mel().create_material_expression(
            mat, unreal.MaterialExpressionConstant3Vector, x, y)
        c.set_editor_property("constant", unreal.LinearColor(
            BASE_COLOR_FALLBACK[0], BASE_COLOR_FALLBACK[1],
            BASE_COLOR_FALLBACK[2], 1.0))
        return c, ""
    if channel == "normal":
        c = _mel().create_material_expression(
            mat, unreal.MaterialExpressionConstant3Vector, x, y)
        c.set_editor_property("constant", unreal.LinearColor(0.0, 0.0, 1.0, 1.0))
        return c, ""
    c = _mel().create_material_expression(
        mat, unreal.MaterialExpressionConstant, x, y)
    c.set_editor_property("r", ROUGH_FALLBACK)
    return c, ""


def _build_chain(mat, channel, mp_prop, y0):
    """One LandscapeLayerWeight chain for a channel; returns ok bool."""
    prev, prev_pin = _term(mat, channel, -1900, y0)
    ok = True
    for i, layer in enumerate(LAYERS):
        x = -1600 + i * 220
        lw = _mel().create_material_expression(
            mat, unreal.MaterialExpressionLandscapeLayerWeight, x, y0)
        lw.set_editor_property("parameter_name", layer["name"])
        src, sp = _layer_src(mat, layer, channel, x - 180, y0 + 140)
        if src is None:
            _log("ABORT: %s layer %s source missing" % (channel,
                                                        layer["name"]))
            return False
        r1 = _mel().connect_material_expressions(src, sp, lw, "Layer")
        r2 = _mel().connect_material_expressions(prev, prev_pin, lw, "Base")
        _log("  %s/%s Layer<-%s(%r)=%s Base<-prev=%s"
             % (channel, layer["name"], src.get_class().get_name(),
                sp, r1, r2))
        ok = ok and r1 and r2
        prev, prev_pin = lw, ""
    rf = _mel().connect_material_property(prev, prev_pin, mp_prop)
    _log("  %s chain -> %s = %s" % (channel, mp_prop, rf))
    return ok and rf


def _build_grass_output(mat):
    go = _mel().create_material_expression(
        mat, unreal.MaterialExpressionLandscapeGrassOutput, -300, 700)
    gt = unreal.load_asset(GRASS_TYPE_PATH)
    if gt is None:
        _log("WARNING: %s not found - run build_landscape_grass.py MODE A "
             "first; GrassOutput will be wired but unbound until then"
             % GRASS_TYPE_PATH)
    entries = []
    for i, layer in enumerate(LAYERS):
        if not layer.get("grass"):
            continue
        ls = _mel().create_material_expression(
            mat, unreal.MaterialExpressionLandscapeLayerSample, -700,
            760 + i * 120)
        ls.set_editor_property("parameter_name", layer["name"])
        gi = unreal.GrassInput()
        gi.set_editor_property("name", layer["name"])
        if gt is not None:
            gi.set_editor_property("grass_type", gt)
        entries.append((gi, ls, layer["name"]))
    go.set_editor_property("grass_types", [e[0] for e in entries])
    # wire each LayerSample weight into the matching grass-type input pin
    for idx, (gi, ls, nm) in enumerate(entries):
        wired = False
        for pin in ("Input", "Grass", nm, str(idx)):
            try:
                if _mel().connect_material_expressions(ls, "", go, pin):
                    _log("  grass '%s' weight -> GrassOutput pin %r" % (nm, pin))
                    wired = True
                    break
            except Exception:
                continue
        if not wired:
            _log("  NOTE grass '%s': could not auto-wire LayerSample to a "
                 "GrassOutput pin - verify pin name in editor" % nm)
    _log("GrassOutput: %d grass entry(ies) bound to %s"
         % (len(entries), GRASS_TYPE_PATH))


def _reassign_landscape(mat):
    """Nuke+recreate makes a NEW UObject at the same path; the Landscape's
    landscape_material pointer goes to None (verified) and the terrain
    renders the default checker until reassigned. Re-bind it here."""
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    n = 0
    for a in eas.get_all_level_actors():
        if a.get_class().get_name() == "Landscape":
            a.set_editor_property("landscape_material", mat)
            n += 1
    _log("re-assigned %s to %d Landscape actor(s) (Paint UI 'Add From "
         "Material(s)' if target layers dropped)" % (MATERIAL_PATH, n))


def main():
    _log("=== RANGE MATERIAL REBUILD (MODE A) ===")
    mat = _backup_then_recreate()
    if mat is None:
        return
    ok = True
    ok = _build_chain(mat, "base", unreal.MaterialProperty.MP_BASE_COLOR,
                      0) and ok
    ok = _build_chain(mat, "normal", unreal.MaterialProperty.MP_NORMAL,
                      900) and ok
    ok = _build_chain(mat, "rough", unreal.MaterialProperty.MP_ROUGHNESS,
                      1800) and ok
    if not ok:
        _log("ABORT: a connect_* returned False - NOT saving the "
             "half-wired material. Backup %s is intact; restore it if the "
             "live material was already deleted." % BACKUP_PATH)
        return
    if BUILD_GRASS:
        _build_grass_output(mat)
    else:
        _log("BUILD_GRASS=False: skipping LandscapeGrassOutput (range has no 3D "
             "grass; set BUILD_GRASS=True to restore it)")
    _mel().recompile_material(mat)   # returns None by design; no exception=ok
    unreal.EditorAssetLibrary.save_asset(MATERIAL_PATH)
    _reassign_landscape(mat)
    tex = [l["name"] for l in LAYERS if l["mode"] == "textured"]
    _log("SAVED %s | textured=%r | backup=%s"
         % (MATERIAL_PATH, tex, BACKUP_PATH))
    _log("NEXT: USER assign M_PracticeRange to the range landscape (same path "
         "re-binds), Paint UI 'Add From Material(s)', then Manage > Import the "
         "courses/practice-range/splat_*.png into the Layers array.")
    _log("=== MODE A DONE ===")


if globals().get("MATERIAL_MODE") == "probe":
    _probe()
else:
    main()
