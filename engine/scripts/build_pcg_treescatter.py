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
# safe to re-run and produces an identical graph each time. All randomness is
# seeded with fixed constants, so the built graph is byte-identical run to run.
#
# SIBLING GRAPHS: GRAPH_PATH and DENSITY_PPSM read from globals() first, so a
# caller can author a separate graph at a different density WITHOUT disturbing
# the GolfForgeDemo-validated defaults. If GRAPH_PATH names a graph that does not
# exist yet, main() seeds it by duplicating BASE_GRAPH_PATH (so it builds from
# a valid PCGGraph copy). This is how the practice range gets its own denser
# graph (the shared graph stays at the GolfForgeDemo value):
#
#   DENSITY_PPSM=0.35; GRAPH_PATH="/Game/PCG/PCG_TreeScatter_Range"
#   exec(compile(open(r"<repo>\engine\scripts"
#                     r"\build_pcg_treescatter.py").read(),
#                "build_pcg_treescatter.py", "exec"))
#
# Final graph topology (GOL-167 mixed forest, 2 species x 4 variants):
#
#   GetLandscape --------------\
#                               -> SurfaceSampler -> AttributeFilter(Trees>0.3)
#   GraphInput (volume bounds)-/                            |  [fairway/green = 0]
#                                                           v
#                              Roll(ZoneRoll) -> AttributeFilter(Trees>ZoneRoll)
#                                                           |  [perimeter dense, rough sparse]
#                                                           v
#                                              Roll(SppRoll) -> AttributeFilter(SppRoll>SPLIT)
#                                          +-- outside(<=SPLIT) ----+--- inside(>SPLIT) ---+
#                                 == BIRCH branch ==                    == PINE branch ==
#                              TransformPoints(birch scale,          TransformPoints(pine scale,
#                                 yaw, lean +/-3)                       yaw, lean +/-2)
#                              Roll(VarRoll) -> 3-filter chain       Roll(VarRoll) -> 3-filter chain
#                              4 bands -> 4x AddAttribute(Mesh)      4 bands -> 4x AddAttribute(Mesh)
#                              SkinnedMeshSpawner(attr=Mesh)         SkinnedMeshSpawner(attr=Mesh)
#                                          \                        /
#                                           +-------> Output <-----+
#
# WHY THIS SHAPE (see GOL-167):
# * No PCGSkinnedMeshSelectorWeighted exists in this UE5.7 build, so species/
#   variant mixing must write the `Mesh` soft-path attribute PER POINT upstream
#   (one AddAttribute per distinct mesh) and let the attribute-driven spawner
#   read it back -- exactly the proven single-species pattern, fanned out.
# * Selection rolls a seeded per-point uniform [0,1] into a NAMED attribute
#   (Roll = a Density Noise node aimed at an attribute, not the Density channel)
#   and partitions it with a chain of attribute-vs-constant AttributeFilters,
#   using both the inside (>cut) and outside (<=cut) pins so every point lands
#   in exactly one band. (Routing via the Density channel + DensityFilter bands
#   double-counted points -- a keep_zero band swept up a duplicate pile -- so we
#   keep the Density channel out of it entirely.)
# * Zone density is graded off the painted Trees weight: keep where Trees >
#   ZoneRoll, so keep-probability == paint strength (perimeter ~1.0 stays dense,
#   rough ~0.4 thins to ~40%). Risk-free: if the paint is binary (Trees~1 where
#   painted) this keeps ~everything, i.e. today's look.
# * Per-species scale needs scale applied per branch (a single shared spawner
#   could only apply one scale band), hence the parallel Birch/Pine branches.
#
# The painted `Trees` landscape weight layer (LII_Trees, from M0.8) is the gate:
# GetLandscape emits per-layer weights as point attributes; the hard >0.3 filter
# zeroes fairway/green/tee/bunker, and the ZoneRoll filter grades the rest.
#
# bridge note: execute_unreal_python returns output:None. All feedback goes
# through unreal.log() under LogPython; read it with get_log_lines.

