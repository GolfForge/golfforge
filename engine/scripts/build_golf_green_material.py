"""Author /Game/Materials/M_GolfGreen.uasset for AGolfPinActor (GOL-29; textured pass).

A Masked round-disc material for the practice-green decal, the collar ring, the hole cup, and the
gimme ring -- all the same material, recolored per-instance via the "Color" MID param in C++.

Graph:
  - Color (Vector3 param) * turf-grain detail -> BaseColor (so it's not a flat shaded disc)
  - Roughness (scalar param, default 0.85 -- grass is matte) -> Roughness
  - Normal: a tiling procedural TURF-GRAIN normal (T_TurfGrain), tiled by GrainTiling -> Normal
    (this is the "more texture" -- relief under lighting instead of a flat color circle)
  - Circular alpha: (1 - Distance(UV, (0.5,0.5))) -> OpacityMask, ClipValue 0.5 -> a clean round
    disc inside the plane's unit-square UV (no side wall, no z-fight; the actor lifts the plane).

The turf-grain normal is generated off-engine with stdlib (seamless tiling multi-octave cos-grid
height -> wrapped central-difference normal -> RGB PNG), the same technique as T_GolfBallDimples /
T_WaterNormal. No Fab dependency -- the green/collar/cup are gameplay actors that must look right on
a fresh clone, so the texture is procedural + committed, not a per-machine Megascans download.

Idempotent: re-running deletes + recreates the material (and regenerates the texture unless present).

Run in the UE5.7 editor Python interpreter via execute_unreal_python (close PIE first):
    exec(compile(open(r"<repo>\\engine\\scripts\\build_golf_green_material.py",
        encoding="utf-8").read(), "build_golf_green_material.py", "exec"))

bridge note: execute_unreal_python returns output:None. Feedback via unreal.log() under LogPython.
"""
import math
import os
import struct
import zlib

import unreal

DIR = "/Game/Materials"
TEX_NAME = "T_TurfGrain"
MAT_NAME = "M_GolfGreen"
TEX_PATH = f"{DIR}/{TEX_NAME}"
MAT_PATH = f"{DIR}/{MAT_NAME}"

NORMAL_PNG_SIZE = 256
REIMPORT_NORMAL = globals().get("REIMPORT_NORMAL", False)

mel = unreal.MaterialEditingLibrary
eal = unreal.EditorAssetLibrary
at = unreal.AssetToolsHelpers.get_asset_tools()


def _log(m):
    unreal.log("GOLFGREEN_MAT: " + str(m))


# ---------------------------------------------------------------- procedural turf-grain normal (stdlib)
def _write_rgb_png(path, w, h, rgb):
    """8-bit RGB (color type 2), no interlace. rgb: row-major bytes, len w*h*3."""
    def _chunk(typ, data):
        body = typ + data
        return (struct.pack(">I", len(data)) + body
                + struct.pack(">I", zlib.crc32(body) & 0xffffffff))

    ihdr = struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)
    raw = bytearray()
    for y in range(h):
        raw.append(0)                       # per-scanline filter type 0 (None)
        raw.extend(rgb[y * w * 3:(y + 1) * w * 3])
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(_chunk(b"IHDR", ihdr))
        f.write(_chunk(b"IDAT", zlib.compress(bytes(raw), 9)))
        f.write(_chunk(b"IEND", b""))


# Octaves: (freq_x, freq_y, amplitude, phase_x, phase_y). Integer freqs => the height (and so the
# normal) tiles seamlessly; the spread of frequencies + phase offsets breaks the regular-grid read
# into an irregular turf grain. All-high frequencies => fine even cut grass; the earlier low-freq
# octaves (3..8) cut big diagonal gouges ("attacked with a knife") so they're gone.
_OCTAVES = [
    (11, 13, 1.00, 0.00, 0.00),
    (17, 19, 0.75, 0.13, 0.41),
    (23, 29, 0.55, 0.27, 0.07),
    (31, 37, 0.40, 0.51, 0.33),
    (43, 47, 0.28, 0.09, 0.61),
    (59, 53, 0.18, 0.71, 0.19),
    (71, 67, 0.11, 0.37, 0.83),
]


