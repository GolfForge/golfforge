# scatter_full_landscape.py
#
# Reusable, idempotent orchestrator for the golfsim full-course PCG tree
# scatter (Milestone 0.8.5 scale-up). Drives a transient, full-extent
# PCGVolume over the whole landscape, points it at the prebuilt
# /Game/PCG/PCG_TreeScatter graph, and fires generation. It does NOT build
# or modify the graph (that is build_pcg_treescatter.py's job, and density
# lives only there) and it does NOT save the level - the full volume is a
# measure-only, transient session actor by design (the de-risk perf gate
# is read, then we STOP for an explicit go/no-go before anything persists).
#
# Run inside the UE5.7 editor Python interpreter via the UnrealClaudeMCP
# `execute_unreal_python` tool. TWO modes:
#
#   MODE A - setup + fire generation (one call):
#     exec(compile(open(r"<repo>\engine\scripts"
#                        r"\scatter_full_landscape.py",encoding="utf-8")
#                  .read(),"scatter_full_landscape.py","exec"))
#
#   MODE B - read the gate metrics (a SEPARATE, LATER call, because
#            PCGComponent.generate() is async - counts are NOT valid in
#            the same call that fired it):
#     SCATTER_MODE="report"; exec(compile(open(r"<repo>\engine\scripts"
#                  r"\scatter_full_landscape.py",
#                  encoding="utf-8").read(),
#                  "scatter_full_landscape.py","exec"))
#
# Volume strategy: a FRESH actor labelled PCG_TreeScatter_FullVolume is
# found-or-spawned each run and (re)scaled from the landscape bounds. The
# small de-risk volume PCG_TreeScatter_TestVolume (PCGVolume_0, baked into
# the umap) is NEVER touched or generated - it is found-and-logged only.
#
# bridge note: execute_unreal_python returns output:None. All feedback
# goes through unreal.log() under LogPython; read it with get_log_lines.
# Generation completion / instance counts surface under LogPCG / LogPython
# on the LATER report() call, never the call that fired generate().

import unreal

# ---------------------------------------------------------------- parameters
GRAPH_PATH        = "/Game/PCG/PCG_TreeScatter"
LEVEL_HINT        = "BethPageBlack"            # logged for sanity only
VOLUME_LABEL      = "PCG_TreeScatter_FullVolume"
TEST_VOLUME_LABEL = "PCG_TreeScatter_TestVolume"   # de-risk volume; NEVER touch

# World-space center of the course landscape. BethPage's Landscape_0 sits
# at actor loc (-100800,-100800,0) and spans XY +/-100800 cm about the
# world origin, so its center is (0,0,0). Kept a parameter (per-course).
VOLUME_LOCATION   = (0.0, 0.0, 0.0)

# Volume half-extents (cm). XY must cover the full landscape (+/-100800)
# with a small margin; Z must EXCEED local relief (~2154 cm for the 43 m
# BethPage range) with headroom or SurfaceSampler finds no surface inside
# the box and yields 0 points.
HALF_XY_CM        = 101000.0                   # 100800 landscape + 200 margin
HALF_Z_CM         = 3000.0                     # > ~2154 relief, generous

# A PCGVolume spawned from class has a default box-brush half-extent of
# 100 cm (verified - get_actor_bounds LIES for volumes; only
# PCGBlueprintHelpers.get_actor_bounds_pcg is truthful). scale3d is
# computed against this; if the measured brush differs we recompute once.
BRUSH_HALF_CM     = 100.0
BOUNDS_TOL_CM     = 50.0                        # per-axis |measured-target|

# Gate sanity band for the instance count. De-risk: 2265 over a 500 m box
# (0.25 km^2); the full ~4.06 km^2 Trees-painted footprint at the same
# 0.02 ppsm projects to ~30-40 k. Outside [floor,ceil] => loud warning.
EXPECT_MIN_INST   = 25000
EXPECT_MAX_INST   = 60000

_TARGET_HALF = (HALF_XY_CM, HALF_XY_CM, HALF_Z_CM)


def _log(msg):
    unreal.log("PCG_SCATTER: " + str(msg))


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
    reliable source. Returns (hx,hy,hz) or None if the struct can't be
    parsed (logged with raw repr so the operator can diagnose)."""
    try:
        b = unreal.PCGBlueprintHelpers.get_actor_bounds_pcg(vol, True)
    except Exception as exc:
        _log("get_actor_bounds_pcg raised: %s" % exc)
        return None
    # Box(min/max), or origin/extent, or center/box_extent forms.
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


def _ensure_full_volume():
    """Find-or-spawn PCG_TreeScatter_FullVolume at the landscape center and
    (re)apply the computed scale3d. Idempotent and independent of umap save
    state (transform is re-applied every run). Never touches the de-risk
    test volume."""
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
    tolerance. If off by a uniform factor (non-default brush) recompute
    scale from the measured brush, re-apply ONCE, re-measure. Fail closed
    - the caller must not scatter into an unverified volume."""
    half = _pcg_bounds_half(vol)
    if half is None:
        _log("bounds FAIL: unreadable")
        return False
    _log("measured half-extents cm = (%.0f, %.0f, %.0f); target = "
         "(%.0f, %.0f, %.0f)" % (half + _TARGET_HALF))
    if all(abs(half[i] - _TARGET_HALF[i]) <= BOUNDS_TOL_CM for i in range(3)):
        _log("bounds PASS")
        return True
    # One corrective pass: infer the true brush half from measured X.
    cur = vol.get_actor_scale3d()
    if cur.x and half[0]:
        measured_brush = half[0] / cur.x
        if measured_brush > 0:
            _log("retry: inferred brush_half=%.2f cm, recomputing scale3d"
                 % measured_brush)
            vol.set_actor_scale3d(_scale_for(measured_brush))
            half = _pcg_bounds_half(vol)
            if half is not None:
                _log("re-measured half-extents cm = (%.0f, %.0f, %.0f)"
                     % half)
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
    """Point the volume's PCG component at the prebuilt graph asset. We
    consume the graph as-is - never edit it here."""
    graph = unreal.load_asset(GRAPH_PATH)
    if graph is None:
        _log("FATAL: graph not found at %s" % GRAPH_PATH)
        return False
    if hasattr(comp, "set_graph"):
        comp.set_graph(graph)
    else:
        comp.set_editor_property("graph", graph)
    _log("graph assigned: %s" % GRAPH_PATH)
    return True


