# build_course_material.py
#
# Reusable, idempotent rebuilder for the golfsim landscape material
# /Game/Materials/M_GolfsimCourse (Material-polish milestone). The existing
# material cannot be introspected/rewired (MaterialEditingLibrary has no
# get_material_expressions; Material.expressions is protected) so it is
# rebuilt from scratch. SAFETY: it is first duplicated to a backup, then
# nuke+recreated at the SAME path (canonical name + landscape soft-path
# binding preserved; fully reversible).
#
# Per-layer config: each of the 7 painted layers is either "flat" (a
# Constant3Vector, the current placeholder behavior) or "textured" (a tiled
# Megascans TextureSample chain: albedo/normal/roughness). Fairway starts
# textured; replicating to Green/Tee/Rough = flip "mode" + supply paths +
# re-run. A LandscapeGrassOutput node binds the Fairway-keyed grass type
# (built by build_landscape_grass.py).
#
#   MODE PROBE - resolve UNKNOWNS, mutate nothing persistent (run FIRST):
#     MATERIAL_MODE="probe"; exec(compile(open(r"<repo>\engine\scripts"
#       r"\build_course_material.py",
#       encoding="utf-8").read(),"build_course_material.py","exec"))
#
#   MODE A - backup + rebuild + save (default):
#     exec(compile(open(r"<repo>\engine\scripts"
#       r"\build_course_material.py",encoding="utf-8").read(),
#       "build_course_material.py","exec"))
#
# bridge note: execute_unreal_python returns output:None. Feedback via
# unreal.log() under LogPython; read with get_log_lines. recompile_material
# returns None BY DESIGN (success == no exception, not a truthy return).
# Constant3Vector output pin is "" (empty), NOT "RGB" (silent-False trap).
# Textured-layer texture paths are Megascans Bridge downloads (gitignored,
# per-machine) the USER supplies; build FAILS LOUD on an unloadable path.

import unreal

# ---------------------------------------------------------------- parameters
MATERIAL_PATH = "/Game/Materials/M_GolfsimCourse"
BACKUP_PATH   = "/Game/Materials/M_GolfsimCourse_PreGrassBackup"
# Live-tuning Material Instance (GOL-163): the base holds param DEFAULTS; the landscape uses this MIC
# so per-surface tint/roughness/stripe params are adjustable in the Details panel with no rebuild.
MIC_PATH      = "/Game/Materials/MIC_GolfsimCourse"

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

# All 7 painted layers textured, ordered EXACTLY as the existing chain.
# tiling = LandscapeLayerCoords.mapping_scale (higher = finer / more
# repeats); per-layer starting guesses, tune live. grass=True keys the
# LandscapeGrassOutput (Fairway only for now).
# GOL-163 per-surface look = live-tunable params (defaults here, adjust on MIC_GolfsimCourse):
#   tint     = HUE-REPLACE VectorParameter <name>_Tint (desaturate->multiply; forces a target hue)
#   mul_tint = PRESERVE+NUDGE VectorParameter <name>_Tint (plain multiply; white=identity, keeps the
#              texture's own color -- used for the sand so the current look is the default)
#   rough_default = ScalarParameter <name>_Rough default (multiplies ORM-G; <1 sheen, >1 dull)
# Green/Tee currently still share Lawn_Grass with Fairway -- distinguished by tint/roughness/tiling
# now; swap in dedicated Fab textures (P2) when downloaded.
LAYERS = [
    dict(name="Fairway",  mode="textured", grass=True, tiling=3.0,
         tint=(0.10, 0.26, 0.08), rough_default=1.0,
         **_surf("Lawn_Grass_tkynejer", "tkynejer")),
    dict(name="Green",    mode="textured", tiling=4.5,
         tint=(0.07, 0.22, 0.10), rough_default=0.75,   # darker, cooler, finer, wetter sheen
         **_surf("Lawn_Grass_tkynejer", "tkynejer")),
    dict(name="Bunker",   mode="textured", tiling=3.0,
         mul_tint=(1.0, 1.0, 1.0), rough_default=1.0,   # keep current sand; nudge-able neutral default
         **_surf("Bright_Desert_Sand_sjzkfega", "sjzkfega")),
    dict(name="Rough",    mode="textured", tiling=2.0,
         tint=(0.15, 0.18, 0.09), rough_default=1.15,   # duller, browner, less saturated
         **_surf("Uncut_Grass_oilpt20", "oilpt20")),
    dict(name="CartPath", mode="textured", tiling=4.0,
         **_surf("Concrete_Floor_virrebs", "virrebs")),
    dict(name="Tee",      mode="textured", tiling=3.0,
         tint=(0.13, 0.23, 0.08), rough_default=1.1,    # worn, slightly yellower than fairway
         **_surf("Lawn_Grass_tkynejer", "tkynejer")),
    dict(name="Trees",    mode="textured", tiling=2.5,
         **_surf("Clover_Patches_on_Grass_sgmkajak", "sgmkajak")),
]
BASE_COLOR_FALLBACK = (0.05, 0.20, 0.05)   # chain terminator (current behavior)
ROUGH_FALLBACK      = 0.70
GRASS_TYPE_PATH     = "/Game/Landscape/Grass/LGT_FairwayGrass"