def _gen_turf_normal_png(path, n=NORMAL_PNG_SIZE):
    """Seamless tiling turf-grain micro-normal. Height = a sum of integer-frequency cos grids
    (so it wraps); normal via wrapped central differences, encoded tangent-space RGB."""
    tau = 2.0 * math.pi
    height = [0.0] * (n * n)
    for y in range(n):
        fy = y / float(n)
        for x in range(n):
            fx = x / float(n)
            h = 0.0
            for (fxi, fyi, a, px, py) in _OCTAVES:
                h += a * math.cos(tau * (fxi * fx + px)) * math.cos(tau * (fyi * fy + py))
            height[y * n + x] = h

    strength = 0.8     # gradient gain -> grain relief depth (soft; high = harsh "knife" gouges)
    rgb = bytearray(n * n * 3)
    for y in range(n):
        for x in range(n):
            hl = height[y * n + (x - 1) % n]
            hr = height[y * n + (x + 1) % n]
            hd = height[((y - 1) % n) * n + x]
            hu = height[((y + 1) % n) * n + x]
            nx = -(hr - hl) * strength
            ny = -(hu - hd) * strength
            nz = 1.0
            inv = 1.0 / math.sqrt(nx * nx + ny * ny + nz * nz)
            i = (y * n + x) * 3
            rgb[i]     = int(max(0.0, min(1.0, nx * inv * 0.5 + 0.5)) * 255.0)
            rgb[i + 1] = int(max(0.0, min(1.0, ny * inv * 0.5 + 0.5)) * 255.0)
            rgb[i + 2] = int(max(0.0, min(1.0, nz * inv * 0.5 + 0.5)) * 255.0)
    _write_rgb_png(path, n, n, rgb)


def _ensure_turf_normal():
    """Find-or-import T_TurfGrain (TC_NORMALMAP, non-sRGB). Source PNG -> gitignored Intermediate."""
    if eal.does_asset_exist(TEX_PATH) and not REIMPORT_NORMAL:
        return eal.load_asset(TEX_PATH)
    # Reimport: an in-place replace fails while M_GolfGreen still references the old texture, so drop
    # the material (it's recreated below) and the texture first, then import a clean asset.
    if REIMPORT_NORMAL and eal.does_asset_exist(TEX_PATH):
        if eal.does_asset_exist(MAT_PATH):
            eal.delete_asset(MAT_PATH)
        eal.delete_asset(TEX_PATH)
        _log("REIMPORT: dropped old %s (+ %s) before regenerating" % (TEX_PATH, MAT_PATH))
    proj = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
    src_dir = os.path.join(proj, "Intermediate", "TurfGrainSource")
    if not os.path.isdir(src_dir):
        os.makedirs(src_dir)
    png = os.path.join(src_dir, TEX_NAME + ".png")
    _gen_turf_normal_png(png)
    _log("generated turf-grain normal %s (%dx%d)" % (png, NORMAL_PNG_SIZE, NORMAL_PNG_SIZE))

    task = unreal.AssetImportTask()
    task.set_editor_property("filename", png)
    task.set_editor_property("destination_path", DIR)
    task.set_editor_property("destination_name", TEX_NAME)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", True)
    at.import_asset_tasks([task])
    tex = eal.load_asset(TEX_PATH)
    if tex is None:
        raise RuntimeError("failed to import " + TEX_PATH)
    tex.set_editor_property("compression_settings",
                            unreal.TextureCompressionSettings.TC_NORMALMAP)
    tex.set_editor_property("srgb", False)
    try:
        tex.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_WORLD_NORMAL_MAP)
    except Exception:
        pass
    eal.save_asset(TEX_PATH)
    _log("imported %s (TC_NORMALMAP, sRGB off)" % TEX_PATH)
    return tex