def _iter_skinned_components():
    """Every InstancedSkinnedMeshComponent in the level (PCG SkinnedMesh
    spawner output). Yielded with its owning actor for logging."""
    for a in _editor_actor_subsystem().get_all_level_actors():
        try:
            comps = a.get_components_by_class(
                unreal.InstancedSkinnedMeshComponent)
        except Exception:
            continue
        for c in comps:
            yield a, c


def _instance_count(c):
    """Instance count for an InstancedSkinnedMeshComponent. The instances
    live in the `instance_data` array property; get_instance_count() /
    get_num_instances() are static-ISM only and SILENTLY return 0 for
    skinned (no exception), so the array length must be tried first."""
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
    """MODE A: setup + fire generation. Never reads counts (async)."""
    _log("=== SCALE-UP START (level hint: %s) ===" % LEVEL_HINT)

    tv = _find_actor_by_label(TEST_VOLUME_LABEL)
    _log("de-risk test volume %s: %s (NOT touched)"
         % (TEST_VOLUME_LABEL, "present" if tv else "absent"))

    vol = _ensure_full_volume()
    if not _verify_bounds(vol):
        _log("ABORT: bounds FAILED - not generating (fail closed)")
        return

    comp = _get_pcg_component(vol)
    if comp is None or not _ensure_graph_assigned(comp):
        _log("ABORT: PCG component / graph not ready")
        return

    comp.generate(True)
    _log("generate(True) fired ASYNC. DO NOT read counts now. Wait, then "
         "run report() in a SEPARATE later call (SCATTER_MODE='report').")
    _log("=== SCALE-UP MODE A DONE (generation in flight) ===")


def report():
    """MODE B: a LATER, separate call. Reads instance count + dumps perf
    metrics + emits the gate template. Measure-only: nothing is tuned or
    saved here."""
    _log("=== GATE REPORT ===")
    vol = _find_actor_by_label(VOLUME_LABEL)
    if vol is None:
        _log("ERROR: %s not found - did MODE A run?" % VOLUME_LABEL)
        return

    gate_total = 0          # the scale-up number (FullVolume only)
    other_total = 0         # de-risk test volume / anything else
    n_comp = 0
    xs, ys = [], []
    for owner, c in _iter_skinned_components():
        cnt = _instance_count(c)
        if cnt <= 0:
            continue
        is_gate = owner.get_actor_label() == VOLUME_LABEL
        if is_gate:
            gate_total += cnt
            n_comp += 1
            # Spatial-spread sanity (FullVolume only): sample ~24
            # instances (avoid a per-instance loop) and track XY extremes.
            step = max(1, cnt // 24)
            for i in range(0, cnt, step):
                try:
                    t = c.get_instance_transform(i, True)  # world space
                    loc = t.translation
                    xs.append(loc.x)
                    ys.append(loc.y)
                except Exception:
                    break
        else:
            other_total += cnt
        _log("  component on '%s': %d instances%s"
             % (owner.get_actor_label(), cnt,
                "  [GATE]" if is_gate else "  [excluded from gate]"))

    _log("GATE INSTANCE TOTAL (%s) = %d across %d component(s); "
         "other skinned (excluded) = %d"
         % (VOLUME_LABEL, gate_total, n_comp, other_total))
    if gate_total < EXPECT_MIN_INST:
        _log("WARNING: gate total %d < expected floor %d - possible "
             "0-point / bounds / Trees-filter failure"
             % (gate_total, EXPECT_MIN_INST))
    elif gate_total > EXPECT_MAX_INST:
        _log("WARNING: gate total %d > expected ceiling %d - density/"
             "bounds off" % (gate_total, EXPECT_MAX_INST))
    else:
        _log("gate total within expected band [%d, %d]"
             % (EXPECT_MIN_INST, EXPECT_MAX_INST))
    total = gate_total

    if xs and ys:
        _log("spatial spread (sampled) X[%.0f, %.0f] Y[%.0f, %.0f] cm "
             "(expect ~ +/-100800 if landscape-wide)"
             % (min(xs), max(xs), min(ys), max(ys)))
    else:
        _log("spatial spread: no instance transforms sampled")

    world = _editor_world()
    for cmd in ("stat unit", "stat fps", "stat streaming",
                "rhi.DumpMemory"):
        try:
            unreal.SystemLibrary.execute_console_command(world, cmd)
            _log("issued console: %s" % cmd)
        except Exception as exc:
            _log("console '%s' failed: %s" % (cmd, exc))

    _log("---- M0.8.5 SCALE-UP GATE (fill USER-read rows) ----")
    _log("  instances        = %d   (de-risk ref 2265)" % total)
    _log("  GPU total MB      = <read from LogRHI rhi.DumpMemory>")
    _log("  editor FPS        = <USER reads focused viewport stat fps>")
    _log("  texpool over MB   = <USER reads stat streaming>")
    _log("  no density tuning; umap NOT saved; full volume transient")
    _log("=== GATE REPORT DONE ===")


if globals().get("SCATTER_MODE") == "report":
    report()
else:
    main()