_PROBE_MAT = "/Game/Materials/_PROBE_MATBUILD_DELETE"


def _log(msg):
    unreal.log("COURSE_MAT: " + str(msg))


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
    # GrassOutput.grass_types / GrassInput real UPROPERTY names
    eal = unreal.EditorAssetLibrary
    if eal.does_asset_exist(_PROBE_MAT):
        eal.delete_asset(_PROBE_MAT)
    try:
        mat = _at().create_asset("_PROBE_MATBUILD_DELETE", "/Game/Materials",
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
        # LayerBlend.layers authorability (decides chains vs LayerBlend)
        lb = _mel().create_material_expression(
            mat, unreal.MaterialExpressionLandscapeLayerBlend, -600, 0)
        try:
            lv = unreal.LayerBlendInput() if hasattr(
                unreal, "LayerBlendInput") else None
            _log("LayerBlendInput=%s" % (
                [m for m in dir(lv) if not m.startswith("_")] if lv else None))
            lb.set_editor_property("layers", [lv] if lv else [])
            rb = lb.get_editor_property("layers")
            _log("LayerBlend.layers set->readback len=%s (AUTHORABLE=%s)"
                 % (len(rb) if hasattr(rb, "__len__") else "?", bool(rb)
                    or lv is None))
        except Exception as exc:
            _log("LayerBlend.layers ERR %s -> use 3 parallel chains (default)"
                 % str(exc)[:70])
        eal.delete_asset(_PROBE_MAT)
        _log("probe material deleted")
    except Exception as exc:
        _log("probe ERR %s" % str(exc)[:120])
    # best-effort read of the live flat colors
    m = unreal.load_asset(MATERIAL_PATH)
    _log("M_GolfsimCourse loaded=%s; expressions introspection is "
         "protected in 5.7 - USER must CONFIRM the 6 non-Fairway flat "
         "colors from the material editor against the LAYERS defaults "
         "before MODE A (R4)." % (m is not None))
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
    # SHARED sampler: 7 textured layers x 3 textures (+macro) far exceeds
    # the 16 per-material sampler limit unless they share the world group
    # sampler. Without this the material won't compile at full replication.
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
    # plain multiply can only darken (can't hue-shift), which is why tint
    # tweaks were imperceptible. `tint` here is the KBG turf-green target so
    # the base inherently meshes with the KBG 3D grass at the cull boundary.
    # Per-surface recolor as a live-tunable VectorParameter <name>_Tint (MIC-adjustable, no rebuild):
    #   tint     -> HUE-REPLACE: desaturate->multiply (forces the target hue; for the grass surfaces).
    #   mul_tint -> PRESERVE+NUDGE: plain multiply (keeps the texture's own color; white=identity).
    tint = layer.get("tint")
    mul_tint = layer.get("mul_tint")
    col = tint or mul_tint
    if not col:
        return src, pin
    vp = _mel().create_material_expression(
        mat, unreal.MaterialExpressionVectorParameter, x + 400, y + 220)
    vp.set_editor_property("parameter_name", layer["name"] + "_Tint")
    vp.set_editor_property(
        "default_value", unreal.LinearColor(col[0], col[1], col[2], 1.0))
    mul = _mel().create_material_expression(
        mat, unreal.MaterialExpressionMultiply, x + 560, y)
    if tint:
        des = _mel().create_material_expression(
            mat, unreal.MaterialExpressionDesaturation, x + 380, y)
        _mel().connect_material_expressions(src, pin, des, "")   # -> luminance, then re-hue
        _mel().connect_material_expressions(des, "", mul, "A")
    else:
        _mel().connect_material_expressions(src, pin, mul, "A")  # preserve texture color
    _mel().connect_material_expressions(vp, "", mul, "B")
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
        if ts is None:
            return (None, None)
        ch = layer.get("rough_ch", "R")
        # Per-surface roughness as a live-tunable ScalarParameter <name>_Rough (MIC-adjustable):
        # multiply the ORM-G roughness (default 1.0). <1 = wetter/sheen (greens), >1 = duller (tee/rough).
        rp = _mel().create_material_expression(
            mat, unreal.MaterialExpressionScalarParameter, x, y + 200)
        rp.set_editor_property("parameter_name", layer["name"] + "_Rough")
        rp.set_editor_property(
            "default_value", float(layer.get("rough_default", 1.0)))
        rmul = _mel().create_material_expression(
            mat, unreal.MaterialExpressionMultiply, x + 200, y)
        _mel().connect_material_expressions(ts, ch, rmul, "A")
        _mel().connect_material_expressions(rp, "", rmul, "B")
        return (rmul, "")
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
         "Material(s)' if target layers dropped)" % (mat.get_name(), n))


