# build_range_splatmap.py
#
# Generates the weight-paint masks for the flat synthetic Practice Range
# (M_PracticeRange). This is the engine-side analogue of the Mac OSM pipeline's
# build_splatmap.py, but for a hand-defined rectangular range -- so it is
# stdlib-only for its PNG work (zlib + struct + os): no PIL, no numpy. The only
# `unreal` use is an optional local import to resolve paths/logging in-editor.
# The SAME file runs either standalone via the Windows `py` launcher OR
# inside UE5's embedded Python via execute_unreal_python (the primary path,
# matching every other engine/scripts/*.py).
#
#   In-editor (primary):
#     exec(compile(open(r"<repo>\engine\scripts"
#       r"\build_range_splatmap.py",encoding="utf-8").read(),
#       "build_range_splatmap.py","exec"))
#
#   Standalone (fallback):  py engine\scripts\build_range_splatmap.py
#
# Output: courses/practice-range/splat_{fairway,rough,tee,trees}.png -- four
# mutually-exclusive 8-bit grayscale masks (255 = this layer owns the pixel),
# imported into the flat landscape via Landscape > Manage > Import (Layers
# array, Heightmap unchecked), reusing the existing LII_{Fairway,Rough,Tee,
# Trees} LayerInfos.
#
# The masks assume the PNG spans the landscape footprint exactly; the layout is
# defined in named constants below (all tunable). The play area runs along world
# +X with the tee at the -X end (TEE_AT_MIN_X). UE's splat import maps PNG
# column->X (unflipped) and row->Y (flipped); the layout is Y-symmetric so the
# flip is invisible, but if the tee ends up at the wrong (downrange) end in the
# editor, flip TEE_AT_MIN_X and regenerate.

import os
import struct
import zlib

# ----------------------------------------------------------------- parameters
PNG_SIZE = 505           # px; a default UE New Landscape is ~505x505 verts
WORLD_M = 504.0          # m; that landscape at scale 100 is ~504 m square
YD = 0.9144              # m per yard (exact)

# Open playable LANE (centered; tee at the -X end, ball flies +X), framed by a
# TREE WALL just outside it on all four sides. Land beyond the wall stays rough
# and is hidden behind the trees, so the range reads as a tight corridor rather
# than the full 504 m plain. LANE_LEN_YD is kept at the old 400 so the tee (and
# the PlayerStart placed on it) stays put; only the WIDTH tightened.
LANE_LEN_YD = 400.0      # X extent of the open lane (tee at -X end)
LANE_WID_YD = 70.0       # Y width of the open lane (gap between the side tree walls)
TREE_WALL_YD = 22.0      # depth of the tree wall framing the lane
FAIRWAY_WID_YD = 50.0    # Y width of the centered mown strip (must be < LANE_WID_YD)
TEE_LEN_YD = 15.0        # X length of the tee box (sits on the fairway)
TEE_WID_YD = 12.0        # Y width of the tee box
TEE_AT_MIN_X = True      # tee box at the -X (low-column) end; flip if reversed

LAYERS = ("fairway", "rough", "tee", "trees")

# Output dir: derive the repo root portably. Standalone, this file sits in
# engine/scripts/ (so ../.. is the repo root). Via execute_unreal_python there is
# no usable __file__ (it points at a throwaway temp script under Intermediate/),
# so derive the root from the UE project location (repo/engine/Golfsim -> repo).
# Override by setting an OUT_DIR global before exec.
try:
    _here = os.path.dirname(os.path.abspath(__file__))
except NameError:
    _here = ""
if os.path.basename(_here).lower() == "scripts":
    _ROOT = os.path.normpath(os.path.join(_here, "..", ".."))
else:
    import unreal
    _ROOT = os.path.normpath(os.path.join(
        unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()),
        "..", ".."))
OUT_DIR = globals().get("OUT_DIR") or os.path.join(_ROOT, "courses", "practice-range")


def _log(msg):
    try:
        import unreal
        unreal.log("RANGE_SPLAT: " + str(msg))
    except Exception:
        print("RANGE_SPLAT: " + str(msg))