# ---------------------------------------------------------------- material graph
def _build_material(turf_tex):
    if eal.does_asset_exist(MAT_PATH):
        eal.delete_asset(MAT_PATH)
    mat = at.create_asset(MAT_NAME, DIR, unreal.Material, unreal.MaterialFactoryNew())
    if mat is None:
        raise RuntimeError("failed to create " + MAT_PATH)

    # Masked: pixels with OpacityMask >= clip render, the rest are clipped. Two-sided so the disc
    # still draws if the camera dips slightly below the green plane.
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    mat.set_editor_property("opacity_mask_clip_value", 0.5)
    mat.set_editor_property("two_sided", True)

    def _expr(cls, x, y):
        return mel.create_material_expression(mat, cls, x, y)

    # BaseColor: "Color" Vector3 param (per-instance MID override) * turf-grain detail so the disc
    # isn't a flat shaded color. The detail = 1 + (grain.B - bias) * variation: grain.B (the normal's
    # Z) dips in the crevices, so this darkens them subtly without shifting hue.
    # Default Color = the GREEN's color (AGolfPinActor assigns this material directly to the green disc,
    # so this default IS the green -- tune it here, no C++ rebuild). The collar/cup MIDs override it.
    color = _expr(unreal.MaterialExpressionVectorParameter, -560, -200)
    color.set_editor_property("parameter_name", "Color")
    color.set_editor_property("default_value", unreal.LinearColor(0.045, 0.28, 0.10, 1.0))

    # Roughness param -- grass is matte; collar/cup MIDs can nudge it duller.
    roughness = _expr(unreal.MaterialExpressionScalarParameter, -560, 40)
    roughness.set_editor_property("parameter_name", "Roughness")
    roughness.set_editor_property("default_value", 0.85)
    mel.connect_material_property(roughness, "", unreal.MaterialProperty.MP_ROUGHNESS)

    # Grain tiling: TexCoord * GrainTiling -> the turf normal's UVs (and the albedo detail sample).
    uv = _expr(unreal.MaterialExpressionTextureCoordinate, -1100, 320)
    tiling = _expr(unreal.MaterialExpressionScalarParameter, -1100, 460)
    tiling.set_editor_property("parameter_name", "GrainTiling")
    tiling.set_editor_property("default_value", 28.0)
    uv_mul = _expr(unreal.MaterialExpressionMultiply, -900, 360)
    mel.connect_material_expressions(uv, "", uv_mul, "A")
    mel.connect_material_expressions(tiling, "", uv_mul, "B")

    ts = _expr(unreal.MaterialExpressionTextureSample, -700, 340)
    ts.set_editor_property("texture", turf_tex)
    try:
        ts.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
    except Exception as exc:
        _log("sampler_type note: %s" % str(exc)[:50])
    mel.connect_material_expressions(uv_mul, "", ts, "UVs")
    mel.connect_material_property(ts, "RGB", unreal.MaterialProperty.MP_NORMAL)

    # Albedo detail: darken crevices. detail = 1 + (grain.B - 1) * GrainShade, where grain.B ~ 1 on
    # flats and < 1 in the grain -> a subtle multiplier <= 1. Keeps the Color hue, adds depth.
    shade = _expr(unreal.MaterialExpressionScalarParameter, -700, 560)
    shade.set_editor_property("parameter_name", "GrainShade")
    shade.set_editor_property("default_value", 0.3)
    b_minus1 = _expr(unreal.MaterialExpressionAdd, -480, 460)
    negone = _expr(unreal.MaterialExpressionConstant, -640, 500)
    negone.set_editor_property("r", -1.0)
    mel.connect_material_expressions(ts, "B", b_minus1, "A")
    mel.connect_material_expressions(negone, "", b_minus1, "B")
    shaded = _expr(unreal.MaterialExpressionMultiply, -320, 460)
    mel.connect_material_expressions(b_minus1, "", shaded, "A")
    mel.connect_material_expressions(shade, "", shaded, "B")
    detail = _expr(unreal.MaterialExpressionAdd, -160, 360)
    one = _expr(unreal.MaterialExpressionConstant, -320, 300)
    one.set_editor_property("r", 1.0)
    mel.connect_material_expressions(one, "", detail, "A")
    mel.connect_material_expressions(shaded, "", detail, "B")
    base = _expr(unreal.MaterialExpressionMultiply, 40, 0)
    mel.connect_material_expressions(color, "", base, "A")
    mel.connect_material_expressions(detail, "", base, "B")
    mel.connect_material_property(base, "", unreal.MaterialProperty.MP_BASE_COLOR)

    # Circular alpha (unchanged from GOL-29): 1 - dist(UV, center); clip 0.5 -> inscribed unit disc.
    auv = _expr(unreal.MaterialExpressionTextureCoordinate, -800, 760)
    center = _expr(unreal.MaterialExpressionConstant2Vector, -800, 900)
    center.set_editor_property("r", 0.5)
    center.set_editor_property("g", 0.5)
    distance = _expr(unreal.MaterialExpressionDistance, -520, 800)
    mel.connect_material_expressions(auv, "", distance, "A")
    mel.connect_material_expressions(center, "", distance, "B")
    one_minus = _expr(unreal.MaterialExpressionOneMinus, -280, 800)
    mel.connect_material_expressions(distance, "", one_minus, "")
    mel.connect_material_property(one_minus, "", unreal.MaterialProperty.MP_OPACITY_MASK)

    mel.recompile_material(mat)
    eal.save_asset(MAT_PATH)
    _log("authored %s (round masked disc + turf-grain normal + Color/Roughness/GrainTiling params)"
         % MAT_PATH)
    return mat


def main():
    _log("=== GOLF GREEN MATERIAL START ===")
    tex = _ensure_turf_normal()
    _build_material(tex)
    _log("DONE: %s + %s" % (TEX_PATH, MAT_PATH))


main()
