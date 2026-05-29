# build_pcg_treescatter.py
#
# Reusable, idempotent constructor for the golfsim PCG tree-scatter graph
# (Milestone 0.8.5). Run inside the UE5.7 editor's Python interpreter, e.g.
# via the UnrealClaudeMCP `execute_unreal_python` tool:
#
#   exec(compile(open(r"<repo>\engine\scripts"
#                     r"\build_pcg_treescatter.py").read(),
#                "build_pcg_treescatter.py", "exec"))
#
# It rebuilds /Game/PCG/PCG_TreeScatter from scratch every run (clears all
# non-input/output nodes + edges, then recreates the full chain), so it is
# safe to re-run and produces an identical graph each time.
#
# SIBLING GRAPHS: GRAPH_PATH and DENSITY_PPSM read from globals() first, so a
# caller can author a separate graph at a different density WITHOUT disturbing
# the BethPage-validated defaults. If GRAPH_PATH names a graph that does not
# exist yet, main() seeds it by duplicating BASE_GRAPH_PATH (so it builds from
# a valid PCGGraph copy). This is how the practice range gets its own denser
# graph (the shared graph stays at the BethPage value):
#
#   DENSITY_PPSM=0.35; GRAPH_PATH="/Game/PCG/PCG_TreeScatter_Range"
#   exec(compile(open(r"<repo>\engine\scripts"
#                     r"\build_pcg_treescatter.py").read(),
#                "build_pcg_treescatter.py", "exec"))
#
# Final graph topology:
#
#   GetLandscape ----------------\
#                                 -> SurfaceSampler -> AttributeFilter(Trees)
#   GraphInput (volume bounds) --/                          |
#                                                           v
#   Output <- SkinnedMeshSpawner <- AddAttribute(Mesh) <- TransformPoints
#
# The painted `Trees` landscape weight layer (LII_Trees, from M0.8) is the
# gate: GetLandscape emits per-layer weights as point attributes, and the
# AttributeFilter keeps only points whose `Trees` weight exceeds a threshold.
# The SkinnedMeshSpawner is attribute-driven (no fixed-mesh field), so
# AddAttribute stamps the Megaplant skeletal-mesh soft path onto every point
# and the spawner's selector reads it back.
#
# bridge note: execute_unreal_python returns output:None. All feedback goes
# through unreal.log() under LogPython; read it with get_log_lines.

import unreal

# ---------------------------------------------------------------- parameters
# BASE_GRAPH_PATH is the canonical (BethPage-validated) graph and the seed for
# any sibling. GRAPH_PATH / DENSITY_PPSM honor a pre-set global so a caller can
# author a separate, denser graph (e.g. the range) without editing this file.
BASE_GRAPH_PATH  = "/Game/PCG/PCG_TreeScatter"
GRAPH_PATH       = globals().get("GRAPH_PATH", BASE_GRAPH_PATH)
# Nanite SkeletalMesh hero tree (Megaplant; gitignored, re-download via Bridge
# on a fresh clone). _D is the lowest-poly full-tree variant.
MESH_SOFT_PATH   = ("/Game/Megaplant_Library/Tree_Silver_Birch/"
                    "Tree_Silver_Birch_01/SK_Silver_Birch_01_D.SK_Silver_Birch_01_D")
TREES_LAYER_NAME = "Trees"     # must match the LII_Trees landscape layer name
TREES_THRESHOLD  = 0.3         # keep points where Trees weight > this
DENSITY_PPSM     = globals().get("DENSITY_PPSM", 0.02)  # points/m^2 (BethPage de-risk default)
SCALE_MIN        = 0.85
SCALE_MAX        = 1.20
MESH_ATTR_NAME   = "Mesh"      # attribute that carries the mesh soft path


def _log(msg):
    unreal.log("PCG_TREESCATTER: " + str(msg))


def _pin_label(pin):
    try:
        return str(pin.get_editor_property("properties").get_editor_property("label"))
    except Exception as exc:
        return "<err %s>" % exc


def _find_pin_label(node, direction, prefer):
    """Return the label of node's first pin in `direction` ('in'/'out') whose
    label case-insensitively contains one of `prefer` (checked in order);
    falls back to the first pin's label."""
    pins = list(node.get_editor_property(
        "input_pins" if direction == "in" else "output_pins"))
    labels = [_pin_label(p) for p in pins]
    for want in prefer:
        for lbl in labels:
            if want.lower() in lbl.lower():
                return lbl
    return labels[0] if labels else None


def _add(graph, settings_cls):
    node, settings = graph.add_node_of_type(settings_cls)
    return node, settings


def _set_struct(owner, prop, mutate):
    """get -> mutate -> set-back (UE returns struct copies by value)."""
    s = owner.get_editor_property(prop)
    mutate(s)
    owner.set_editor_property(prop, s)
    return s


