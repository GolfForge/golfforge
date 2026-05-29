# scatter_range_trees.py
#
# Reusable, idempotent orchestrator for the Practice Range perimeter tree
# frame (backlog R3). It drives a PCGVolume sized to the flat ~504 m range,
# points it at /Game/PCG/PCG_TreeScatter_Range (the range's own denser graph,
# authored by build_pcg_treescatter.py with DENSITY_PPSM=0.35), and fires
# generation. The painted perimeter `Trees` weight band gates placement inside
# the graph (AttributeFilter Trees>0.3) - no graph change here.
#
# This is a deliberate FORK of scatter_full_landscape.py with the OPPOSITE
# persistence intent: BethPage's scale-up volume is transient/measure-only and
# the umap is never saved (it was a de-risk perf gate). The range trees are a
# permanent feature, so this script sets the PCG component to GenerateOnLoad
# and the saved level regenerates the frame each load. It still does NOT save
# the level itself - the USER eyeballs the frame in PIE and saves (Ctrl+S),
# keeping the persistent .umap change human-gated (project convention).
#
# Run inside the UE5.7 editor Python interpreter via the UnrealClaudeMCP
# `execute_unreal_python` tool. TWO modes:
#
#   MODE A - setup volume + fire generation (one call):
#     exec(compile(open(r"<repo>\engine\scripts"
#                        r"\scatter_range_trees.py",encoding="utf-8")
#                  .read(),"scatter_range_trees.py","exec"))
#
#   MODE B - read the count + spread (a SEPARATE, LATER call, because
#            PCGComponent.generate() is async - counts are NOT valid in the
#            same call that fired it):
#     SCATTER_MODE="report"; exec(compile(open(r"<repo>\engine\scripts"
#                  r"\scatter_range_trees.py",
#                  encoding="utf-8").read(),
#                  "scatter_range_trees.py","exec"))
#
# bridge note: execute_unreal_python returns output:None. All feedback goes
# through unreal.log() under LogPython; read it with get_log_lines. Generation
# completion / instance counts surface on the LATER report() call, never the
# call that fired generate().

import unreal

# ---------------------------------------------------------------- parameters
GRAPH_PATH    = "/Game/PCG/PCG_TreeScatter_Range"   # range's own 0.35 ppsm graph
LEVEL_HINT    = "PracticeRange"                     # logged for sanity only
VOLUME_LABEL  = "PCG_TreeScatter_RangeVolume"       # distinct from BethPage's

# The range landscape is centered at the world origin and spans XY +/-25200 cm
# (504 m), flat (Z within +/-100 cm). Verified via inspect_landscape.
VOLUME_LOCATION = (0.0, 0.0, 0.0)

# Volume half-extents (cm). XY just covers the +/-25200 landscape with a small
# margin so the outermost perimeter trees are not clipped. Z must EXCEED the
# (tiny, flat) relief with headroom or SurfaceSampler finds no surface and
# yields 0 points; 1000 cm is generous for a +/-100 cm range.
HALF_XY_CM    = 25300.0
HALF_Z_CM     = 1000.0

# A PCGVolume spawned from class has a default box-brush half-extent of 100 cm
# (get_actor_bounds LIES for volumes; only PCGBlueprintHelpers.
# get_actor_bounds_pcg is truthful). scale3d is computed against this; if the
# measured brush differs we recompute once.
BRUSH_HALF_CM = 100.0
BOUNDS_TOL_CM = 50.0

# Gate sanity band for the instance count. The tree wall framing the ~400x70-yd
# open lane (22-yd depth) is ~18900 m^2; density is a tuning knob (~0.10 ppsm
# -> ~1900, ~0.5 -> ~9800), so the band is wide and only there to catch real
# failures: a blown Trees filter scatters the whole 504 m plain (~89k, trips the
# ceiling); 0-point / bounds failures trip the floor.
EXPECT_MIN_INST = 1000
EXPECT_MAX_INST = 14000

_TARGET_HALF = (HALF_XY_CM, HALF_XY_CM, HALF_Z_CM)


def _log(msg):
    unreal.log("RANGE_TREES: " + str(msg))


def _editor_actor_subsystem():
    return unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def _editor_world():
    return unreal.get_editor_subsystem(
        unreal.UnrealEditorSubsystem).get_editor_world()


def _find_actor_by_label(label):
    """First level actor whose editor label == label, else None."""
    for a in _editor_actor_subsystem().get_all_level_actors():
        try:
            if a.get_actor_label() == label:
                return a
        except Exception:
            continue
    return None


def _scale_for(brush_half):
    return unreal.Vector(HALF_XY_CM / brush_half,
                         HALF_XY_CM / brush_half,
                         HALF_Z_CM / brush_half)


