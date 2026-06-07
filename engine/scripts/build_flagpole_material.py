"""Author /Game/Materials/M_FlagPole.uasset for AGolfPinActor's flagstick.

The pole was a flat white cylinder. This gives it the classic striped flagstick read: alternating
white/black horizontal bands up the pole. Pure procedural (no texture) -- the bands come from the
cylinder's V texcoord (V runs along the pole height), so it costs nothing and never needs an asset.

Graph:
  band = Floor(Frac(V * StripeCount) + 0.5)   -> 0 for the lower half of each period, 1 for the upper
  BaseColor = Lerp(White, Black, band)        -> alternating bands
  Roughness = 0.4 (painted metal, slight sheen)
StripeCount / PoleWhite / PoleBlack / PoleRoughness are params for live tuning. If the bands come out
vertical (UV orientation differs), flip the masked channel from G to R in MASK_CHANNEL below.

Idempotent: re-running deletes + recreates the asset.

Run in the UE5.7 editor Python interpreter via execute_unreal_python (close PIE first):
    exec(compile(open(r"<repo>\\engine\\scripts\\build_flagpole_material.py",
        encoding="utf-8").read(), "build_flagpole_material.py", "exec"))

bridge note: execute_unreal_python returns output:None. Feedback via unreal.log() under LogPython.
"""
import unreal

DIR = "/Game/Materials"
MAT_NAME = "M_FlagPole"
MAT_PATH = f"{DIR}/{MAT_NAME}"

MASK_CHANNEL = "g"     # cylinder V (height) -> horizontal bands; switch to "r" if they come vertical
DEFAULT_STRIPES = 6.0  # band periods up the pole (each period = one white + one black band)

mel = unreal.MaterialEditingLibrary
eal = unreal.EditorAssetLibrary
at = unreal.AssetToolsHelpers.get_asset_tools()


def _log(m):
    unreal.log("FLAGPOLE_MAT: " + str(m))


def main():
    _log("=== FLAGPOLE MATERIAL START ===")
    if eal.does_asset_exist(MAT_PATH):
        eal.delete_asset(MAT_PATH)
    mat = at.create_asset(MAT_NAME, DIR, unreal.Material, unreal.MaterialFactoryNew())
    if mat is None:
        raise RuntimeError("failed to create " + MAT_PATH)

    def _expr(cls, x, y):
        return mel.create_material_expression(mat, cls, x, y)

    # V coord along the pole height: TexCoord -> ComponentMask (G).
    uv = _expr(unreal.MaterialExpressionTextureCoordinate, -900, 0)
    vmask = _expr(unreal.MaterialExpressionComponentMask, -700, 0)
    for c in ("r", "g", "b", "a"):
        vmask.set_editor_property(c, c == MASK_CHANNEL)
    mel.connect_material_expressions(uv, "", vmask, "")

    stripes = _expr(unreal.MaterialExpressionScalarParameter, -900, 160)
    stripes.set_editor_property("parameter_name", "StripeCount")
    stripes.set_editor_property("default_value", DEFAULT_STRIPES)
    vmul = _expr(unreal.MaterialExpressionMultiply, -500, 40)
    mel.connect_material_expressions(vmask, "", vmul, "A")
    mel.connect_material_expressions(stripes, "", vmul, "B")

    # band = Floor(Frac(V*stripes) + 0.5) -> 0 then 1 within each period (a square wave, 0/1).
    frac = _expr(unreal.MaterialExpressionFrac, -340, 40)
    mel.connect_material_expressions(vmul, "", frac, "")
    half = _expr(unreal.MaterialExpressionConstant, -340, 180)
    half.set_editor_property("r", 0.5)
    plus = _expr(unreal.MaterialExpressionAdd, -180, 40)
    mel.connect_material_expressions(frac, "", plus, "A")
    mel.connect_material_expressions(half, "", plus, "B")
    band = _expr(unreal.MaterialExpressionFloor, -20, 40)
    mel.connect_material_expressions(plus, "", band, "")

    white = _expr(unreal.MaterialExpressionVectorParameter, -180, 240)
    white.set_editor_property("parameter_name", "PoleWhite")
    white.set_editor_property("default_value", unreal.LinearColor(0.95, 0.95, 0.95, 1.0))
    black = _expr(unreal.MaterialExpressionVectorParameter, -180, 400)
    black.set_editor_property("parameter_name", "PoleBlack")
    black.set_editor_property("default_value", unreal.LinearColor(0.03, 0.03, 0.03, 1.0))
    lerp = _expr(unreal.MaterialExpressionLinearInterpolate, 180, 120)
    mel.connect_material_expressions(white, "", lerp, "A")
    mel.connect_material_expressions(black, "", lerp, "B")
    mel.connect_material_expressions(band, "", lerp, "Alpha")
    mel.connect_material_property(lerp, "", unreal.MaterialProperty.MP_BASE_COLOR)

    rough = _expr(unreal.MaterialExpressionScalarParameter, 180, 320)
    rough.set_editor_property("parameter_name", "PoleRoughness")
    rough.set_editor_property("default_value", 0.4)
    mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)

    mel.recompile_material(mat)
    eal.save_asset(MAT_PATH)
    _log("authored %s (white/black banded flagstick, %d stripe periods)"
         % (MAT_PATH, int(DEFAULT_STRIPES)))


main()