import unreal

# ---------------------------------------------------------------- parameters
# BASE_GRAPH_PATH is the canonical (GolfForgeDemo-validated) graph and the seed for
# any sibling. GRAPH_PATH / DENSITY_PPSM honor a pre-set global so a caller can
# author a separate, denser graph (e.g. the range) without editing this file.
BASE_GRAPH_PATH  = "/Game/PCG/PCG_TreeScatter"
GRAPH_PATH       = globals().get("GRAPH_PATH", BASE_GRAPH_PATH)

# Species table. Each variant is a (suffix, weight) pair; the variant routing
# splits each branch's points into equal quartiles (the weight column is kept
# for documentation / future weighted bands -- current bands are equal-width).
# NOTE the asset-name asymmetry: Birch variants are prefixed "SK_", Pine
# variants are NOT -- the readback at the end verifies every soft path resolved.
# All meshes are Megaplant SkeletalMeshes (gitignored, re-download via Bridge).
# scale_min/max are NON-uniform (x,y = width, z = height) so height varies
# independently; pines biased tall+narrow, birches rounder. lean_deg is the
# max random pitch/roll tilt. Override the whole table via globals() if needed.
SPECIES = globals().get("SPECIES", {
    "Birch": {
        "dir":      "/Game/Megaplant_Library/Tree_Silver_Birch/Tree_Silver_Birch_01/",
        "prefix":   "SK_Silver_Birch_01_",
        "variants": [("A", 1), ("B", 1), ("C", 1), ("D", 1)],
        "scale_min": (0.90, 0.90, 0.85), "scale_max": (1.15, 1.15, 1.25),
        "lean_deg":  3.0,
        "seed":      30303,
    },
    "Pine": {
        "dir":      "/Game/Megaplant_Library/Tree_Baltic_Pine/Tree_Baltic_Pine_01/",
        "prefix":   "Baltic_Pine_01_",
        "variants": [("A", 1), ("B", 1), ("C", 1), ("D", 1)],
        "scale_min": (0.80, 0.80, 1.05), "scale_max": (1.05, 1.05, 1.45),
        "lean_deg":  2.0,
        "seed":      40404,
    },
})
# density-roll cut: points with SppRoll <= this go Birch, the rest go Pine.
SPECIES_SPLIT    = globals().get("SPECIES_SPLIT", 0.55)

TREES_LAYER_NAME = "Trees"     # must match the LII_Trees landscape layer name
TREES_THRESHOLD  = 0.3         # hard gate: keep points where Trees weight > this
DENSITY_PPSM     = globals().get("DENSITY_PPSM", 0.02)  # points/m^2 (GolfForgeDemo de-risk default)
MESH_ATTR_NAME   = "Mesh"      # attribute that carries the mesh soft path
ZONE_ATTR_NAME   = "ZoneRoll"  # per-point random used to grade density by paint
SPP_ATTR_NAME    = "SppRoll"   # per-point random used to split species
VAR_ATTR_NAME    = "VarRoll"   # per-point random used to split variants in a branch
SEED_ZONE        = 10101       # fixed seeds -> deterministic / idempotent graph
SEED_SPECIES     = 20202


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


def _pin(node, prefer=None):
    return _find_pin_label(node, "in", prefer or ["In"])


def _pout(node, prefer=None):
    return _find_pin_label(node, "out", prefer or ["Out"])


def _wire(graph, a, a_out, b, b_in):
    try:
        graph.add_edge(a, unreal.Name(a_out), b, unreal.Name(b_in))
        _log("edge OK  %s -> %s" % (a_out, b_in))
    except Exception as exc:
        _log("edge FAIL %s -> %s : %s" % (a_out, b_in, exc))


