"""Import the CC-BY cup-drop sound -> SW_CupDrop (GOL-203 hole-out feedback).

Source: Freesound "Drop ball in cup-1.wav" by AGFX (freesound.org/people/AGFX/sounds/20428/),
CC-BY 4.0 -- attribution required (entry in ATTRIBUTION.md), commercial use allowed, so it's
AGPL-repo-committable per the license policy (copyleft/CC-BY in, NC out). The committed file is
Freesound's public HQ preview encode (128 kbps MP3 of the 48 kHz/24-bit original, ~2.2 s) -- more
than enough fidelity for a one-shot ball-rattles-into-cup SFX. UE5 imports MP3 natively since 5.0.

Played one-shot at the cup by AGolfRangeHUD::OnPuttingShotSettled as the ball starts its GOL-203
sink animation (UGameplayStatics::PlaySoundAtLocation; lazy-loaded like SW_BallStrike).

Run in the UE5.7 editor Python interpreter via execute_unreal_python:
    exec(compile(open(r"<repo>\\engine\\scripts\\import_cup_drop_sfx.py",
        encoding="utf-8").read(), "import_cup_drop_sfx.py", "exec"))

bridge note: execute_unreal_python returns output:None. Feedback via unreal.log() under LogPython.
"""
import os
import unreal

DIR = "/Game/Audio/CupDrop"
NAME = "SW_CupDrop"
PATH = f"{DIR}/{NAME}"
SRC_FILE = "golf_ball_cup_20428_AGFX_ccby.mp3"

eal = unreal.EditorAssetLibrary
at = unreal.AssetToolsHelpers.get_asset_tools()


def _log(m):
    unreal.log("CUPDROP_SFX: " + str(m))


def main():
    proj = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
    src = os.path.join(proj, "Content", "Audio", "CupDrop", "_src", SRC_FILE)
    if not os.path.isfile(src):
        _log("FATAL: missing source " + src)
        return

    task = unreal.AssetImportTask()
    task.set_editor_property("filename", src)
    task.set_editor_property("destination_path", DIR)
    task.set_editor_property("destination_name", NAME)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", True)
    at.import_asset_tasks([task])

    sw = eal.load_asset(PATH)
    if sw is None:
        _log("FATAL: import failed for " + src)
        return
    try:
        sw.set_editor_property("looping", False)
    except Exception as exc:
        _log("looping set note: " + str(exc)[:60])
    eal.save_asset(PATH)

    dur = "?"
    try:
        dur = sw.get_editor_property("duration")
    except Exception:
        pass
    _log("imported %s (duration=%s s, looping=False)" % (PATH, dur))


main()