def _set_selector(owner, prop, attr_name):
    """Point a PCGAttributeProperty(Input|Output)Selector at a named
    attribute. In UE5.7 the Python set_attribute_name / plain import_text
    forms are silent no-ops; only the fully-wrapped text form
    `PCGBegin(<name>)PCGEnd` actually takes (verified persistent across
    save/reload). owner may be a settings object or the selector's owning
    UObject; prop is the selector property name."""
    sel = owner.get_editor_property(prop)
    sel.import_text("PCGBegin(%s)PCGEnd" % attr_name)
    owner.set_editor_property(prop, sel)
    return sel


def main():
    _log("target graph = %s @ %.3f ppsm" % (GRAPH_PATH, DENSITY_PPSM))
    graph = unreal.load_asset(GRAPH_PATH)
    if graph is None:
        # A sibling graph (overridden GRAPH_PATH) that doesn't exist yet: seed
        # it by duplicating the base graph so we author from a valid PCGGraph.
        # When GRAPH_PATH IS the base and it's missing, that's a fresh-clone
        # state with nothing to seed from -> fail loud (unchanged behavior).
        eal = unreal.EditorAssetLibrary
        if GRAPH_PATH != BASE_GRAPH_PATH and eal.does_asset_exist(BASE_GRAPH_PATH):
            if eal.duplicate_asset(BASE_GRAPH_PATH, GRAPH_PATH) is not None:
                _log("seeded %s by duplicating %s" % (GRAPH_PATH, BASE_GRAPH_PATH))
                graph = unreal.load_asset(GRAPH_PATH)
        if graph is None:
            _log("FATAL: graph not found at %s (and could not seed from %s)"
                 % (GRAPH_PATH, BASE_GRAPH_PATH))
            return

    # --- 1. clean slate (idempotent) -------------------------------------
    for n in list(graph.get_editor_property("nodes")):
        graph.remove_node(n)
    _log("cleared; nodes now = %d" % len(list(graph.get_editor_property("nodes"))))

    inp = graph.get_input_node()
    outp = graph.get_output_node()

    # --- 2. create nodes -------------------------------------------------
    ls_node,  ls_set  = _add(graph, unreal.PCGGetLandscapeSettings)
    ss_node,  ss_set  = _add(graph, unreal.PCGSurfaceSamplerSettings)
    flt_node, flt_set = _add(graph, unreal.PCGAttributeFilteringSettings)
    xf_node,  xf_set  = _add(graph, unreal.PCGTransformPointsSettings)
    at_node,  at_set  = _add(graph, unreal.PCGAddAttributeSettings)
    sp_node,  sp_set  = _add(graph, unreal.PCGSkinnedMeshSpawnerSettings)

    # --- 3. configure settings ------------------------------------------
    # Get Landscape: emit per-layer weights as attributes (default True;
    # set explicitly so the script is self-documenting / re-run safe).
    _set_struct(ls_set, "sampling_properties",
                lambda sp: sp.set_editor_property("get_layer_weights", True))

    ss_set.set_editor_property("points_per_squared_meter", DENSITY_PPSM)

    # Attribute filter: keep points where Trees weight > TREES_THRESHOLD.
    _set_selector(flt_set, "target_attribute", TREES_LAYER_NAME)
    flt_set.set_editor_property("operator", unreal.PCGAttributeFilterOperator.GREATER)
    flt_set.set_editor_property("use_constant_threshold", True)
    _set_struct(flt_set, "attribute_types", lambda t: (
        t.set_editor_property("type", unreal.PCGMetadataTypes.DOUBLE),
        t.set_editor_property("double_value", float(TREES_THRESHOLD))))

    # Transform: random full yaw + modest uniform scale so it isn't a grid.
    _set_struct(xf_set, "rotation_min", lambda r: r.set_editor_property("yaw", 0.0))
    _set_struct(xf_set, "rotation_max", lambda r: r.set_editor_property("yaw", 360.0))
    _set_struct(xf_set, "scale_min", lambda v: (
        v.set_editor_property("x", SCALE_MIN),
        v.set_editor_property("y", SCALE_MIN),
        v.set_editor_property("z", SCALE_MIN)))
    _set_struct(xf_set, "scale_max", lambda v: (
        v.set_editor_property("x", SCALE_MAX),
        v.set_editor_property("y", SCALE_MAX),
        v.set_editor_property("z", SCALE_MAX)))
    xf_set.set_editor_property("uniform_scale", True)

    # Add Attribute: stamp the skeletal-mesh soft path onto every point.
    # NOTE: unreal.SoftObjectPath(str) constructs EMPTY in UE5.7 (its ctor
    # ignores the arg) and the struct has no settable properties; the only
    # working path is import_text() on the SoftObjectPath sub-struct (verified
    # persistent across save/reload), same family of quirk as the selectors.
    _set_selector(at_set, "output_target", MESH_ATTR_NAME)
    at_const = at_set.get_editor_property("attribute_types")
    at_const.set_editor_property("type", unreal.PCGMetadataTypes.SOFT_OBJECT_PATH)
    sopv = at_const.get_editor_property("soft_object_path_value")
    sopv.import_text(MESH_SOFT_PATH)
    at_const.set_editor_property("soft_object_path_value", sopv)
    at_set.set_editor_property("attribute_types", at_const)

    # Skinned mesh spawner: it reads the mesh soft path off the MESH_ATTR_NAME
    # point attribute created by AddAttribute. The selector default is `@Last`,
    # which does NOT auto-resolve to our attribute at generation time ("Mesh
    # Attribute '@Last' is not in the metadata"), so it must be set explicitly.
    # mesh_selector_parameters is a read-only UObject ref - mutate the
    # mesh_attribute selector on it in place, do not set the param back.
    sel = sp_set.get_editor_property("mesh_selector_parameters")
    _set_selector(sel, "mesh_attribute", MESH_ATTR_NAME)

    # --- 4. discover pin labels -----------------------------------------
    in_out      = _find_pin_label(inp,  "out", ["In", "Out"])
    out_in      = _find_pin_label(outp, "in",  ["Out", "In"])
    ls_out      = _find_pin_label(ls_node, "out", ["Landscape", "Out"])
    ss_surface  = _find_pin_label(ss_node, "in",  ["Surface"])
    ss_bounds   = _find_pin_label(ss_node, "in",  ["Bounding", "Bounds"])
    ss_out      = _find_pin_label(ss_node, "out", ["Out"])
    flt_in      = _find_pin_label(flt_node, "in",  ["In"])
    flt_out     = _find_pin_label(flt_node, "out", ["Inside", "In Filter", "Out"])
    xf_in       = _find_pin_label(xf_node, "in",  ["In"])
    xf_out      = _find_pin_label(xf_node, "out", ["Out"])
    at_in       = _find_pin_label(at_node, "in",  ["In"])
    at_out      = _find_pin_label(at_node, "out", ["Out"])
    sp_in       = _find_pin_label(sp_node, "in",  ["In"])
    sp_out      = _find_pin_label(sp_node, "out", ["Out"])

    for tag, val in [
        ("INPUT.out", in_out), ("OUTPUT.in", out_in),
        ("GetLandscape.out", ls_out),
        ("SurfaceSampler.Surface", ss_surface),
        ("SurfaceSampler.Bounds", ss_bounds), ("SurfaceSampler.out", ss_out),
        ("Filter.in", flt_in), ("Filter.out(kept)", flt_out),
        ("Transform.in", xf_in), ("Transform.out", xf_out),
        ("AddAttr.in", at_in), ("AddAttr.out", at_out),
        ("Spawner.in", sp_in), ("Spawner.out", sp_out)]:
        _log("pin %-24s = %r" % (tag, val))

    # --- 5. wire edges ---------------------------------------------------
    edges = [
        (ls_node,  ls_out,     ss_node,  ss_surface),
        (inp,      in_out,     ss_node,  ss_bounds),
        (ss_node,  ss_out,     flt_node, flt_in),
        (flt_node, flt_out,    xf_node,  xf_in),
        (xf_node,  xf_out,     at_node,  at_in),
        (at_node,  at_out,     sp_node,  sp_in),
        (sp_node,  sp_out,     outp,     out_in),
    ]
    for a, ap, b, bp in edges:
        try:
            graph.add_edge(a, unreal.Name(ap), b, unreal.Name(bp))
            _log("edge OK  %s -> %s" % (ap, bp))
        except Exception as exc:
            _log("edge FAIL %s -> %s : %s" % (ap, bp, exc))

    # --- 6. verify + save ------------------------------------------------
    final_nodes = list(graph.get_editor_property("nodes"))
    _log("final node count = %d (expect 6)" % len(final_nodes))
    chk_sel = sp_set.get_editor_property("mesh_selector_parameters")
    _log("filter target_attribute   = %s"
         % flt_set.get_editor_property("target_attribute").export_text())
    _log("addattr output_target     = %s"
         % at_set.get_editor_property("output_target").export_text())
    _log("addattr mesh soft path    = %s"
         % at_set.get_editor_property("attribute_types")
                 .get_editor_property("soft_object_path_value").export_text())
    _log("spawner mesh_attribute    = %s"
         % chk_sel.get_editor_property("mesh_attribute").export_text())

    unreal.EditorAssetLibrary.save_asset(GRAPH_PATH)
    _log("DONE: %s saved" % GRAPH_PATH)


main()