def _pcg_bounds_half(vol):
    """Truthful per-axis half-extent (cm) of a PCGVolume. get_actor_bounds
    lies for volumes; PCGBlueprintHelpers.get_actor_bounds_pcg is the only
    reliable source. Returns (hx,hy,hz) or None (logged with raw repr)."""
    try:
        b = unreal.PCGBlueprintHelpers.get_actor_bounds_pcg(vol, True)
    except Exception as exc:
        _log("get_actor_bounds_pcg raised: %s" % exc)
        return None
    try:
        mn, mx = b.min, b.max
        return (abs(mx.x - mn.x) * 0.5,
                abs(mx.y - mn.y) * 0.5,
                abs(mx.z - mn.z) * 0.5)
    except Exception:
        pass
    for ext_attr in ("box_extent", "extent"):
        try:
            e = getattr(b, ext_attr)
            return (abs(e.x), abs(e.y), abs(e.z))
        except Exception:
            continue
    _log("UNPARSEABLE bounds struct, raw repr = %r" % (b,))
    return None


def _ensure_volume():
    """Find-or-spawn the range volume at the landscape center and (re)apply the
    computed scale3d. Idempotent and independent of umap save state."""
    eas = _editor_actor_subsystem()
    vol = _find_actor_by_label(VOLUME_LABEL)
    if vol is None:
        vol = eas.spawn_actor_from_class(
            unreal.PCGVolume,
            unreal.Vector(*VOLUME_LOCATION),
            unreal.Rotator(0.0, 0.0, 0.0))
        vol.set_actor_label(VOLUME_LABEL)
        _log("spawned fresh %s" % VOLUME_LABEL)
    else:
        _log("reusing existing %s" % VOLUME_LABEL)
    vol.set_actor_location(unreal.Vector(*VOLUME_LOCATION), False, False)
    vol.set_actor_scale3d(_scale_for(BRUSH_HALF_CM))
    _log("applied loc=%s scale3d=%s (brush_half assumed %.0f cm)"
         % (VOLUME_LOCATION, _scale_for(BRUSH_HALF_CM), BRUSH_HALF_CM))
    return vol


def _verify_bounds(vol):
    """True only if the volume's real PCG bounds match the targets within
    tolerance. If off by a uniform factor (non-default brush) recompute scale
    from the measured brush, re-apply ONCE, re-measure. Fail closed."""
    half = _pcg_bounds_half(vol)
    if half is None:
        _log("bounds FAIL: unreadable")
        return False
    _log("measured half-extents cm = (%.0f, %.0f, %.0f); target = "
         "(%.0f, %.0f, %.0f)" % (half + _TARGET_HALF))
    if all(abs(half[i] - _TARGET_HALF[i]) <= BOUNDS_TOL_CM for i in range(3)):
        _log("bounds PASS")
        return True
    cur = vol.get_actor_scale3d()
    if cur.x and half[0]:
        measured_brush = half[0] / cur.x
        if measured_brush > 0:
            _log("retry: inferred brush_half=%.2f cm, recomputing scale3d"
                 % measured_brush)
            vol.set_actor_scale3d(_scale_for(measured_brush))
            half = _pcg_bounds_half(vol)
            if half is not None:
                _log("re-measured half-extents cm = (%.0f, %.0f, %.0f)" % half)
                if all(abs(half[i] - _TARGET_HALF[i]) <= BOUNDS_TOL_CM
                       for i in range(3)):
                    _log("bounds PASS (after retry)")
                    return True
    _log("bounds FAIL: outside tolerance after retry")
    return False


def _get_pcg_component(vol):
    comp = vol.get_component_by_class(unreal.PCGComponent)
    if comp is None:
        _log("FATAL: PCGVolume has no PCGComponent")
    return comp


def _ensure_graph_assigned(comp):
    graph = unreal.load_asset(GRAPH_PATH)
    if graph is None:
        _log("FATAL: graph not found at %s (run build_pcg_treescatter.py with "
             "DENSITY_PPSM=0.35; GRAPH_PATH=%s first)" % (GRAPH_PATH, GRAPH_PATH))
        return False
    if hasattr(comp, "set_graph"):
        comp.set_graph(graph)
    else:
        comp.set_editor_property("graph", graph)
    _log("graph assigned: %s" % GRAPH_PATH)
    return True


def _set_generate_on_load(comp):
    """Make the saved volume regenerate the frame on every level load, so the
    trees persist without baking. Best-effort across 5.7 enum naming."""
    try:
        trig = unreal.PCGComponentGenerationTrigger.GENERATE_ON_LOAD
    except Exception as exc:
        _log("GenerateOnLoad enum unavailable (%s) - leaving trigger at default"
             % str(exc)[:60])
        return
    try:
        comp.set_editor_property("generation_trigger", trig)
        _log("generation_trigger = GenerateOnLoad (persists across load)")
    except Exception as exc:
        _log("could not set generation_trigger (%s)" % str(exc)[:60])


