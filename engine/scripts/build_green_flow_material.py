"""Author /Game/Materials/M_GreenFlow.uasset for AGreenBreakGridActor (GOL-203 break grid).

PGA-2K-style "flowing dots" green-reading overlay: an UNLIT TRANSLUCENT surface material whose
pixels are a world-space dot lattice drifting downhill along a per-cell flow field. The flow field
arrives as a small runtime texture (UI/GreenFlowTexture.cpp encodes it: RG = fall dir [-1,1]->[0,1],
B = slope / SlopeMax, A = on-green mask); animation is the classic TWO-PHASE FLOW-MAP technique --
two copies of the dot lattice offset along the flow by Frac(Time/Cycle) half a cycle apart, blended
by Abs(2*P0-1) so each copy's wrap-around snap happens at zero weight. Everything is driven by the
material Time node: zero per-frame C++ cost.

Parameters (defaults here; AGreenBreakGridActor's MID sets FlowTex + GridWorldToUV per green):
  FlowTex           (Texture)  flow field (linear color)
  GridWorldToUV     (Vector4)  (MinX, MinY, 1/SpanX, 1/SpanY): world cm -> flow UV
  DotDensityPerMeter(Scalar)   dot lattice pitch (3 -> one dot per ~33 cm)
  DotRadius         (Scalar)   dot radius in lattice cells (0..0.5)
  FlowSpeed         (Scalar)   lattice cells traveled per cycle at full slope
  CycleSeconds      (Scalar)   seconds per flow phase
  SlopeTintLow/High (Vector)   flat (cool) -> steep (hot) dot tint
  OpacityScalar     (Scalar)   master opacity
  MinFlatOpacity    (Scalar)   dim factor for flat (slope ~ 0) cells

FALLBACK=True authors a texture-free single-phase variant instead: the flow data comes from the
mesh VERTEX COLORS (AGreenBreakGridActor bakes the same encoding per vertex) and the wrap snap is
hidden by a sin(pi*phase) opacity pulse. Use it if the two-phase graph misbehaves -- zero C++ change.

Idempotent: re-running deletes + recreates the material (MaterialEditingLibrary can't rewire an
existing graph in 5.7 -- cookbook). A tiny neutral default texture (T_GreenFlowDefault) is generated
+ imported by this script so the LinearColor sampler compiles before any MID override exists.

Run in the UE5.7 editor Python interpreter via execute_unreal_python (close PIE first):
    exec(compile(open(r"<repo>\\engine\\scripts\\build_green_flow_material.py",
        encoding="utf-8").read(), "build_green_flow_material.py", "exec"))

bridge note: execute_unreal_python returns output:None. Feedback via unreal.log() under LogPython.
"""
import os
import struct
import zlib

import unreal

FALLBACK = False   # True -> vertex-color single-phase "pulsing flow" variant (no texture)

DIR = "/Game/Materials"
MAT_NAME = "M_GreenFlow"
MAT_PATH = f"{DIR}/{MAT_NAME}"
DEFTEX_NAME = "T_GreenFlowDefault"
DEFTEX_PATH = f"{DIR}/{DEFTEX_NAME}"

mel = unreal.MaterialEditingLibrary
eal = unreal.EditorAssetLibrary
at = unreal.AssetToolsHelpers.get_asset_tools()


def _log(m):
    unreal.log("GREENFLOW_MAT: " + str(m))


# ---------------------------------------------------------------- default flow texture
def _write_neutral_png(path):
    """8x8 RGBA PNG of the neutral flow pixel (128,128,0,0): zero flow, zero slope, masked out."""
    w = h = 8
    row = b"\x00" + bytes([128, 128, 0, 0] * w)   # filter 0 + pixels
    raw = row * h

    def chunk(tag, data):
        c = tag + data
        return struct.pack(">I", len(data)) + c + struct.pack(">I", zlib.crc32(c) & 0xFFFFFFFF)

    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(raw))
           + chunk(b"IEND", b""))
    with open(path, "wb") as f:
        f.write(png)