def _mk_roll(graph, seed, attr):
    """Stamp a per-point uniform [0,1] random into a NAMED double attribute.
    We deliberately write a named attribute (not the Density channel) so the
    split filters can compare it with an ordinary attribute-vs-constant test --
    routing via the Density channel + DensityFilter banding double-counted points
    (the keep_zero band swept up a duplicate pile). Seeded -> identical layout
    every build."""
    node, st = _add(graph, unreal.PCGDensityNoiseSettings)
    st.set_editor_property("mode", unreal.PCGAttributeNoiseMode.SET)
    st.set_editor_property("noise_min", 0.0)
    st.set_editor_property("noise_max", 1.0)
    st.set_editor_property("seed", int(seed))
    _set_selector(st, "output_target", attr)
    return node, st


def _mk_attr_filter(graph, target_attr, cut):
    """AttributeFilter: keep where target_attr > cut on the INSIDE pin, the rest
    on the OUTSIDE pin. Returns (node, inside_label, outside_label)."""
    node, st = _add(graph, unreal.PCGAttributeFilteringSettings)
    _set_selector(st, "target_attribute", target_attr)
    st.set_editor_property("operator", unreal.PCGAttributeFilterOperator.GREATER)
    st.set_editor_property("use_constant_threshold", True)
    _set_struct(st, "attribute_types", lambda t: (
        t.set_editor_property("type", unreal.PCGMetadataTypes.DOUBLE),
        t.set_editor_property("double_value", float(cut))))
    inside  = _pout(node, ["Inside", "In Filter"])
    outside = _pout(node, ["Outside", "Out Filter", "Discarded"])
    return node, inside, outside


def _split_chain(graph, src_node, src_out, roll_attr, cuts):
    """Partition incoming points into len(cuts)+1 ascending bands of `roll_attr`
    using a chain of attribute filters (both pins), so every point lands in
    exactly one band -- no overlap, no point lost. Band i (i<n-1) is the OUTSIDE
    (<= cut[i]) of filter i; the final band is the top INSIDE (> cut[-1]).
    Returns a list of (node, out_label) endpoints, one per band."""
    endpoints = []
    cur_node, cur_out = src_node, src_out
    for c in cuts:
        fn, inside, outside = _mk_attr_filter(graph, roll_attr, c)
        _wire(graph, cur_node, cur_out, fn, _pin(fn))
        endpoints.append((fn, outside))      # points <= c peel off here
        cur_node, cur_out = fn, inside        # points > c continue down the chain
    endpoints.append((cur_node, cur_out))     # whatever survived all cuts (top band)
    return endpoints


def _mk_transform(graph, smin, smax, lean):
    """Full random yaw + NON-uniform scale (x,y width / z height vary
    independently) + small random pitch/roll lean (+/- lean deg)."""
    node, st = _add(graph, unreal.PCGTransformPointsSettings)
    st.set_editor_property("uniform_scale", False)
    _set_struct(st, "scale_min", lambda v: (
        v.set_editor_property("x", smin[0]),
        v.set_editor_property("y", smin[1]),
        v.set_editor_property("z", smin[2])))
    _set_struct(st, "scale_max", lambda v: (
        v.set_editor_property("x", smax[0]),
        v.set_editor_property("y", smax[1]),
        v.set_editor_property("z", smax[2])))
    _set_struct(st, "rotation_min", lambda r: (
        r.set_editor_property("pitch", -lean),
        r.set_editor_property("yaw", 0.0),
        r.set_editor_property("roll", -lean)))
    _set_struct(st, "rotation_max", lambda r: (
        r.set_editor_property("pitch", lean),
        r.set_editor_property("yaw", 360.0),
        r.set_editor_property("roll", lean)))
    return node, st


def _mk_add_attr_mesh(graph, soft_path):
    """Stamp a skeletal-mesh soft path onto every point's MESH_ATTR_NAME.
    SoftObjectPath only takes via import_text() on the sub-struct (UE5.7 ctor is
    a silent no-op) -- same quirk family as the attribute selectors."""
    node, st = _add(graph, unreal.PCGAddAttributeSettings)
    _set_selector(st, "output_target", MESH_ATTR_NAME)
    const = st.get_editor_property("attribute_types")
    const.set_editor_property("type", unreal.PCGMetadataTypes.SOFT_OBJECT_PATH)
    sopv = const.get_editor_property("soft_object_path_value")
    sopv.import_text(soft_path)
    const.set_editor_property("soft_object_path_value", sopv)
    st.set_editor_property("attribute_types", const)
    return node, st