def _iter_skinned_components():
    """Every InstancedSkinnedMeshComponent in the level (PCG SkinnedMesh
    spawner output), yielded with its owning actor."""
    for a in _editor_actor_subsystem().get_all_level_actors():
        try:
            comps = a.get_components_by_class(
                unreal.InstancedSkinnedMeshComponent)
        except Exception:
            continue
        for c in comps:
            yield a, c


def _instance_count(c):
    """Instance count for an InstancedSkinnedMeshComponent. Instances live in
    the `instance_data` array; get_instance_count()/get_num_instances() are
    static-ISM only and SILENTLY return 0 for skinned, so try the array first."""
    try:
        v = c.get_editor_property("instance_data")
        if hasattr(v, "__len__"):
            return len(v)
    except Exception:
        pass
    for getter in ("get_instance_count", "get_num_instances"):
        if hasattr(c, getter):
            try:
                n = int(getattr(c, getter)())
                if n > 0:
                    return n
            except Exception:
                pass
    return 0


def main():
    """MODE A: setup volume + fire generation. Never reads counts (async)."""
    _log("=== RANGE TREE FRAME START (level hint: %s) ===" % LEVEL_HINT)

    vol = _ensure_volume()
    if not _verify_bounds(vol):
        _log("ABORT: bounds FAILED - not generating (fail closed)")
        return

    comp = _get_pcg_component(vol)
    if comp is None or not _ensure_graph_assigned(comp):
        _log("ABORT: PCG component / graph not ready")
        return
    _set_generate_on_load(comp)

    comp.generate(True)
    _log("generate(True) fired ASYNC. DO NOT read counts now. Wait, then run "
         "report() in a SEPARATE later call (SCATTER_MODE='report').")
    _log("=== RANGE TREE FRAME MODE A DONE (generation in flight) ===")


def report():
    """MODE B: a LATER, separate call. Reads the instance count + spatial
    spread + a light FPS/unit dump. Measure-only: does NOT save the level."""
    _log("=== RANGE TREE FRAME REPORT ===")
    vol = _find_actor_by_label(VOLUME_LABEL)
    if vol is None:
        _log("ERROR: %s not found - did MODE A run?" % VOLUME_LABEL)
        return

    total = 0
    n_comp = 0
    xs, ys = [], []
    for owner, c in _iter_skinned_components():
        if owner.get_actor_label() != VOLUME_LABEL:
            continue
        cnt = _instance_count(c)
        if cnt <= 0:
            continue
        total += cnt
        n_comp += 1
        step = max(1, cnt // 24)
        for i in range(0, cnt, step):
            try:
                t = c.get_instance_transform(i, True)   # world space
                xs.append(t.translation.x)
                ys.append(t.translation.y)
            except Exception:
                break
        _log("  component on '%s': %d instances" % (owner.get_actor_label(), cnt))

    _log("RANGE TREE TOTAL (%s) = %d across %d component(s)"
         % (VOLUME_LABEL, total, n_comp))
    if total < EXPECT_MIN_INST:
        _log("WARNING: total %d < expected floor %d - possible 0-point / "
             "bounds / Trees-filter failure (is the Trees layer painted?)"
             % (total, EXPECT_MIN_INST))
    elif total > EXPECT_MAX_INST:
        _log("WARNING: total %d > expected ceiling %d - density/bounds off"
             % (total, EXPECT_MAX_INST))
    else:
        _log("total within expected band [%d, %d]"
             % (EXPECT_MIN_INST, EXPECT_MAX_INST))

    if xs and ys:
        _log("spatial spread (sampled) X[%.0f, %.0f] Y[%.0f, %.0f] cm "
             "(expect ~ +/-25200 for a perimeter frame)"
             % (min(xs), max(xs), min(ys), max(ys)))
    else:
        _log("spatial spread: no instance transforms sampled")

    world = _editor_world()
    for cmd in ("stat unit", "stat fps"):
        try:
            unreal.SystemLibrary.execute_console_command(world, cmd)
            _log("issued console: %s" % cmd)
        except Exception as exc:
            _log("console '%s' failed: %s" % (cmd, exc))

    _log("NEXT: look from the tee in PIE. If the frame reads right, SAVE the "
         "level (Ctrl+S) to persist the volume - GenerateOnLoad rebuilds the "
         "frame each load. To retune, re-author the graph at a new density "
         "(build_pcg_treescatter.py DENSITY_PPSM=...; GRAPH_PATH=%s) then re-run "
         "MODE A." % GRAPH_PATH)
    _log("=== RANGE TREE FRAME REPORT DONE ===")


if globals().get("SCATTER_MODE") == "report":
    report()
else:
    main()
