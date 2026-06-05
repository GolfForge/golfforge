"""Author MPC_GolfWind + M_FlagWind for AGolfPinActor's fluttering flag (GOL-165).

Two assets under /Game/Materials:
  - MPC_GolfWind (MaterialParameterCollection): the single wind source.
      WindDirection (vector, world XY in RG; default a gentle SE-ish breeze)
      WindStrength  (scalar 0..1; default 0.4 = calm-moderate)
    Both the flag (now) and trees (follow-up) sample it; GOL-154 live weather sets it at runtime.
  - M_FlagWind (Material): two-sided, opaque. BaseColor is the branded T_GolfForgeFlag texture
    (golden tee + "GolfForge" wordmark on flag-red), flat Roughness, and a World-Position-Offset
    flutter:
      U = TexCoord.x (0 at the pole-attached edge -> 1 at the free edge; the procedural flag grid
      authors UVs this way). wave = Sine(Time*Freq - U*WaveDensity); mag = wave * U(mask) *
      WindStrength * PeakAmp; WPO = WindDirection * mag (perpendicular flutter in world space).
    Freq / WaveDensity / PeakAmp are scalar params (tunable). UE Sine Period=1 -> inputs in turns.

Idempotent: re-running deletes + recreates both assets.

How to run (one-time bootstrap; commit the .uasset to LFS after):
    exec(compile(open(r"<repo>\\engine\\scripts\\build_flag_wind_material.py",
        encoding="utf-8").read(), "build_flag_wind_material.py", "exec"))

Until this runs the C++ falls back to a static red BasicShapeMaterial (a frozen flag).
"""
import unreal

DIR = "/Game/Materials"
MPC_NAME = "MPC_GolfWind"
MAT_NAME = "M_FlagWind"
TEX_NAME = "T_GolfForgeFlag"           # branded flag art (golden tee + wordmark on red)
MPC_PATH = f"{DIR}/{MPC_NAME}"
MAT_PATH = f"{DIR}/{MAT_NAME}"
TEX_PATH = f"{DIR}/{TEX_NAME}"

mel = unreal.MaterialEditingLibrary
eal = unreal.EditorAssetLibrary
at = unreal.AssetToolsHelpers.get_asset_tools()


def _log(m):
    unreal.log("FLAG_WIND: " + str(m))


# ---------------------------------------------------------------- MPC_GolfWind
# Delete the dependent material BEFORE the collection, so the old M_FlagWind doesn't transiently
# recompile against a just-deleted MPC (harmless but noisy "invalid Collection" warnings).
if eal.does_asset_exist(MAT_PATH):
    eal.delete_asset(MAT_PATH)
if eal.does_asset_exist(MPC_PATH):
    eal.delete_asset(MPC_PATH)

mpc_factory = getattr(unreal, "MaterialParameterCollectionFactoryNew", None)
mpc = at.create_asset(MPC_NAME, DIR, unreal.MaterialParameterCollection,
                      mpc_factory() if mpc_factory else None)
if mpc is None:
    raise RuntimeError("Failed to create " + MPC_PATH)


# `id` (the per-param GUID) is protected in Python -- the engine assigns it on the set_editor_property
# PostEditChange + save (UMaterialParameterCollection fills any invalid GUIDs). So just set name+default.
vp = unreal.CollectionVectorParameter()
vp.set_editor_property("parameter_name", "WindDirection")
vp.set_editor_property("default_value", unreal.LinearColor(0.8, 0.3, 0.0, 0.0))  # world XY in RG

sp = unreal.CollectionScalarParameter()
sp.set_editor_property("parameter_name", "WindStrength")
sp.set_editor_property("default_value", 0.5)   # calm-moderate; ~5cm flag-tip flutter with PeakAmp

mpc.set_editor_property("vector_parameters", [vp])
mpc.set_editor_property("scalar_parameters", [sp])
eal.save_asset(MPC_PATH)
_log("created %s (WindDirection vec, WindStrength scalar; GUIDs engine-assigned)" % MPC_PATH)


# ---------------------------------------------------------------- M_FlagWind
if eal.does_asset_exist(MAT_PATH):
    eal.delete_asset(MAT_PATH)

mat = at.create_asset(MAT_NAME, DIR, unreal.Material, unreal.MaterialFactoryNew())
if mat is None:
    raise RuntimeError("Failed to create " + MAT_PATH)
mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
mat.set_editor_property("two_sided", True)   # flags read from both faces


def _expr(cls, x, y):
    return mel.create_material_expression(mat, cls, x, y)


def _scalar(name, default, x, y):
    p = _expr(unreal.MaterialExpressionScalarParameter, x, y)
    p.set_editor_property("parameter_name", name)
    p.set_editor_property("default_value", float(default))
    return p


def _coll(param_name, x, y):
    c = _expr(unreal.MaterialExpressionCollectionParameter, x, y)
    c.set_editor_property("collection", mpc)
    c.set_editor_property("parameter_name", param_name)
    return c


def _mul(a, ap, b, bp, x, y):
    m = _expr(unreal.MaterialExpressionMultiply, x, y)
    mel.connect_material_expressions(a, ap, m, "A")
    mel.connect_material_expressions(b, bp, m, "B")
    return m