# --------------------------------------------------------------- PNG (stdlib)
def _write_gray_png(path, width, height, pixels):
    """8-bit grayscale, no interlace. pixels: row-major bytes, len w*h."""
    def _chunk(typ, data):
        body = typ + data
        return (struct.pack(">I", len(data)) + body
                + struct.pack(">I", zlib.crc32(body) & 0xffffffff))

    ihdr = struct.pack(">IIBBBBB", width, height, 8, 0, 0, 0, 0)
    raw = bytearray()
    for y in range(height):
        raw.append(0)                       # per-scanline filter type 0 (None)
        raw.extend(pixels[y * width:(y + 1) * width])
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(_chunk(b"IHDR", ihdr))
        f.write(_chunk(b"IDAT", zlib.compress(bytes(raw), 9)))
        f.write(_chunk(b"IEND", b""))


# --------------------------------------------------------------- geometry
def _classify(wx, wy):
    """Which single layer owns world point (wx, wy) [meters, origin=center].

    A long, narrow OPEN LANE sits centered at the origin (tee at the -X end,
    ball flies +X). A TREE WALL frames the lane just outside it on all four
    sides; land past the wall is rough, hidden behind the trees. Result: a
    tight corridor instead of the full 504 m plain.
    """
    lane_half_len = (LANE_LEN_YD * YD) / 2.0
    lane_half_wid = (LANE_WID_YD * YD) / 2.0
    wall = TREE_WALL_YD * YD

    if abs(wx) > lane_half_len or abs(wy) > lane_half_wid:
        # outside the open lane: the framing tree wall, else hidden rough
        if abs(wx) <= lane_half_len + wall and abs(wy) <= lane_half_wid + wall:
            return "trees"
        return "rough"

    # inside the open lane: tee box (-X end), centered fairway strip, else rough
    if TEE_AT_MIN_X:
        tx0, tx1 = -lane_half_len, -lane_half_len + TEE_LEN_YD * YD
    else:
        tx0, tx1 = lane_half_len - TEE_LEN_YD * YD, lane_half_len
    if tx0 <= wx <= tx1 and abs(wy) <= (TEE_WID_YD * YD) / 2.0:
        return "tee"                        # tee box at the lane's -X end

    if abs(wy) <= (FAIRWAY_WID_YD * YD) / 2.0:
        return "fairway"                    # centered mown strip, full lane length

    return "rough"                          # lane shoulders before the trees


def main():
    _log("=== RANGE SPLATMAP (stdlib) ===")
    if not os.path.isdir(OUT_DIR):
        os.makedirs(OUT_DIR)
    n = PNG_SIZE * PNG_SIZE
    masks = {name: bytearray(n) for name in LAYERS}
    counts = {name: 0 for name in LAYERS}

    for py in range(PNG_SIZE):
        wy = (py + 0.5) / PNG_SIZE * WORLD_M - WORLD_M / 2.0
        row0 = py * PNG_SIZE
        for px in range(PNG_SIZE):
            wx = (px + 0.5) / PNG_SIZE * WORLD_M - WORLD_M / 2.0
            owner = _classify(wx, wy)
            masks[owner][row0 + px] = 255
            counts[owner] += 1

    for name in LAYERS:
        path = os.path.join(OUT_DIR, "splat_%s.png" % name)
        _write_gray_png(path, PNG_SIZE, PNG_SIZE, masks[name])
        pct = 100.0 * counts[name] / n
        _log("wrote %s  (%d px, %.1f%%)" % (path, counts[name], pct))

    _log("layout: %.0f x %.0f-yd open lane, %.0f-yd fairway, tee at %s X end, "
         "%.0f-yd tree wall; world=%.0f m, png=%d px"
         % (LANE_LEN_YD, LANE_WID_YD, FAIRWAY_WID_YD, "-" if TEE_AT_MIN_X else "+",
            TREE_WALL_YD, WORLD_M, PNG_SIZE))
    _log("=== DONE -> %s ===" % OUT_DIR)


main()
