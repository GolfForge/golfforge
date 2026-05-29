# build_range_lighting.py
#
# "Cozy golden-hour" lighting tune for the Practice Range. A UE5 Basic level
# already ships SkyAtmosphere + SkyLight + DirectionalLight +
# ExponentialHeightFog, so this spawns NOTHING -- it just finds those existing
# actors and nudges them: a low sun angle (the SkyAtmosphere then renders the
# warm sky for free), a warm colour temperature, and a touch more fog. Run with
# PracticeRange.umap open:
#
#   exec(compile(open(r"<repo>\engine\scripts"
#     r"\build_range_lighting.py",encoding="utf-8").read(),
#     "build_range_lighting.py","exec"))
#
# Idempotent (re-running just re-applies the same values). Feedback via
# unreal.log("RANGE_LIGHT: ...") -> read with get_log_lines. Tunable knobs below.

import unreal

# ---------------------------------------------------------------- knobs
SUN_PITCH = -9.0      # deg; ~9 deg above the horizon = golden hour
SUN_YAW   = 35.0      # deg; light comes in from front-left of a +X-facing player
SUN_TEMPERATURE = 5000.0   # K; <6500 = warmer (golden), >6500 = cooler
FOG_DENSITY = 0.03    # Basic-level default is 0.02; a nudge for cozy haze


def _log(msg):
    unreal.log("RANGE_LIGHT: " + str(msg))


def _actors():
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    return eas.get_all_level_actors()


def _first(actors, class_name):
    for a in actors:
        if a.get_class().get_name() == class_name:
            return a
    return None


def main():
    _log("=== RANGE LIGHTING (golden-hour tune) ===")
    actors = _actors()

    sun = _first(actors, "DirectionalLight")
    if sun is None:
        _log("WARNING: no DirectionalLight in the level - a Basic level should "
             "have one; skipping sun tune")
    else:
        sun.set_actor_rotation(unreal.Rotator(0.0, SUN_PITCH, SUN_YAW), False)
        dlc = sun.get_component_by_class(unreal.DirectionalLightComponent)
        if dlc is not None:
            try:
                dlc.set_editor_property("use_temperature", True)
                dlc.set_editor_property("temperature", float(SUN_TEMPERATURE))
            except Exception as exc:
                _log("temperature note: %s" % str(exc)[:60])
        _log("sun: pitch=%.1f yaw=%.1f temp=%.0fK" % (SUN_PITCH, SUN_YAW,
                                                      SUN_TEMPERATURE))

    fog = _first(actors, "ExponentialHeightFog")
    if fog is None:
        _log("WARNING: no ExponentialHeightFog in the level; skipping fog")
    else:
        fc = fog.get_component_by_class(unreal.ExponentialHeightFogComponent)
        if fc is not None:
            try:
                fc.set_editor_property("fog_density", float(FOG_DENSITY))
            except Exception as exc:
                _log("fog_density note: %s" % str(exc)[:60])
        _log("fog: density=%.3f" % FOG_DENSITY)

    # Re-capture the SkyLight so ambient matches the new sun/sky.
    sky = _first(actors, "SkyLight")
    if sky is not None:
        slc = sky.get_component_by_class(unreal.SkyLightComponent)
        if slc is not None:
            try:
                slc.recapture_sky()
                _log("SkyLight recaptured")
            except Exception as exc:
                _log("recapture note: %s" % str(exc)[:60])

    # Persist (level actors live in the .umap). Fall back to a reminder if the
    # level-save API isn't bound in this build.
    try:
        les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
        les.save_current_level()
        _log("level saved")
    except Exception as exc:
        _log("could not auto-save level (%s) - USER: save the umap"
             % str(exc)[:60])
    _log("=== DONE ===")


main()
