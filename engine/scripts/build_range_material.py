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
#     MATERIAL_MODE="probe"; exec(compile(open(r"<repo>\engine\scripts"
#       r"\build_range_material.py",
#       encoding="utf-8").read(),"build_range_material.py","exec"))
#
#   MODE A - backup + rebuild + save (default):
#     exec(compile(open(r"<repo>\engine\scripts"
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
# Live-tuning Material Instance (GOL-163): the base holds param DEFAULTS; the landscape uses this MIC
# so per-surface tint/roughness/stripe params are adjustable in the Details panel with no rebuild.
MIC_PATH      = "/Game/Materials/MIC_PracticeRange"

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
         tint=(0.192, 0.561, 0.155), rough_default=1.0,
         # criss-cross mown range lane: a true checkerboard of crisp mown squares
         # (sharpness pushes the rows to square waves; contrast = light/dark delta).
         # Geometry baked from live tuning (angle 45, width 4 m); dial these on
         # MIC_PracticeRange (Fairway_Stripe{Width,Angle,Contrast,Sharpness}).
         stripes=dict(width_m=4.0, angle_deg=45.0, contrast=0.18, sharpness=4.0,
                      crisscross=True),
         variation=dict(amount=0.06, scale=0.0006),                 # GOL-163 P4: tonal patches over the lane
         **_surf("Lawn_Grass_tkynejer", "tkynejer")),
    dict(name="Rough",    mode="textured", tiling=2.0,
         variation=dict(amount=0.10, scale=0.0005),                 # GOL-163 P4: rough reads patchy/natural
         **_surf("Uncut_Grass_oilpt20", "oilpt20")),
    dict(name="Tee",      mode="textured", tiling=3.0,
         tint=(0.10, 0.26, 0.08),
         variation=dict(amount=0.05, scale=0.0010),                 # GOL-163 P4
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


def _stripe_mask(mat, layer, x, y):
    """Mowing stripes (GOL-163): a WORLD-aligned brightness band -> scalar multiplier ~[1-c, 1+c]
    to multiply into the albedo. World-position-driven so rows stay straight regardless of texture
    tiling/terrain. Width(m)/angle(deg)/contrast/sharpness are live-tunable ScalarParameters on the
    MIC; contrast=0 disables the stripes. UE Sine/Cosine use a Period (default 1.0) -> input is in
    TURNS, so feed degrees/360 and coord/width directly (no manual 2*pi).

    Each band is a Sine pushed toward a SQUARE wave by <name>_StripeSharpness (clamp(sin*sharpness,
    -1, 1)): sharpness 1 = soft sine rows (default), higher = crisp flat-topped stripes with an
    anti-aliased edge. cfg['crisscross']=True multiplies the two perpendicular *square band signals*
    (NOT two brightness factors) -> a true CHECKERBOARD of mown squares: the sign product is +1 in
    two diagonal cells and -1 in the others, then contrast is applied once. (Multiplying brightness
    factors instead peaks at the corners -> soft blobs / "splotches"; that was the original bug.)"""
    name = layer["name"]
    cfg = layer["stripes"]
    me = _mel().create_material_expression
    cn = _mel().connect_material_expressions

    def _const(cx, cy, val):
        c = me(mat, unreal.MaterialExpressionConstant, cx, cy)
        c.set_editor_property("r", float(val))
        return c

    def _scalar(cx, cy, pname, default):
        p = me(mat, unreal.MaterialExpressionScalarParameter, cx, cy)
        p.set_editor_property("parameter_name", name + pname)
        p.set_editor_property("default_value", float(default))
        return p

    def _mask(cx, cy, comp):
        m = me(mat, unreal.MaterialExpressionComponentMask, cx, cy)
        for c in ("r", "g", "b", "a"):
            m.set_editor_property(c, c == comp)
        cn(wp, "", m, "")
        return m

    def _mul(cx, cy, a, ap, b, bp):
        m = me(mat, unreal.MaterialExpressionMultiply, cx, cy)
        cn(a, ap, m, "A"); cn(b, bp, m, "B")
        return m

    wp = me(mat, unreal.MaterialExpressionWorldPosition, x, y)
    wx = _mask(x + 180, y - 60, "r")
    wy = _mask(x + 180, y + 60, "g")

    # shared knobs: angle(deg), width(m->cm), contrast, sharpness -- all live-tunable params.
    ang = _scalar(x, y + 180, "_StripeAngle", cfg.get("angle_deg", 0.0))
    wid = _scalar(x + 520, y + 180, "_StripeWidth", cfg.get("width_m", 1.5))
    wcm = _mul(x + 680, y + 180, wid, "", _const(x + 520, y + 250, 100.0), "")
    con = _scalar(x + 1100, y + 340, "_StripeContrast", cfg.get("contrast", 0.08))
    shp = _scalar(x + 360, y + 340, "_StripeSharpness", cfg.get("sharpness", 1.0))

    def _band(yoff, offset_deg):
        """One mower direction -> band signal in ~[-1,1], a Sine sharpened toward a square wave.
        offset_deg rotates this band off the shared StripeAngle (90 = perpendicular)."""
        # (angle + offset)(deg) -> turns -> cos/sin (Period=1 => cos(2pi*turns))
        if offset_deg:
            asrc = me(mat, unreal.MaterialExpressionAdd, x, y + yoff)
            cn(ang, "", asrc, "A")
            cn(_const(x - 160, y + yoff + 60, float(offset_deg)), "", asrc, "B")
        else:
            asrc = ang
        turns = _mul(x + 180, y + yoff, asrc, "",
                     _const(x, y + yoff + 90, 1.0 / 360.0), "")
        cosn = me(mat, unreal.MaterialExpressionCosine, x + 340, y + yoff - 40)
        cn(turns, "", cosn, "")
        sinn = me(mat, unreal.MaterialExpressionSine, x + 340, y + yoff + 40)
        cn(turns, "", sinn, "")
        # coord = wx*cos + wy*sin  (world distance perpendicular to the rows)
        coord = me(mat, unreal.MaterialExpressionAdd, x + 680, y + yoff)
        cn(_mul(x + 520, y + yoff - 40, wx, "", cosn, ""), "", coord, "A")
        cn(_mul(x + 520, y + yoff + 60, wy, "", sinn, ""), "", coord, "B")
        # phase = coord / (width_m * 100 cm) -> Sine(Period=1) = a soft row, -1..1
        div = me(mat, unreal.MaterialExpressionDivide, x + 840, y + yoff)
        cn(coord, "", div, "A"); cn(wcm, "", div, "B")
        raw = me(mat, unreal.MaterialExpressionSine, x + 1000, y + yoff)
        cn(div, "", raw, "")
        # sharpen toward a square wave: clamp(sin * sharpness, -1, 1)
        wide = _mul(x + 1160, y + yoff, raw, "", shp, "")
        sq = me(mat, unreal.MaterialExpressionClamp, x + 1320, y + yoff)
        sq.set_editor_property("min_default", -1.0)
        sq.set_editor_property("max_default", 1.0)
        cn(wide, "", sq, "")
        return sq

    sig = _band(40, 0.0)
    if cfg.get("crisscross"):
        sig = _mul(x + 1520, y + 240, sig, "", _band(460, 90.0), "")   # checkerboard sign product
    # brightness = 1 + sig * contrast
    bright = me(mat, unreal.MaterialExpressionAdd, x + 1720, y + 120)
    cn(_const(x + 1560, y + 60, 1.0), "", bright, "A")
    cn(_mul(x + 1560, y + 180, sig, "", con, ""), "", bright, "B")
    return bright, ""


def _region_variation(mat, layer, x, y):
    """Per-region color variation (GOL-163 P4): low-frequency procedural Noise on world position ->
    a scalar brightness multiplier ~[1-amt, 1+amt] to multiply into the albedo, so every fairway/green
    has gentle color variance over its length with no obvious texture-tile repeat on flyovers. The macro
    blend (above) breaks the texture repeat; this breaks the FLAT overall tone. Returns (expr, '').

    PROCEDURAL (MaterialExpressionNoise, GRADIENT_ALU) -> adds NO texture sampler (the material already
    sits near the 16-sampler limit). World-position-driven like _stripe_mask so patches stay put
    regardless of texture tiling/terrain. Two live-tunable ScalarParameters on the MIC:
      <name>_VarScale  = world-space frequency (cm^-1); LOWER = larger, lower-frequency patches.
      <name>_VarAmount = amplitude; 0 disables. brightness = 1 + noise(-1..1) * amount.
    Noise-node quirks (UE 5.7, confirmed by probe): the function property is `noise_function` (NOT
    `function`); the Position INPUT pin name is "" (empty), as is the output pin -- so the scaled world
    position feeds the empty-named input and the node output reads off ""."""
    name = layer["name"]
    cfg = layer["variation"]
    me = _mel().create_material_expression
    cn = _mel().connect_material_expressions

    # world position -> * <name>_VarScale (frequency knob) -> Noise Position input (pin name "")
    wp = me(mat, unreal.MaterialExpressionWorldPosition, x, y)
    scl = me(mat, unreal.MaterialExpressionScalarParameter, x, y + 160)
    scl.set_editor_property("parameter_name", name + "_VarScale")
    scl.set_editor_property("default_value", float(cfg.get("scale", 0.0006)))
    pos = me(mat, unreal.MaterialExpressionMultiply, x + 200, y)
    cn(wp, "", pos, "A"); cn(scl, "", pos, "B")

    nz = me(mat, unreal.MaterialExpressionNoise, x + 400, y)
    try:
        nz.set_editor_property(
            "noise_function", unreal.NoiseFunction.NOISEFUNCTION_GRADIENT_ALU)  # ALU = tex-free, no sampler
    except Exception as exc:
        _log("noise_function note: %s" % str(exc)[:50])
    for prop, val in (("quality", 1), ("levels", 2), ("turbulence", False),
                      ("output_min", -1.0), ("output_max", 1.0), ("scale", 1.0)):
        try:
            nz.set_editor_property(prop, val)
        except Exception as exc:
            _log("noise %s note: %s" % (prop, str(exc)[:40]))
    cn(pos, "", nz, "")   # scaled world position -> Noise Position input (empty pin name)

    # brightness = 1 + noise(-1..1) * <name>_VarAmount
    amt = me(mat, unreal.MaterialExpressionScalarParameter, x + 400, y + 200)
    amt.set_editor_property("parameter_name", name + "_VarAmount")
    amt.set_editor_property("default_value", float(cfg.get("amount", 0.06)))
    sig = me(mat, unreal.MaterialExpressionMultiply, x + 580, y)
    cn(nz, "", sig, "A"); cn(amt, "", sig, "B")
    one = me(mat, unreal.MaterialExpressionConstant, x + 580, y + 200)
    one.set_editor_property("r", 1.0)
    bright = me(mat, unreal.MaterialExpressionAdd, x + 740, y)
    cn(one, "", bright, "A"); cn(sig, "", bright, "B")
    return bright, ""


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
        out, out_pin = src, pin
    else:
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
        out, out_pin = mul, ""
    # GOL-163 P4 per-region variation: multiply a low-frequency world-noise brightness band into the
    # albedo so the surface's overall tone varies naturally over its length (no flat-tile look on flyovers).
    if layer.get("variation"):
        rv, _rp = _region_variation(mat, layer, x + 200, y - 520)
        vmul = _mel().create_material_expression(
            mat, unreal.MaterialExpressionMultiply, x + 660, y - 40)
        _mel().connect_material_expressions(out, out_pin, vmul, "A")
        _mel().connect_material_expressions(rv, "", vmul, "B")
        out, out_pin = vmul, ""
    # GOL-163 mowing stripes: multiply a world-aligned brightness band into the albedo (contrast=0 off).
    if layer.get("stripes"):
        sm, _sp = _stripe_mask(mat, layer, x + 200, y + 560)
        smul = _mel().create_material_expression(
            mat, unreal.MaterialExpressionMultiply, x + 760, y)
        _mel().connect_material_expressions(out, out_pin, smul, "A")
        _mel().connect_material_expressions(sm, "", smul, "B")
        return smul, ""
    return out, out_pin


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
    """Create MIC_PracticeRange instancing the base material + return it for the landscape (GOL-163).
    The base holds the per-surface param DEFAULTS (from LAYERS); the MIC starts all-inherited and is
    the live-tuning surface (tweak <name>_Tint / <name>_Rough / Fairway_Stripe* in its Details panel
    -- no rebuild)."""
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
        _log("built MIC %s (parent=M_PracticeRange; tune surfaces here, no rebuild)"
             % MIC_PATH)
        return mic
    except Exception as exc:
        _log("MIC build ERR %s - landscape will use the base material instead"
             % str(exc)[:90])
        return None


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
    mic = _build_mic(mat)
    _reassign_landscape(mic if mic is not None else mat)
    tex = [l["name"] for l in LAYERS if l["mode"] == "textured"]
    _log("SAVED %s (+ %s) | textured=%r | backup=%s"
         % (MATERIAL_PATH, MIC_PATH, tex, BACKUP_PATH))
    _log("NEXT: USER confirm the range Landscape uses MIC_PracticeRange; tune each surface's "
         "<name>_Tint / <name>_Rough / Fairway_Stripe* on the MIC (live, no rebuild), then "
         "back-port finals to LAYERS. Paint UI 'Add From Material(s)' + re-import the "
         "courses/practice-range/splat_*.png only if target layers dropped.")
    _log("=== MODE A DONE ===")


if globals().get("MATERIAL_MODE") == "probe":
    _probe()
else:
    main()
