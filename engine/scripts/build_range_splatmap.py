# build_range_splatmap.py
#
# Generates the weight-paint masks for the flat synthetic Practice Range
# (M_PracticeRange). This is the engine-side analogue of the Mac OSM pipeline's
# build_splatmap.py, but for a hand-defined rectangular range -- so it is
# deliberately stdlib-ONLY (zlib + struct + os): no PIL, no numpy, no `unreal`
# import. The SAME file runs either standalone via the Windows `py` launcher OR
# inside UE5's embedded Python via execute_unreal_python (the primary path,
# matching every other engine/scripts/*.py).
#
#   In-editor (primary):
#     exec(compile(open(r"C:\Users\pucho\code\golfsim\engine\scripts"
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

FAIRWAY_LEN_YD = 400.0   # X extent of the fairway strip (centered; tee at -X end)
FAIRWAY_WID_YD = 100.0   # Y width of the centered fairway strip
TEE_LEN_YD = 15.0        # X length of the tee box (sits on the fairway)
TEE_WID_YD = 12.0        # Y width of the tee box
TREE_BAND_YD = 18.0      # thickness of the perimeter tree frame
TEE_AT_MIN_X = True      # tee box at the -X (low-column) end; flip if reversed

LAYERS = ("fairway", "rough", "tee", "trees")

# Output dir: derive the repo root from this file when run standalone; fall back
# to the absolute Windows path when exec'd in UE (no __file__). Override by
# setting an OUT_DIR global before exec.
try:
    _ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                          "..", ".."))
except NameError:
    _ROOT = r"C:\Users\pucho\code\golfsim"
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
    """Which single layer owns world point (wx, wy) [meters, origin=center]."""
    half = WORLD_M / 2.0
    band = TREE_BAND_YD * YD
    if abs(wx) > half - band or abs(wy) > half - band:
        return "trees"                      # perimeter frame (highest priority)

    fair_half_len = (FAIRWAY_LEN_YD * YD) / 2.0
    if TEE_AT_MIN_X:
        tx0, tx1 = -fair_half_len, -fair_half_len + TEE_LEN_YD * YD
    else:
        tx0, tx1 = fair_half_len - TEE_LEN_YD * YD, fair_half_len
    if tx0 <= wx <= tx1 and abs(wy) <= (TEE_WID_YD * YD) / 2.0:
        return "tee"                        # tee box on the fairway

    if abs(wx) <= fair_half_len and abs(wy) <= (FAIRWAY_WID_YD * YD) / 2.0:
        return "fairway"

    return "rough"                          # everything else


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

    _log("layout: %.0f yd x %.0f-yd fairway, tee at %s X end, %.0f-yd tree band; "
         "world=%.0f m, png=%d px"
         % (FAIRWAY_LEN_YD, FAIRWAY_WID_YD, "-" if TEE_AT_MIN_X else "+",
            TREE_BAND_YD, WORLD_M, PNG_SIZE))
    _log("=== DONE -> %s ===" % OUT_DIR)


main()