def _ensure_default_texture():
    if eal.does_asset_exist(DEFTEX_PATH):
        return eal.load_asset(DEFTEX_PATH)
    proj = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
    tmp = os.path.join(proj, "Saved", "greenflow_default.png")
    _write_neutral_png(tmp)
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", tmp)
    task.set_editor_property("destination_path", DIR)
    task.set_editor_property("destination_name", DEFTEX_NAME)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", True)
    at.import_asset_tasks([task])
    tex = eal.load_asset(DEFTEX_PATH)
    if tex is None:
        raise RuntimeError("default flow texture import failed: " + tmp)
    # Data texture: linear, no mips/compression artifacts on a flow field.
    tex.set_editor_property("srgb", False)
    tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_VECTOR_DISPLACEMENTMAP)
    tex.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    eal.save_asset(DEFTEX_PATH)
    _log("authored %s (neutral 8x8 linear)" % DEFTEX_PATH)
    return tex


# ---------------------------------------------------------------- graph helpers
def _probe():
    """Fail fast with a clear log if a 5.7 expression class is missing."""
    needed = ["MaterialExpressionSmoothStep", "MaterialExpressionDistance",
              "MaterialExpressionTime", "MaterialExpressionFrac",
              "MaterialExpressionAppendVector", "MaterialExpressionVertexColor",
              "MaterialExpressionSine"]
    missing = [n for n in needed if not hasattr(unreal, n)]
    if missing:
        raise RuntimeError("missing expression classes: " + ", ".join(missing))
    _log("probe ok (%d expression classes present)" % len(needed))