def _mk_spawner(graph):
    """Attribute-driven skinned-mesh spawner: reads the per-point MESH_ATTR_NAME
    soft path. The selector default `@Last` does NOT resolve our attribute, so
    set mesh_attribute explicitly (mutate the read-only param in place)."""
    node, st = _add(graph, unreal.PCGSkinnedMeshSpawnerSettings)
    sel = st.get_editor_property("mesh_selector_parameters")
    _set_selector(sel, "mesh_attribute", MESH_ATTR_NAME)
    return node, st


def _build_branch(graph, src_node, src_out, cfg, sink):
    """One species branch: per-species scaled/leaned Transform -> a fresh variant
    roll attribute -> a split chain into one band per variant -> AddAttribute
    (Mesh=variant) per band -> a single SkinnedMeshSpawner. Returns the spawner
    node so the caller can wire it to Output. `sink` collects (prefix, suffix,
    soft_path, addattr_settings) for the readback."""
    xf_node, _ = _mk_transform(graph, cfg["scale_min"], cfg["scale_max"], cfg["lean_deg"])
    _wire(graph, src_node, src_out, xf_node, _pin(xf_node))

    vn_node, _ = _mk_roll(graph, cfg["seed"], VAR_ATTR_NAME)
    _wire(graph, xf_node, _pout(xf_node), vn_node, _pin(vn_node))

    variants = cfg["variants"]
    n = len(variants)
    cuts = [(i + 1) / float(n) for i in range(n - 1)]   # equal-width bands
    endpoints = _split_chain(graph, vn_node, _pout(vn_node), VAR_ATTR_NAME, cuts)

    sp_node, _ = _mk_spawner(graph)
    for (suffix, _w), (en, eo) in zip(variants, endpoints):
        path = "%s%s%s.%s%s" % (cfg["dir"], cfg["prefix"], suffix, cfg["prefix"], suffix)
        aa_node, aa_set = _mk_add_attr_mesh(graph, path)
        _wire(graph, en, eo, aa_node, _pin(aa_node))
        _wire(graph, aa_node, _pout(aa_node), sp_node, _pin(sp_node))
        sink.append((cfg["prefix"], suffix, path, aa_set))
    return sp_node


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

    # --- 2. shared chain: sample -> hard Trees gate -> graded zone density --
    ls_node, ls_set = _add(graph, unreal.PCGGetLandscapeSettings)
    _set_struct(ls_set, "sampling_properties",
                lambda sp: sp.set_editor_property("get_layer_weights", True))

    ss_node, ss_set = _add(graph, unreal.PCGSurfaceSamplerSettings)
    ss_set.set_editor_property("points_per_squared_meter", DENSITY_PPSM)

    # Hard gate: keep points where Trees weight > TREES_THRESHOLD (fairway,
    # green, tee, bunker = 0). Kept on the "inside" output pin.
    gate_node, gate_set = _add(graph, unreal.PCGAttributeFilteringSettings)
    _set_selector(gate_set, "target_attribute", TREES_LAYER_NAME)
    gate_set.set_editor_property("operator", unreal.PCGAttributeFilterOperator.GREATER)
    gate_set.set_editor_property("use_constant_threshold", True)
    _set_struct(gate_set, "attribute_types", lambda t: (
        t.set_editor_property("type", unreal.PCGMetadataTypes.DOUBLE),
        t.set_editor_property("double_value", float(TREES_THRESHOLD))))

    # Graded density: roll a per-point ZoneRoll, then keep where Trees > ZoneRoll
    # (== ZoneRoll < Trees, expressed with the GREATER op we know resolves). The
    # keep-probability equals the painted Trees weight -> perimeter (~1.0) stays
    # dense, rough (~0.4) thins. If the paint is binary this keeps ~everything.
    zn_node, _ = _mk_roll(graph, SEED_ZONE, ZONE_ATTR_NAME)
    zf_node, zf_set = _add(graph, unreal.PCGAttributeFilteringSettings)
    _set_selector(zf_set, "target_attribute", TREES_LAYER_NAME)
    _set_selector(zf_set, "threshold_attribute", ZONE_ATTR_NAME)
    zf_set.set_editor_property("use_constant_threshold", False)
    zf_set.set_editor_property("operator", unreal.PCGAttributeFilterOperator.GREATER)

    # Species roll: a per-point SppRoll attribute, split below by a filter chain.
    sn_node, _ = _mk_roll(graph, SEED_SPECIES, SPP_ATTR_NAME)

    # --- 3. shared edges -------------------------------------------------
    gate_out = _pout(gate_node, ["Inside", "In Filter", "Out"])
    zf_out   = _pout(zf_node,   ["Inside", "In Filter", "Out"])
    _wire(graph, ls_node, _pout(ls_node, ["Landscape", "Out"]),
          ss_node, _pin(ss_node, ["Surface"]))
    _wire(graph, inp, _pout(inp, ["In", "Out"]),
          ss_node, _pin(ss_node, ["Bounding", "Bounds"]))
    _wire(graph, ss_node, _pout(ss_node), gate_node, _pin(gate_node))
    _wire(graph, gate_node, gate_out, zn_node, _pin(zn_node))
    _wire(graph, zn_node, _pout(zn_node), zf_node, _pin(zf_node))
    _wire(graph, zf_node, zf_out, sn_node, _pin(sn_node))

    # --- 4. species split chain, then one branch per species -------------
    species_items = list(SPECIES.items())
    ns = len(species_items)
    if ns == 2:
        spp_cuts = [SPECIES_SPLIT]                       # <=SPLIT birch, >SPLIT pine
    else:
        spp_cuts = [(i + 1) / float(ns) for i in range(ns - 1)]
    spp_ends = _split_chain(graph, sn_node, _pout(sn_node), SPP_ATTR_NAME, spp_cuts)

    out_in = _pin(outp, ["Out", "In"])
    sink = []
    for (name, cfg), (en, eo) in zip(species_items, spp_ends):
        _log("species %-6s -> %d variants" % (name, len(cfg["variants"])))
        sp_node = _build_branch(graph, en, eo, cfg, sink)
        _wire(graph, sp_node, _pout(sp_node), outp, out_in)

    # --- 5. verify + save ------------------------------------------------
    # expected nodes: 6 shared (landscape, sampler, gate, zone-roll, zone-
    # filter, species-roll) + (ns-1) species split filters + per species
    # (1 transform + 1 variant-roll + (V-1) variant filters + V add-attrs +
    # 1 spawner).
    expect = (6 + (ns - 1)
              + sum(2 + (len(c["variants"]) - 1) + len(c["variants"]) + 1
                    for _, c in species_items))
    final_nodes = list(graph.get_editor_property("nodes"))
    _log("final node count = %d (expect %d)" % (len(final_nodes), expect))
    _log("gate target_attribute = %s | zone target = %s threshold = %s"
         % (gate_set.get_editor_property("target_attribute").export_text(),
            zf_set.get_editor_property("target_attribute").export_text(),
            zf_set.get_editor_property("threshold_attribute").export_text()))
    ok = True
    for prefix, suffix, path, aa_set in sink:
        got = (aa_set.get_editor_property("attribute_types")
                     .get_editor_property("soft_object_path_value").export_text())
        good = bool(got) and (prefix + suffix) in got
        ok = ok and good
        _log("variant %s%s soft path = %s  [%s]"
             % (prefix, suffix, got, "OK" if good else "BAD"))
    _log("all %d variant soft paths resolved = %s" % (len(sink), ok))

    unreal.EditorAssetLibrary.save_asset(GRAPH_PATH)
    _log("DONE: %s saved" % GRAPH_PATH)


main()
