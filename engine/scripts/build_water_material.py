"""Author T_WaterNormal + M_GolfsimWater + MIC_GolfsimWater for the course water bodies (GOL-164).

Upgrades the bare translucent water (flat blue tint) into a real Long-Island-pond surface:
  - Reflections: low Roughness + a Fresnel curve. Lumen Reflections are already ON (r.ReflectionMethod=1,
    movable lights from GOL-161), so the water reflects sky + trees for free; this just tunes it.
  - Ripple: PER-PIXEL ANIMATED NORMALS (not WPO). The water mesh is a flat low-poly triangulated
    polygon -- WPO has no verts to move and would fight the ball-water collision. Two layers of a
    seamless tiling T_WaterNormal, each panned along the shared MPC_GolfWind direction at different
    scale/speed, blended + lerped toward flat by RippleStrength. Zero geometry change => collision honest.
  - Shore foam: a DepthFade mask lightens (toward ShoreTint) + thickens (more opaque) the water near
    shorelines, reading as shallows.
  - Tint: WaterTint, a deep pond green-blue (parkland, not Caribbean turquoise).

T_WaterNormal is generated procedurally off-engine with stdlib (sum-of-sine-waves height ->
finite-difference normal -> RGB PNG, integer wave periods => seamless tiling). No Fab dependency.

All knobs are Scalar/Vector params on M_GolfsimWater, driving MIC_GolfsimWater -- tune the look live in
the Details panel (no script re-run), the GOL-163 pattern. The MIC is re-assigned to the live Water_*
actors so the upgrade shows immediately; build_water_actors.py still spawns them, this just re-skins.

Caustics (Light-Function on the DirectionalLight) are intentionally deferred -- see GOL-164 follow-up.

Idempotent: re-running deletes + recreates the material + MIC and re-assigns the actors. T_WaterNormal
is regenerated only if missing (set REIMPORT_NORMAL=True to force).

Run in the UE5.7 editor Python interpreter via execute_unreal_python, with GolfForgeDemoBlack.umap open
(close PIE first -- material create/delete is blocked during play):
    exec(compile(open(r"<repo>\\engine\\scripts\\build_water_material.py",
        encoding="utf-8").read(), "build_water_material.py", "exec"))

bridge note: execute_unreal_python returns output:None. All feedback via unreal.log() under LogPython.
"""
import math
import os
import struct
import zlib

import unreal

DIR = "/Game/Materials"
TEX_NAME = "T_WaterNormal"
MAT_NAME = "M_GolfsimWater"
MIC_NAME = "MIC_GolfsimWater"
MPC_NAME = "MPC_GolfWind"
TEX_PATH = f"{DIR}/{TEX_NAME}"
MAT_PATH = f"{DIR}/{MAT_NAME}"
MIC_PATH = f"{DIR}/{MIC_NAME}"
MPC_PATH = f"{DIR}/{MPC_NAME}"

WATER_LABEL_PREFIX = "Water_"   # build_water_actors.py spawns Water_<osm_way_id>
NORMAL_PNG_SIZE = 256

REIMPORT_NORMAL = globals().get("REIMPORT_NORMAL", False)

mel = unreal.MaterialEditingLibrary
eal = unreal.EditorAssetLibrary
at = unreal.AssetToolsHelpers.get_asset_tools()


def _log(m):
    unreal.log("WATER_MAT: " + str(m))


# ---------------------------------------------------------------- procedural normal PNG (stdlib)
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


def _gen_water_normal_png(path, n=NORMAL_PNG_SIZE):
    """Seamless tiling ripple normal. Height = sum of a few integer-period sine waves (so the
    pattern wraps), normal via wrapped central differences, encoded tangent-space RGB."""
    # (freq_x, freq_y, amplitude, phase) -- small integer freqs tile seamlessly across n.
    waves = [(1, 0, 1.00, 0.0), (0, 1, 0.85, 1.3), (2, 1, 0.55, 0.5),
             (1, 2, 0.45, 2.1), (3, 2, 0.30, 0.7), (2, 3, 0.25, 1.7)]
    height = [0.0] * (n * n)
    tau = 2.0 * math.pi
    for y in range(n):
        for x in range(n):
            h = 0.0
            for (fx, fy, a, ph) in waves:
                h += a * math.sin(tau * ((fx * x + fy * y) / float(n)) + ph)
            height[y * n + x] = h

    strength = 1.6     # gradient gain -> bump steepness (subtle ripple)
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


def _ensure_water_normal():
    """Find-or-import T_WaterNormal (TC_NORMALMAP, non-sRGB). Source PNG is generated into the
    gitignored Intermediate dir; only the .uasset is committed."""
    if eal.does_asset_exist(TEX_PATH) and not REIMPORT_NORMAL:
        return eal.load_asset(TEX_PATH)
    proj = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
    src_dir = os.path.join(proj, "Intermediate", "WaterNormalSource")
    if not os.path.isdir(src_dir):
        os.makedirs(src_dir)
    png = os.path.join(src_dir, TEX_NAME + ".png")
    _gen_water_normal_png(png)
    _log("generated procedural normal %s (%dx%d)" % (png, NORMAL_PNG_SIZE, NORMAL_PNG_SIZE))

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


