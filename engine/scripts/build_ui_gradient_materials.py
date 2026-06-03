"""Author /Game/UI/Materials/M_UIGradient{Linear,Radial}.uasset for the GolfForge UI (GOL-150).

Slate/UMG has no native linear/radial gradient brush (UBorder is solid/9-slice). These two MD_UI
translucent materials are used as UMG Image brushes via GolfUITheme::MakeLinearGradient /
MakeRadialGradient (each widget makes a MID and sets the colour params):

  - M_UIGradientLinear  : vertical. lerp(ColorA, ColorB, UV.V) -> emissive; same lerp on alpha -> opacity.
                          ColorA = top (V=0), ColorB = bottom (V=1). RGBA params.
  - M_UIGradientRadial  : lerp(ColorA, ColorB, min(Distance(UV, (CenterX,CenterY)) / Radius, 1)).
                          ColorA = inner, ColorB = outer. Center + Radius are params.

Idempotent + safe to re-run: no .uasset references these (widgets LoadObject them at runtime), so the
nuke+recreate is fine. Run once in the editor (Output Log -> Python):
    py engine/scripts/build_ui_gradient_materials.py
then Save All + git add the .uasset under engine/Golfsim/Content/UI/Materials/.
"""
import unreal

DIR = "/Game/UI/Materials"
mel = unreal.MaterialEditingLibrary
MP = unreal.MaterialProperty


def _new_material(name):
    full = f"{DIR}/{name}"
    if unreal.EditorAssetLibrary.does_asset_exist(full):
        unreal.EditorAssetLibrary.delete_asset(full)
    mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        name, DIR, unreal.Material, unreal.MaterialFactoryNew())
    if mat is None:
        raise RuntimeError(f"create_asset failed: {full}")
    # Domain first (UI), then translucent so the alpha lerp drives opacity.
    mat.set_editor_property("material_domain", unreal.MaterialDomain.MD_UI)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    return mat, full


def _vec(mat, pname, default, x, y):
    p = mel.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, x, y)
    p.set_editor_property("parameter_name", pname)
    p.set_editor_property("default_value", default)
    return p


def _scalar(mat, pname, default, x, y):
    p = mel.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, x, y)
    p.set_editor_property("parameter_name", pname)
    p.set_editor_property("default_value", default)
    return p


def _connect(a, ap, b, bp):
    if not mel.connect_material_expressions(a, ap, b, bp):
        unreal.log_warning(f"connect failed: ({ap or '<>'}) -> ({bp or '<>'})")


