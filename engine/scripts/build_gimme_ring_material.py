"""Author /Game/Materials/M_GimmeRing.uasset for AGolfPinActor's gimme ring (GOL-123 polish).

A dead-simple TRANSLUCENT, UNLIT surface material: a semi-transparent accent halo. The ring *shape*
is geometry (AGolfPinActor builds a thin procedural annulus that drapes over the terrain), so the
material only needs to be a flat see-through colour -- no radial maths, no texture, no decal-domain
gotchas. Two params so C++ can tune per spawn without a rebuild:
  - "Color"   (Vector3) -> Emissive Color   (unlit, so emissive == the visible colour)
  - "Opacity" (Scalar)  -> Opacity           (how see-through; ~0.45 reads as a halo, not a slab)

Idempotent: re-running deletes + recreates the material.

Run in the UE5.7 editor Python interpreter via execute_unreal_python (close PIE first):
    exec(compile(open(r"<repo>\\engine\\scripts\\build_gimme_ring_material.py",
        encoding="utf-8").read(), "build_gimme_ring_material.py", "exec"))
"""
import unreal

DIR = "/Game/Materials"
MAT_NAME = "M_GimmeRing"
MAT_PATH = f"{DIR}/{MAT_NAME}"

mel = unreal.MaterialEditingLibrary
eal = unreal.EditorAssetLibrary
at = unreal.AssetToolsHelpers.get_asset_tools()


def _log(m):
    unreal.log("GIMMERING_MAT: " + str(m))


def main():
    _log("=== GIMME RING MATERIAL START ===")
    if eal.does_asset_exist(MAT_PATH):
        eal.delete_asset(MAT_PATH)
    mat = at.create_asset(MAT_NAME, DIR, unreal.Material, unreal.MaterialFactoryNew())
    if mat is None:
        raise RuntimeError("failed to create " + MAT_PATH)

    # Translucent + unlit: a flat coloured halo you can see the green through. Two-sided so the thin
    # band still draws if the camera dips to grazing angles.
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("two_sided", True)

    def _expr(cls, x, y):
        return mel.create_material_expression(mat, cls, x, y)

    color = _expr(unreal.MaterialExpressionVectorParameter, -360, -60)
    color.set_editor_property("parameter_name", "Color")
    color.set_editor_property("default_value", unreal.LinearColor(0.98, 0.95, 0.40, 1.0))  # warm halo
    mel.connect_material_property(color, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    opacity = _expr(unreal.MaterialExpressionScalarParameter, -360, 140)
    opacity.set_editor_property("parameter_name", "Opacity")
    opacity.set_editor_property("default_value", 0.45)
    mel.connect_material_property(opacity, "", unreal.MaterialProperty.MP_OPACITY)

    mel.recompile_material(mat)
    eal.save_asset(MAT_PATH)
    _log("authored %s (translucent unlit; Color + Opacity params)" % MAT_PATH)
    _log("DONE")


main()
