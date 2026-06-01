"""Author /Game/Materials/M_GolfGreen.uasset for AGolfPinActor (GOL-29).

Builds a Masked Material with:
  - Color (Vector3 param, default forest-green) -> BaseColor
  - Constant Roughness 0.85 (grass is matte) -> Roughness
  - Circular alpha: (1 - Distance(UV, (0.5, 0.5))) -> OpacityMask, ClipValue 0.5
This gives the static-mesh PLANE a clean round target green at any distance, no
side wall, no z-fight (the actor lifts the plane a couple cm).

Idempotent: re-running deletes + recreates the asset.

How to run (one-time bootstrap; the .uasset gets committed to LFS afterward):
    1. Open Golfsim.uproject in the UE5.7 editor.
    2. Window > Output Log > switch the bottom-row mode to "Python".
    3. py engine/scripts/build_golf_green_material.py
    4. Save the new asset (Ctrl+S or File > Save All).
    5. git add + commit the new .uasset under engine/Golfsim/Content/Materials/.

Until this runs the C++ falls back to a tinted BasicShapeMaterial -- the disc shows
as a green square. After this lands the disc is a true round green.
"""
import unreal

PATH_DIR = "/Game/Materials"
PATH_NAME = "M_GolfGreen"
FULL_PATH = f"{PATH_DIR}/{PATH_NAME}"

if unreal.EditorAssetLibrary.does_asset_exist(FULL_PATH):
    unreal.EditorAssetLibrary.delete_asset(FULL_PATH)

factory = unreal.MaterialFactoryNew()
mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
    PATH_NAME, PATH_DIR, unreal.Material, factory
)
if mat is None:
    raise RuntimeError(f"Failed to create {FULL_PATH}")

mel = unreal.MaterialEditingLibrary

# Masked: pixels with OpacityMask >= OpacityMaskClipValue render; the rest are clipped. Two-sided
# so the disc renders if the camera dips slightly below the green's plane.
mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
mat.set_editor_property("opacity_mask_clip_value", 0.5)
mat.set_editor_property("two_sided", True)

# BaseColor: a "Color" Vector3 parameter so the actor's MID can override per-instance later.
color = mel.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -400, -200)
color.set_editor_property("parameter_name", "Color")
color.set_editor_property("default_value", unreal.LinearColor(0.10, 0.55, 0.18, 1.0))
mel.connect_material_property(color, "RGB", unreal.MaterialProperty.MP_BASE_COLOR)

# Roughness: a flat scalar -- grass is matte, no specular highlight bouncing at distance.
roughness = mel.create_material_expression(mat, unreal.MaterialExpressionConstant, -400, 0)
roughness.set_editor_property("r", 0.85)
mel.connect_material_property(roughness, "", unreal.MaterialProperty.MP_ROUGHNESS)

# Circular alpha. Distance(UV, (0.5, 0.5)) is 0 at the center and rises to ~0.707 at the corner.
# OneMinus(Distance) is 1 at center, 0.5 at radius 0.5 (the inscribed circle of the unit square).
# With ClipValue 0.5, only pixels with (1 - dist) >= 0.5 -> dist <= 0.5 survive -> a unit-radius
# disc inside the UV square. The plane's UV is the unit square, so the disc fits cleanly.
uv = mel.create_material_expression(mat, unreal.MaterialExpressionTextureCoordinate, -800, 200)
center = mel.create_material_expression(mat, unreal.MaterialExpressionConstant2Vector, -800, 350)
center.set_editor_property("r", 0.5)
center.set_editor_property("g", 0.5)
distance = mel.create_material_expression(mat, unreal.MaterialExpressionDistance, -500, 250)
mel.connect_material_expressions(uv, "", distance, "A")
mel.connect_material_expressions(center, "", distance, "B")
one_minus = mel.create_material_expression(mat, unreal.MaterialExpressionOneMinus, -250, 250)
mel.connect_material_expressions(distance, "", one_minus, "")
mel.connect_material_property(one_minus, "", unreal.MaterialProperty.MP_OPACITY_MASK)

mel.recompile_material(mat)
unreal.EditorAssetLibrary.save_asset(FULL_PATH)
unreal.log(f"build_golf_green_material: created {FULL_PATH}")