def build_linear():
    mat, full = _new_material("M_UIGradientLinear")
    color_a = _vec(mat, "ColorA", unreal.LinearColor(0, 0, 0, 0), -900, -250)   # top  (V=0)
    color_b = _vec(mat, "ColorB", unreal.LinearColor(0, 0, 0, 1), -900, 200)    # bottom (V=1)
    uv = mel.create_material_expression(mat, unreal.MaterialExpressionTextureCoordinate, -900, 0)
    vmask = mel.create_material_expression(mat, unreal.MaterialExpressionComponentMask, -700, 0)
    vmask.set_editor_property("r", False)
    vmask.set_editor_property("g", True)
    vmask.set_editor_property("b", False)
    vmask.set_editor_property("a", False)
    _connect(uv, "", vmask, "")

    lerp_rgb = mel.create_material_expression(mat, unreal.MaterialExpressionLinearInterpolate, -450, -100)
    _connect(color_a, "RGB", lerp_rgb, "A")
    _connect(color_b, "RGB", lerp_rgb, "B")
    _connect(vmask, "", lerp_rgb, "Alpha")
    mel.connect_material_property(lerp_rgb, "", MP.MP_EMISSIVE_COLOR)

    lerp_a = mel.create_material_expression(mat, unreal.MaterialExpressionLinearInterpolate, -450, 150)
    _connect(color_a, "A", lerp_a, "A")
    _connect(color_b, "A", lerp_a, "B")
    _connect(vmask, "", lerp_a, "Alpha")

    # Rounded-box mask so the wash respects the card's rounded corners (a rectangular gradient over a
    # rounded card otherwise shows square corners). SizeX/SizeY (px) are set per-tile from the widget
    # geometry at runtime; Radius defaults to 0 = no rounding for generic callers. SDF in a Custom node.
    size_x = _scalar(mat, "SizeX", 1000.0, -900, 380)
    size_y = _scalar(mat, "SizeY", 1000.0, -900, 470)
    radius = _scalar(mat, "Radius", 0.0, -900, 560)
    size = mel.create_material_expression(mat, unreal.MaterialExpressionAppendVector, -700, 420)
    _connect(size_x, "", size, "A")
    _connect(size_y, "", size, "B")
    mask = mel.create_material_expression(mat, unreal.MaterialExpressionCustom, -450, 420)
    mask.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    ci_uv = unreal.CustomInput();     ci_uv.set_editor_property("input_name", "UV")
    ci_size = unreal.CustomInput();   ci_size.set_editor_property("input_name", "Size")
    ci_radius = unreal.CustomInput(); ci_radius.set_editor_property("input_name", "Radius")
    mask.set_editor_property("inputs", [ci_uv, ci_size, ci_radius])
    mask.set_editor_property("code",
        "float2 p = abs(UV*Size - Size*0.5) - (Size*0.5 - Radius);\n"
        "float d = length(max(p, 0.0)) + min(max(p.x, p.y), 0.0) - Radius;\n"
        "return saturate(0.5 - d);")
    _connect(uv, "", mask, "UV")
    _connect(size, "", mask, "Size")
    _connect(radius, "", mask, "Radius")

    masked_a = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -150, 250)
    _connect(lerp_a, "", masked_a, "A")
    _connect(mask, "", masked_a, "B")
    mel.connect_material_property(masked_a, "", MP.MP_OPACITY)

    mel.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset(full)
    unreal.log(f"build_ui_gradient_materials: built {full}")


def build_radial():
    mat, full = _new_material("M_UIGradientRadial")
    color_a = _vec(mat, "ColorA", unreal.LinearColor(1, 1, 1, 1), -900, -250)   # inner
    color_b = _vec(mat, "ColorB", unreal.LinearColor(0, 0, 0, 0), -900, 200)    # outer
    cx = _scalar(mat, "CenterX", 0.5, -1150, 380)
    cy = _scalar(mat, "CenterY", 0.5, -1150, 470)
    radius = _scalar(mat, "Radius", 0.7, -700, 470)

    uv = mel.create_material_expression(mat, unreal.MaterialExpressionTextureCoordinate, -1150, 120)
    center = mel.create_material_expression(mat, unreal.MaterialExpressionAppendVector, -950, 420)
    _connect(cx, "", center, "A")
    _connect(cy, "", center, "B")
    dist = mel.create_material_expression(mat, unreal.MaterialExpressionDistance, -700, 200)
    _connect(uv, "", dist, "A")
    _connect(center, "", dist, "B")
    div = mel.create_material_expression(mat, unreal.MaterialExpressionDivide, -520, 240)
    _connect(dist, "", div, "A")
    _connect(radius, "", div, "B")
    # clamp the coord to <=1 (distance >= 0 already gives the lower bound) so the lerp doesn't extrapolate.
    coord = mel.create_material_expression(mat, unreal.MaterialExpressionMin, -360, 240)
    coord.set_editor_property("const_b", 1.0)
    _connect(div, "", coord, "A")

    lerp_rgb = mel.create_material_expression(mat, unreal.MaterialExpressionLinearInterpolate, -150, -100)
    _connect(color_a, "RGB", lerp_rgb, "A")
    _connect(color_b, "RGB", lerp_rgb, "B")
    _connect(coord, "", lerp_rgb, "Alpha")
    mel.connect_material_property(lerp_rgb, "", MP.MP_EMISSIVE_COLOR)

    lerp_a = mel.create_material_expression(mat, unreal.MaterialExpressionLinearInterpolate, -150, 150)
    _connect(color_a, "A", lerp_a, "A")
    _connect(color_b, "A", lerp_a, "B")
    _connect(coord, "", lerp_a, "Alpha")
    mel.connect_material_property(lerp_a, "", MP.MP_OPACITY)

    mel.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset(full)
    unreal.log(f"build_ui_gradient_materials: built {full}")


build_linear()
build_radial()
unreal.log("build_ui_gradient_materials: DONE")
