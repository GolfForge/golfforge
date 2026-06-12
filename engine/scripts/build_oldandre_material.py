# build_oldandre_material.py
#
# GOL-205: a LINKS material profile for OldAndre (St Andrews Old Course), branched
# off the shared course material WITHOUT touching the demo. It authors a second
# Material Instance -- MIC_GolfsimLinks -- parented to the SAME base material
# /Game/Materials/M_GolfsimCourse (built by build_course_material.py), overrides
# the per-layer tint/roughness/stripe params toward a drier, sandier, firmer links
# look, and assigns it to OldAndre's Landscape only. The demo tee maps keep
# MIC_GolfsimCourse; one base shader, two instances (the project's param+MIC
# convention -- cf. build_range_treescatter.py authoring a sibling without
# disturbing the canonical asset).
#
# WHY a 2nd MIC, not a base duplicate: links vs parkland differ in COLOUR/finish,
# not shader structure, so tint/roughness/stripe-contrast overrides on an instance
# get the look with zero shader divergence and full live-tuning. If OldAndre ever
# needs genuinely different turf TEXTURES (real fescue/marram/sand), that escalates
# to either base-material TextureObject params or a duplicated base -- out of scope
# here.
#
# Idempotent: find-or-create the MIC, (re)apply overrides, (re)assign to the
# landscape. Does NOT save the umap (the landscape-material assignment is a umap
# change -- the OPERATOR saves OldAndre.umap); the MIC asset itself IS saved.
#
#   Run in the UE5.7 editor Python interpreter via execute_unreal_python, with
#   OldAndre.umap open (so the landscape reassignment targets the right level):
#     exec(compile(open(r"<repo>\engine\scripts\build_oldandre_material.py",
#       encoding="utf-8").read(),"build_oldandre_material.py","exec"))
#
# bridge note: execute_unreal_python returns output:None. Feedback via
# unreal.log() under LogPython; read with get_log_lines. create_asset is blocked
# during PIE/standalone -- exit all play sessions first.

import unreal

BASE_PATH = "/Game/Materials/M_GolfsimCourse"   # shared base shader (do NOT modify)
MIC_PATH  = "/Game/Materials/MIC_GolfsimLinks"   # OldAndre's links instance (this file owns it)
LANDSCAPE_LABEL_HINT = "Landscape"

# Links overrides vs the demo's parkland tuning (probed baselines in comments).
# Tints are the same HUE-REPLACE / mul VectorParameters the base exposes; only the
# params listed here diverge -- everything else inherits the base defaults. These
# are STARTING values; tune live on MIC_GolfsimLinks in the Details panel.
VEC = {
    # drier, yellower fescue fairway (demo 0.10,0.26,0.08 = lush green)
    "Fairway_Tint": (0.16, 0.22, 0.09),
    # firm fine-fescue green, a touch paler/less blue (demo 0.06,0.20,0.11)
    "Green_Tint":   (0.09, 0.19, 0.10),
    # sun-bleached sandy links rough (demo 0.17,0.16,0.08)
    "Rough_Tint":   (0.22, 0.19, 0.10),
    # warmer golden pot-bunker sand -- mul tint, warm not cool (demo 1.00,1.03,1.12)
    "Bunker_Tint":  (1.08, 1.04, 0.92),
    # tee worn drier toward the fairway (demo 0.16,0.20,0.09)
    "Tee_Tint":     (0.18, 0.20, 0.10),
}
SCA = {
    # firmer/drier greens -- less wet sheen than the demo (demo 0.65)
    "Green_Rough":            0.85,
    # subtler links mow: knock back the parkland stripe contrast
    "Fairway_StripeContrast": 0.05,   # demo 0.15
    "Green_StripeContrast":   0.05,   # demo 0.10
}


def _log(m):
    unreal.log("OLDANDRE_MAT: " + str(m))


def _ensure_mic(base):
    """Find-or-create MIC_GolfsimLinks as an instance of `base`."""
    mic = unreal.load_asset(MIC_PATH)
    if mic is not None:
        if mic.get_editor_property("parent") != base:
            mic.set_editor_property("parent", base)
            _log("re-parented existing MIC to %s" % BASE_PATH)
        return mic
    name = MIC_PATH.rsplit("/", 1)[1]
    pkg  = MIC_PATH.rsplit("/", 1)[0]
    mic = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        name, pkg, unreal.MaterialInstanceConstant,
        unreal.MaterialInstanceConstantFactoryNew())
    if mic is None:
        _log("FATAL: create_asset returned None (in a PIE/standalone session?)")
        return None
    mic.set_editor_property("parent", base)
    _log("created %s parented to %s" % (MIC_PATH, BASE_PATH))
    return mic


def _apply_overrides(mic):
    mel = unreal.MaterialEditingLibrary
    for name, (r, g, b) in VEC.items():
        mel.set_material_instance_vector_parameter_value(
            mic, unreal.Name(name), unreal.LinearColor(r, g, b, 1.0))
        _log("VEC %-16s = (%.3f, %.3f, %.3f)" % (name, r, g, b))
    for name, v in SCA.items():
        mel.set_material_instance_scalar_parameter_value(mic, unreal.Name(name), float(v))
        _log("SCA %-16s = %.3f" % (name, v))
    mel.update_material_instance(mic)


def _assign_to_landscape(mic):
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    ls = None
    for a in eas.get_all_level_actors():
        if a.get_class().get_name() == "Landscape" or (
                LANDSCAPE_LABEL_HINT in a.get_actor_label()
                and "Landscape" in a.get_class().get_name()):
            ls = a
            break
    if ls is None:
        _log("WARNING: no Landscape actor in the open level -- MIC built but not "
             "assigned. Open OldAndre.umap and re-run, or assign by hand.")
        return False
    ls.set_editor_property("landscape_material", mic)
    _log("assigned %s to landscape '%s'" % (MIC_PATH, ls.get_actor_label()))
    return True


def main():
    _log("=== LINKS MATERIAL (OldAndre) START ===")
    base = unreal.load_asset(BASE_PATH)
    if base is None:
        _log("FATAL: base material %s not found (run build_course_material.py first)" % BASE_PATH)
        return
    mic = _ensure_mic(base)
    if mic is None:
        return
    _apply_overrides(mic)
    unreal.EditorAssetLibrary.save_asset(MIC_PATH)
    _log("saved %s" % MIC_PATH)
    assigned = _assign_to_landscape(mic)
    _log("=== DONE (umap NOT saved%s) ==="
         % ("; SAVE OldAndre.umap to persist the landscape-material assignment"
            if assigned else ""))


main()