# ---------------------------------------------------------------- material graph helpers
def _build_material(water_tex, mpc):
    if eal.does_asset_exist(MAT_PATH):
        eal.delete_asset(MAT_PATH)
    mat = at.create_asset(MAT_NAME, DIR, unreal.Material, unreal.MaterialFactoryNew())
    if mat is None:
        raise RuntimeError("failed to create " + MAT_PATH)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property("two_sided", True)
    # Surface translucency that still takes lighting/reflections (Lumen) rather than additive glow.
    try:
        mat.set_editor_property("translucency_lighting_mode",
                                unreal.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING)
    except Exception as exc:
        _log("translucency_lighting_mode note: %s" % str(exc)[:60])

    def _expr(cls, x, y):
        return mel.create_material_expression(mat, cls, x, y)

    def _scalar(name, default, x, y):
        p = _expr(unreal.MaterialExpressionScalarParameter, x, y)
        p.set_editor_property("parameter_name", name)
        p.set_editor_property("default_value", float(default))
        return p

    def _vec(name, color, x, y):
        p = _expr(unreal.MaterialExpressionVectorParameter, x, y)
        p.set_editor_property("parameter_name", name)
        p.set_editor_property("default_value", unreal.LinearColor(*color))
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

    def _add(a, ap, b, bp, x, y):
        m = _expr(unreal.MaterialExpressionAdd, x, y)
        mel.connect_material_expressions(a, ap, m, "A")
        mel.connect_material_expressions(b, bp, m, "B")
        return m

    # --- shared wind drift (MPC_GolfWind WindDirection RG -> float2) -----------
    wind_dir = _coll("WindDirection", -1700, 600)
    wind2d = _expr(unreal.MaterialExpressionComponentMask, -1520, 600)
    wind2d.set_editor_property("r", True); wind2d.set_editor_property("g", True)
    wind2d.set_editor_property("b", False); wind2d.set_editor_property("a", False)
    mel.connect_material_expressions(wind_dir, "", wind2d, "")
    time = _expr(unreal.MaterialExpressionTime, -1700, 760)

    def _ripple_layer(tiling_name, tiling_def, speed_name, speed_def, y0):
        """One panned normal sample: UV*Tiling + Time*Speed*WindDir2D -> TextureSample(NORMAL)."""
        uv = _expr(unreal.MaterialExpressionTextureCoordinate, -1520, y0)
        tiling = _scalar(tiling_name, tiling_def, -1520, y0 + 110)
        base_uv = _mul(uv, "", tiling, "", -1340, y0)
        speed = _scalar(speed_name, speed_def, -1520, y0 + 220)
        t_speed = _mul(time, "", speed, "", -1340, y0 + 180)
        offset = _mul(wind2d, "", t_speed, "", -1160, y0 + 120)   # float2 * scalar
        uvs = _add(base_uv, "", offset, "", -980, y0 + 40)
        ts = _expr(unreal.MaterialExpressionTextureSample, -800, y0)
        ts.set_editor_property("texture", water_tex)
        try:
            ts.set_editor_property("sampler_type",
                                   unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
        except Exception as exc:
            _log("sampler_type note: %s" % str(exc)[:50])
        mel.connect_material_expressions(uvs, "", ts, "UVs")
        return ts

    nA = _ripple_layer("RippleTilingA", 8.0, "RippleSpeedA", 0.020, 300)
    nB = _ripple_layer("RippleTilingB", 17.0, "RippleSpeedB", 0.013, 700)
    n_sum = _add(nA, "RGB", nB, "RGB", -560, 460)
    n_avg = _mul(n_sum, "", _scalar("RippleHalf", 0.5, -560, 560), "", -380, 460)
    flat = _expr(unreal.MaterialExpressionConstant3Vector, -560, 360)
    flat.set_editor_property("constant", unreal.LinearColor(0.0, 0.0, 1.0, 0.0))
    ripple_strength = _scalar("RippleStrength", 0.6, -380, 600)
    n_lerp = _expr(unreal.MaterialExpressionLinearInterpolate, -200, 440)
    mel.connect_material_expressions(flat, "", n_lerp, "A")
    mel.connect_material_expressions(n_avg, "", n_lerp, "B")
    mel.connect_material_expressions(ripple_strength, "", n_lerp, "Alpha")
    n_norm = _expr(unreal.MaterialExpressionNormalize, -20, 440)
    mel.connect_material_expressions(n_lerp, "", n_norm, "")
    mel.connect_material_property(n_norm, "", unreal.MaterialProperty.MP_NORMAL)

    # --- shore mask: DepthFade ~0 at shoreline (shallow over lakebed) -> 1 deep -
    shore_width = _scalar("ShoreWidth", 250.0, -800, -120)   # cm of shallows band
    depth_fade = _expr(unreal.MaterialExpressionDepthFade, -620, -180)
    mel.connect_material_expressions(shore_width, "", depth_fade, "FadeDistance")
    near_shore = _expr(unreal.MaterialExpressionOneMinus, -440, -180)   # 1 at shore, 0 deep
    mel.connect_material_expressions(depth_fade, "", near_shore, "")

    # --- BaseColor: deep WaterTint -> lighter ShoreTint near edges -------------
    water_tint = _vec("WaterTint", (0.012, 0.055, 0.060, 1.0), -800, -420)   # deep pond green-blue
    shore_tint = _vec("ShoreTint", (0.045, 0.140, 0.130, 1.0), -800, -300)   # lighter shallows teal
    color = _expr(unreal.MaterialExpressionLinearInterpolate, -440, -380)
    mel.connect_material_expressions(water_tint, "", color, "A")
    mel.connect_material_expressions(shore_tint, "", color, "B")
    mel.connect_material_expressions(near_shore, "", color, "Alpha")
    mel.connect_material_property(color, "", unreal.MaterialProperty.MP_BASE_COLOR)

    # --- Opacity: base, more opaque at grazing (Fresnel) + at shore (shallows) -
    fresnel = _expr(unreal.MaterialExpressionFresnel, -800, 40)
    fresnel.set_editor_property("exponent", 4.0)
    fresnel.set_editor_property("base_reflect_fraction", 0.04)
    grazing_or_shore = _expr(unreal.MaterialExpressionMax, -560, 0)
    mel.connect_material_expressions(fresnel, "", grazing_or_shore, "A")
    mel.connect_material_expressions(near_shore, "", grazing_or_shore, "B")
    base_op = _scalar("WaterOpacity", 0.72, -560, 140)
    opacity = _expr(unreal.MaterialExpressionLinearInterpolate, -360, 60)
    mel.connect_material_expressions(base_op, "", opacity, "A")
    one = _expr(unreal.MaterialExpressionConstant, -560, 220)
    one.set_editor_property("r", 1.0)
    mel.connect_material_expressions(one, "", opacity, "B")
    mel.connect_material_expressions(grazing_or_shore, "", opacity, "Alpha")
    mel.connect_material_property(opacity, "", unreal.MaterialProperty.MP_OPACITY)

    # --- Roughness: low so Lumen reflects sky + trees -------------------------
    rough = _scalar("Roughness", 0.06, -360, 260)
    mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    spec = _scalar("Specular", 1.0, -360, 360)
    mel.connect_material_property(spec, "", unreal.MaterialProperty.MP_SPECULAR)

    mel.recompile_material(mat)
    eal.save_asset(MAT_PATH)
    _log("authored %s (translucent, animated-normal ripple + fresnel + shore foam)" % MAT_PATH)
    return mat


def _ensure_mic(base_mat):
    if eal.does_asset_exist(MIC_PATH):
        eal.delete_asset(MIC_PATH)
    mic = at.create_asset(MIC_NAME, DIR, unreal.MaterialInstanceConstant,
                          unreal.MaterialInstanceConstantFactoryNew())
    if mic is None:
        raise RuntimeError("failed to create " + MIC_PATH)
    mel.set_material_instance_parent(mic, base_mat)
    eal.save_asset(MIC_PATH)
    _log("created %s (parent %s) -- tune live in the Details panel" % (MIC_PATH, MAT_NAME))
    return mic


def _reassign_actors(mic):
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    n = 0
    for a in eas.get_all_level_actors():
        try:
            lbl = a.get_actor_label()
        except Exception:
            continue
        if not lbl.startswith(WATER_LABEL_PREFIX):
            continue
        try:
            dmc = a.get_editor_property("dynamic_mesh_component")
            if dmc is not None:
                dmc.set_material(0, mic)
                n += 1
        except Exception as exc:
            _log("reassign %s note: %s" % (lbl, str(exc)[:60]))
    return n


def main():
    _log("=== GOL-164 WATER MATERIAL START ===")
    mpc = eal.load_asset(MPC_PATH)
    if mpc is None:
        _log("FATAL: %s missing -- run build_flag_wind_material.py first (shared wind source)" % MPC_PATH)
        return
    tex = _ensure_water_normal()
    mat = _build_material(tex, mpc)
    mic = _ensure_mic(mat)
    n = _reassign_actors(mic)
    _log("re-assigned %s to %d Water_* actor(s)" % (MIC_NAME, n))
    if n == 0:
        _log("NOTE: 0 water actors -- open GolfForgeDemoBlack.umap (water is course-only).")
    _log("PERSISTENT actor material change: SAVE GolfForgeDemoBlack.umap to keep it.")
    _log("=== DONE ===")


main()
