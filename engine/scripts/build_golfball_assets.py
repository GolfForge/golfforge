"""Author T_GolfBallDimples + M_GolfBall for the in-flight golf ball (ball+sound task).

The ball was a gray /Engine/BasicShapes/Sphere with no material. This gives it a proper golf-ball look:
a glossy off-white material with a tiling dimple micro-normal, assigned to the sphere in
AGolfBallActor (C++). No Fab dependency -- the dimple normal is generated off-engine with stdlib (a
seamless tiling cosine-grid height -> wrapped central-difference normal -> RGB PNG), exactly the
T_WaterNormal technique in build_water_material.py. (Real spherical-cap dimples are sub-pixel at the
ball's on-screen size; a regular bumpy micro-normal reads identically and is cheap to generate.)

All knobs (BallColor / Roughness / Specular / DimpleTiling) are params on M_GolfBall for live tuning.

Run in the UE5.7 editor Python interpreter via execute_unreal_python (close PIE first -- material
create/delete is blocked during play):
    exec(compile(open(r"<repo>\\engine\\scripts\\build_golfball_assets.py",
        encoding="utf-8").read(), "build_golfball_assets.py", "exec"))

bridge note: execute_unreal_python returns output:None. All feedback via unreal.log() under LogPython.
"""
import math
import os
import struct
import zlib

import unreal

DIR = "/Game/Models/Golf"
TEX_NAME = "T_GolfBallDimples"
MAT_NAME = "M_GolfBall"
TEX_PATH = f"{DIR}/{TEX_NAME}"
MAT_PATH = f"{DIR}/{MAT_NAME}"

NORMAL_PNG_SIZE = 256
REIMPORT_NORMAL = globals().get("REIMPORT_NORMAL", False)

mel = unreal.MaterialEditingLibrary
eal = unreal.EditorAssetLibrary
at = unreal.AssetToolsHelpers.get_asset_tools()


def _log(m):
    unreal.log("GOLFBALL_MAT: " + str(m))


# ---------------------------------------------------------------- procedural dimple normal (stdlib)
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


def _gen_dimple_normal_png(path, n=NORMAL_PNG_SIZE):
    """Seamless tiling dimple micro-normal. Height = a regular grid of round dips (a cosine grid plus a
    finer one for break-up); integer freqs => the pattern wraps. Normal via wrapped central differences,
    encoded tangent-space RGB. Cheap (O(1)/pixel) and tiles cleanly when sampled repeatedly on the ball."""
    K = 18          # primary dimples per tile axis
    K2 = 36         # finer secondary grid for subtle break-up
    tau = 2.0 * math.pi
    height = [0.0] * (n * n)
    for y in range(n):
        fy = y / float(n)
        for x in range(n):
            fx = x / float(n)
            # cos*cos grids: a lattice of bumps/dips. Sign is irrelevant to the look; depth tuned subtle.
            h = math.cos(tau * K * fx) * math.cos(tau * K * fy)
            h += 0.35 * math.cos(tau * K2 * fx) * math.cos(tau * K2 * fy)
            height[y * n + x] = h

    strength = 1.1     # gradient gain -> dimple shading depth (subtle)
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


def _ensure_dimple_normal():
    """Find-or-import T_GolfBallDimples (TC_NORMALMAP, non-sRGB). Source PNG -> gitignored Intermediate."""
    if eal.does_asset_exist(TEX_PATH) and not REIMPORT_NORMAL:
        return eal.load_asset(TEX_PATH)
    proj = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
    src_dir = os.path.join(proj, "Intermediate", "GolfBallNormalSource")
    if not os.path.isdir(src_dir):
        os.makedirs(src_dir)
    png = os.path.join(src_dir, TEX_NAME + ".png")
    _gen_dimple_normal_png(png)
    _log("generated dimple normal %s (%dx%d)" % (png, NORMAL_PNG_SIZE, NORMAL_PNG_SIZE))

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
def _build_material(dimple_tex):
    if eal.does_asset_exist(MAT_PATH):
        eal.delete_asset(MAT_PATH)
    mat = at.create_asset(MAT_NAME, DIR, unreal.Material, unreal.MaterialFactoryNew())
    if mat is None:
        raise RuntimeError("failed to create " + MAT_PATH)

    def _expr(cls, x, y):
        return mel.create_material_expression(mat, cls, x, y)

    # BaseColor: off-white tint param.
    ball_color = _expr(unreal.MaterialExpressionVectorParameter, -540, -300)
    ball_color.set_editor_property("parameter_name", "BallColor")
    ball_color.set_editor_property("default_value", unreal.LinearColor(0.85, 0.85, 0.85, 1.0))
    mel.connect_material_property(ball_color, "", unreal.MaterialProperty.MP_BASE_COLOR)

    # Roughness + Specular params (semi-gloss).
    rough = _expr(unreal.MaterialExpressionScalarParameter, -540, -120)
    rough.set_editor_property("parameter_name", "Roughness")
    rough.set_editor_property("default_value", 0.34)
    mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    spec = _expr(unreal.MaterialExpressionScalarParameter, -540, -20)
    spec.set_editor_property("parameter_name", "Specular")
    spec.set_editor_property("default_value", 0.5)
    mel.connect_material_property(spec, "", unreal.MaterialProperty.MP_SPECULAR)

    # Normal: dimple texture tiled DimpleTiling times.
    uv = _expr(unreal.MaterialExpressionTextureCoordinate, -900, 240)
    tiling = _expr(unreal.MaterialExpressionScalarParameter, -900, 360)
    tiling.set_editor_property("parameter_name", "DimpleTiling")
    tiling.set_editor_property("default_value", 9.0)
    uv_mul = _expr(unreal.MaterialExpressionMultiply, -700, 260)
    mel.connect_material_expressions(uv, "", uv_mul, "A")
    mel.connect_material_expressions(tiling, "", uv_mul, "B")
    ts = _expr(unreal.MaterialExpressionTextureSample, -500, 240)
    ts.set_editor_property("texture", dimple_tex)
    try:
        ts.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
    except Exception as exc:
        _log("sampler_type note: %s" % str(exc)[:50])
    mel.connect_material_expressions(uv_mul, "", ts, "UVs")
    mel.connect_material_property(ts, "RGB", unreal.MaterialProperty.MP_NORMAL)

    mel.recompile_material(mat)
    eal.save_asset(MAT_PATH)
    _log("authored %s (off-white semi-gloss + tiled dimple normal)" % MAT_PATH)
    return mat


def main():
    _log("=== GOLF BALL ASSETS START ===")
    tex = _ensure_dimple_normal()
    _build_material(tex)
    _log("DONE: %s + %s" % (TEX_PATH, MAT_PATH))


main()