def _build_mic(base_mat):
    """Create MIC_GolfsimCourse instancing the base material + return it for the landscape (GOL-163).
    The base holds the per-surface param DEFAULTS (from LAYERS); the MIC starts all-inherited and is
    the live-tuning surface (tweak <name>_Tint / <name>_Rough in its Details panel -- no rebuild)."""
    eal = unreal.EditorAssetLibrary
    if eal.does_asset_exist(MIC_PATH):
        eal.delete_asset(MIC_PATH)
    name = MIC_PATH.rsplit("/", 1)[1]
    pkg = MIC_PATH.rsplit("/", 1)[0]
    try:
        mic = _at().create_asset(name, pkg, unreal.MaterialInstanceConstant,
                                 unreal.MaterialInstanceConstantFactoryNew())
        _mel().set_material_instance_parent(mic, base_mat)
        eal.save_asset(MIC_PATH)
        _log("built MIC %s (parent=M_GolfsimCourse; tune surfaces here, no rebuild)"
             % MIC_PATH)
        return mic
    except Exception as exc:
        _log("MIC build ERR %s - landscape will use the base material instead"
             % str(exc)[:90])
        return None


def main():
    _log("=== COURSE MATERIAL REBUILD (MODE A) ===")
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
    # GOL-163/GOL-170: the course ships grass-free; 3D grass is its own epic. Re-running this canonical
    # builder must NOT re-attach the dormant grass node. Set BUILD_GRASS=True (globals) when GOL-170 wires it.
    if globals().get("BUILD_GRASS", False):
        _build_grass_output(mat)
    else:
        _log("BUILD_GRASS=False: course material ships grass-free (3D grass = GOL-170)")
    _mel().recompile_material(mat)   # returns None by design; no exception=ok
    unreal.EditorAssetLibrary.save_asset(MATERIAL_PATH)
    mic = _build_mic(mat)
    _reassign_landscape(mic if mic is not None else mat)
    flat = [l["name"] for l in LAYERS if l["mode"] == "flat"]
    tex = [l["name"] for l in LAYERS if l["mode"] == "textured"]
    _log("SAVED %s (+ %s) | textured=%r flat=%r | backup=%s"
         % (MATERIAL_PATH, MIC_PATH, tex, flat, BACKUP_PATH))
    _log("NEXT: USER confirm the Landscape uses MIC_GolfsimCourse; tune each surface's "
         "<name>_Tint / <name>_Rough on the MIC (live, no rebuild), then back-port finals to LAYERS.")
    _log("=== MODE A DONE ===")


if globals().get("MATERIAL_MODE") == "probe":
    _probe()
else:
    main()