# BaseColor: the branded GolfForge flag texture (golden tee + wordmark on flag-red). The color is
# baked into the texture, so the actor's MakeColorMID "Color" set is a harmless no-op here -- it
# only tints the BasicShapeMaterial fallback when M_FlagWind is absent on a fresh clone.
flag_tex = eal.load_asset(TEX_PATH)
tex = _expr(unreal.MaterialExpressionTextureSample, -500, -250)
if flag_tex is not None:
    tex.set_editor_property("texture", flag_tex)
else:
    _log("WARNING: %s not found -- import it first; BaseColor will be black" % TEX_PATH)
# The flag grid authors UV (0,0) at the pole-bottom corner (U along +Y, V up) so the WPO chain below
# can anchor the pole at U=0. Sampled raw, the art lands 180-deg-rotated on the +X face, and a
# two-sided flag also mirrors on its back face. Correct both for the texture lookup ONLY (the WPO
# still reads the raw UV, so the PNG stays right-side-up and the flutter physics are unchanged):
#   tex.v = 1 - v               -> flip vertical (right-side-up)
#   tex.u = 0.5 + sign*(0.5-u)  -> TwoSidedSign gives front=1-u, back=u, so the wordmark reads
#                                  correctly from either side (a double-sided print, no mirror).
flag_uv = _expr(unreal.MaterialExpressionTextureCoordinate, -1180, -420)
u_r = _expr(unreal.MaterialExpressionComponentMask, -1000, -480)
u_r.set_editor_property("r", True); u_r.set_editor_property("g", False)
u_r.set_editor_property("b", False); u_r.set_editor_property("a", False)
mel.connect_material_expressions(flag_uv, "", u_r, "")
v_g = _expr(unreal.MaterialExpressionComponentMask, -1000, -340)
v_g.set_editor_property("r", False); v_g.set_editor_property("g", True)
v_g.set_editor_property("b", False); v_g.set_editor_property("a", False)
mel.connect_material_expressions(flag_uv, "", v_g, "")
v_flip = _expr(unreal.MaterialExpressionOneMinus, -820, -340)
mel.connect_material_expressions(v_g, "", v_flip, "")
half = _expr(unreal.MaterialExpressionConstant, -1000, -600)
half.set_editor_property("r", 0.5)
tss = _expr(unreal.MaterialExpressionTwoSidedSign, -1000, -680)
half_minus_u = _expr(unreal.MaterialExpressionSubtract, -820, -560)
mel.connect_material_expressions(half, "", half_minus_u, "A")
mel.connect_material_expressions(u_r, "", half_minus_u, "B")
signed = _mul(tss, "", half_minus_u, "", -640, -600)
u_final = _expr(unreal.MaterialExpressionAdd, -460, -560)
mel.connect_material_expressions(half, "", u_final, "A")
mel.connect_material_expressions(signed, "", u_final, "B")
uv2 = _expr(unreal.MaterialExpressionAppendVector, -280, -440)
mel.connect_material_expressions(u_final, "", uv2, "A")
mel.connect_material_expressions(v_flip, "", uv2, "B")
mel.connect_material_expressions(uv2, "", tex, "UVs")
mel.connect_material_property(tex, "RGB", unreal.MaterialProperty.MP_BASE_COLOR)

# Roughness: flat, fabric is matte.
rough = _expr(unreal.MaterialExpressionConstant, -500, -80)
rough.set_editor_property("r", 0.7)
mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)

# --- WPO flutter -------------------------------------------------------------
uv = _expr(unreal.MaterialExpressionTextureCoordinate, -1100, 250)
u = _expr(unreal.MaterialExpressionComponentMask, -920, 250)   # U = along-flag (0 pole -> 1 free)
u.set_editor_property("r", True)
u.set_editor_property("g", False)
u.set_editor_property("b", False)
u.set_editor_property("a", False)
mel.connect_material_expressions(uv, "", u, "")

time = _expr(unreal.MaterialExpressionTime, -1100, 420)
freq = _scalar("Freq", 1.0, -1100, 540)            # turns/sec (UE Sine Period=1)
wavedensity = _scalar("WaveDensity", 2.0, -1100, 640)  # turns across the flag
peakamp = _scalar("PeakAmp", 10.0, -1100, 740)     # cm peak displacement (free edge, * WindStrength)

tf = _mul(time, "", freq, "", -900, 460)           # Time * Freq
uw = _mul(u, "", wavedensity, "", -900, 600)       # U * WaveDensity
phase = _expr(unreal.MaterialExpressionSubtract, -720, 500)
mel.connect_material_expressions(tf, "", phase, "A")
mel.connect_material_expressions(uw, "", phase, "B")
wave = _expr(unreal.MaterialExpressionSine, -560, 500)
mel.connect_material_expressions(phase, "", wave, "")

wind_str = _coll("WindStrength", -560, 700)
wind_dir = _coll("WindDirection", -200, 760)

sm = _mul(wave, "", u, "", -380, 520)              # wave * U (mask: pole edge anchored)
smw = _mul(sm, "", wind_str, "", -200, 560)        # * WindStrength
mag = _mul(smw, "", peakamp, "", -40, 580)         # * PeakAmp -> scalar cm
wpo = _mul(wind_dir, "", mag, "", 160, 640)        # WindDirection(world XY) * mag -> float3
mel.connect_material_property(wpo, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)

mel.recompile_material(mat)
eal.save_asset(MAT_PATH)
_log("created %s (two-sided, WPO flutter sampling %s)" % (MAT_PATH, MPC_PATH))
_log("DONE")