def main():
    _log("=== GREEN FLOW MATERIAL START (FALLBACK=%s) ===" % FALLBACK)
    _probe()
    deftex = _ensure_default_texture()

    if eal.does_asset_exist(MAT_PATH):
        eal.delete_asset(MAT_PATH)
    mat = at.create_asset(MAT_NAME, DIR, unreal.Material, unreal.MaterialFactoryNew())
    if mat is None:
        raise RuntimeError("failed to create " + MAT_PATH)

    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("two_sided", True)

    def expr(cls, x, y):
        return mel.create_material_expression(mat, cls, x, y)

    def scalar(name, default, x, y):
        e = expr(unreal.MaterialExpressionScalarParameter, x, y)
        e.set_editor_property("parameter_name", name)
        e.set_editor_property("default_value", default)
        return e

    def vector(name, rgba, x, y):
        e = expr(unreal.MaterialExpressionVectorParameter, x, y)
        e.set_editor_property("parameter_name", name)
        e.set_editor_property("default_value", unreal.LinearColor(*rgba))
        return e

    def const(v, x, y):
        e = expr(unreal.MaterialExpressionConstant, x, y)
        e.set_editor_property("r", v)
        return e

    def const2(a, b, x, y):
        e = expr(unreal.MaterialExpressionConstant2Vector, x, y)
        e.set_editor_property("r", a)
        e.set_editor_property("g", b)
        return e

    def op2(cls, a, a_out, b, b_out, x, y, a_in="A", b_in="B"):
        e = expr(cls, x, y)
        mel.connect_material_expressions(a, a_out, e, a_in)
        mel.connect_material_expressions(b, b_out, e, b_in)
        return e

    def mul(a, a_out, b, b_out, x, y):
        return op2(unreal.MaterialExpressionMultiply, a, a_out, b, b_out, x, y)

    def sub(a, a_out, b, b_out, x, y):
        return op2(unreal.MaterialExpressionSubtract, a, a_out, b, b_out, x, y)

    def frac(a, a_out, x, y):
        e = expr(unreal.MaterialExpressionFrac, x, y)
        mel.connect_material_expressions(a, a_out, e, "")
        return e

    # ------------------------------------------------------------ shared inputs
    wp = expr(unreal.MaterialExpressionWorldPosition, -2200, -400)
    wxy = expr(unreal.MaterialExpressionComponentMask, -2000, -400)
    wxy.set_editor_property("r", True); wxy.set_editor_property("g", True)
    wxy.set_editor_property("b", False); wxy.set_editor_property("a", False)
    mel.connect_material_expressions(wp, "", wxy, "")

    # User-tuned 2026-06-12 (two PIE passes): 3.0 dots/m read too dense -> 1.5 (one dot per ~67 cm);
    # radius in lattice-cell units, tuned to ~13 cm dots; flow slowed to ~15 cm/s at full slope
    # (halving the density doubled the apparent speed -- cells/cycle is lattice-relative).
    p_density = scalar("DotDensityPerMeter", 1.5, -2200, -160)
    p_radius = scalar("DotRadius", 0.096, -2200, -80)
    p_speed = scalar("FlowSpeed", 0.45, -2200, 0)
    p_cycle = scalar("CycleSeconds", 2.0, -2200, 80)
    p_opacity = scalar("OpacityScalar", 0.85, -2200, 160)
    p_minflat = scalar("MinFlatOpacity", 0.35, -2200, 240)
    p_tintlo = vector("SlopeTintLow", (0.55, 0.75, 1.0, 1.0), -2200, 320)
    p_tinthi = vector("SlopeTintHigh", (1.0, 0.25, 0.15, 1.0), -2200, 480)

    # DotUV = Wxy(cm) * DotDensityPerMeter/100 -- one lattice cell per dot.
    percm = mul(p_density, "", const(0.01, -2000, -120), "", -1840, -160)
    dotuv = mul(wxy, "", percm, "", -1680, -300)

    if not FALLBACK:
        # -------------------------------------------------------- flow texture sample
        p_grid = vector("GridWorldToUV", (0.0, 0.0, 1.0, 1.0), -2200, -700)
        gmin = op2(unreal.MaterialExpressionAppendVector, p_grid, "R", p_grid, "G", -2000, -720)
        ginv = op2(unreal.MaterialExpressionAppendVector, p_grid, "B", p_grid, "A", -2000, -640)
        flowuv = mul(sub(wxy, "", gmin, "", -1840, -700), "", ginv, "", -1680, -700)

        samp = expr(unreal.MaterialExpressionTextureSampleParameter2D, -1520, -740)
        samp.set_editor_property("parameter_name", "FlowTex")
        samp.set_editor_property("texture", deftex)
        samp.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
        mel.connect_material_expressions(flowuv, "", samp, "UVs")

        dir_rg = expr(unreal.MaterialExpressionComponentMask, -1340, -760)
        dir_rg.set_editor_property("r", True); dir_rg.set_editor_property("g", True)
        dir_rg.set_editor_property("b", False); dir_rg.set_editor_property("a", False)
        mel.connect_material_expressions(samp, "", dir_rg, "")
        flow_dir = sub(mul(dir_rg, "", const(2.0, -1340, -680), "", -1180, -760),
                       "", const(1.0, -1180, -680), "", -1020, -760)
        mag_out, mag_name = samp, "B"     # slope magnitude 0..1
        mask_out, mask_name = samp, "A"   # on-green mask
    else:
        # -------------------------------------------------------- vertex-color flow field
        vc = expr(unreal.MaterialExpressionVertexColor, -2000, -700)
        dir_rg = expr(unreal.MaterialExpressionComponentMask, -1840, -760)
        dir_rg.set_editor_property("r", True); dir_rg.set_editor_property("g", True)
        dir_rg.set_editor_property("b", False); dir_rg.set_editor_property("a", False)
        mel.connect_material_expressions(vc, "", dir_rg, "")
        flow_dir = sub(mul(dir_rg, "", const(2.0, -1840, -680), "", -1680, -760),
                       "", const(1.0, -1680, -680), "", -1520, -760)
        mag_out, mag_name = vc, "B"
        mask_out, mask_name = vc, "A"

    # ------------------------------------------------------------ phases
    time = expr(unreal.MaterialExpressionTime, -2000, 600)
    t = op2(unreal.MaterialExpressionDivide, time, "", p_cycle, "", -1840, 600)
    p0 = frac(t, "", -1680, 560)
    t_half = op2(unreal.MaterialExpressionAdd, t, "", const(0.5, -1840, 700), "", -1680, 680)
    p1 = frac(t_half, "", -1520, 680)

    def dot_mask(phase, ybase):
        """1 inside a dot of the lattice offset along the flow by this phase, 0 outside."""
        # offset = Dir * (phase * FlowSpeed * Mag)
        amt = mul(mul(phase, "", p_speed, "", -1360, ybase),
                  "", mag_out, mag_name, -1200, ybase)
        off = mul(flow_dir, "", amt, "", -1040, ybase)
        uvp = sub(dotuv, "", off, "", -880, ybase - 40)
        cell = frac(uvp, "", -720, ybase - 40)
        dist = op2(unreal.MaterialExpressionDistance, cell, "",
                   const2(0.5, 0.5, -720, ybase + 40), "", -560, ybase)
        ss = expr(unreal.MaterialExpressionSmoothStep, -400, ybase)
        inner = mul(p_radius, "", const(0.6, -560, ybase + 80), "", -480, ybase + 80)
        mel.connect_material_expressions(inner, "", ss, "Min")
        mel.connect_material_expressions(p_radius, "", ss, "Max")
        mel.connect_material_expressions(dist, "", ss, "Value")
        inv = expr(unreal.MaterialExpressionOneMinus, -240, ybase)
        mel.connect_material_expressions(ss, "", inv, "")
        return inv

    if not FALLBACK:
        dot0 = dot_mask(p0, 200)
        dot1 = dot_mask(p1, 560)
        # W = Abs(2*P0 - 1): 1 exactly when P0 wraps (so the snap is invisible), 0 when P1 wraps.
        w = expr(unreal.MaterialExpressionAbs, -1360, 880)
        mel.connect_material_expressions(
            sub(mul(p0, "", const(2.0, -1680, 880), "", -1600, 880),
                "", const(1.0, -1600, 960), "", -1480, 880), "", w, "")
        blend = expr(unreal.MaterialExpressionLinearInterpolate, -80, 400)
        mel.connect_material_expressions(dot0, "", blend, "A")
        mel.connect_material_expressions(dot1, "", blend, "B")
        mel.connect_material_expressions(w, "", blend, "Alpha")
        dots = blend
        pulse = None
    else:
        dots = dot_mask(p0, 200)
        # sin(pi*P0): dots fade out before the wrap snap and back in after ("pulsing flow").
        pulse = expr(unreal.MaterialExpressionSine, -1360, 880)
        pulse.set_editor_property("period", 2.0)   # sin(2*pi*x / 2) = sin(pi*x)
        mel.connect_material_expressions(p0, "", pulse, "")

    # ------------------------------------------------------------ emissive + opacity
    tint = expr(unreal.MaterialExpressionLinearInterpolate, -400, -200)
    mel.connect_material_expressions(p_tintlo, "", tint, "A")
    mel.connect_material_expressions(p_tinthi, "", tint, "B")
    mel.connect_material_expressions(mag_out, mag_name, tint, "Alpha")
    emissive = mul(tint, "", dots, "", -80, -120)
    mel.connect_material_property(emissive, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    flatdim = expr(unreal.MaterialExpressionLinearInterpolate, -400, 40)
    mel.connect_material_expressions(p_minflat, "", flatdim, "A")
    mel.connect_material_expressions(const(1.0, -560, 40), "", flatdim, "B")
    mel.connect_material_expressions(mag_out, mag_name, flatdim, "Alpha")

    op = mul(mul(dots, "", mask_out, mask_name, 40, 120),
             "", mul(p_opacity, "", flatdim, "", -80, 200), "", 180, 160)
    if pulse is not None:
        op = mul(op, "", pulse, "", 320, 200)
    mel.connect_material_property(op, "", unreal.MaterialProperty.MP_OPACITY)

    mel.recompile_material(mat)
    eal.save_asset(MAT_PATH)
    _log("authored %s (%s)" % (MAT_PATH, "vertex-color fallback" if FALLBACK else "two-phase flow map"))
    _log("DONE")


main()
