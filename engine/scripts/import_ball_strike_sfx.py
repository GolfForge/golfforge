"""Trim + import the CC0 ball-strike sound -> SW_BallStrike (ball+sound task).

Source: BigSoundBank "Golf Swing with a Wooden Club" (s0455), CC0 / public-domain -- same licensing
model as the GOL-166 ambient layer (committable to the AGPL repo + redistributable in builds; courtesy
credit in ATTRIBUTION.md). The raw take is a ~10.5s swing recording, far too long for a per-shot SFX, so
this trims it to a ~0.55s clip around the loudest transient (the impact "crack") and imports THAT as a
NON-looping USoundWave at /Game/Audio/BallStrike/SW_BallStrike. Played one-shot at the strike moment by
AGolfRangeHUD::OnShotOutcome (UGameplayStatics::PlaySoundAtLocation).

Trimming is pure stdlib (aifc reads the big-endian AIFF, audioop finds the peak + byteswaps to LE, wave
writes the short clip) -- no external tools. Source files live in
engine/Golfsim/Content/Audio/BallStrike/_src/ (AIFF download; the trimmed .wav is written there too).

Run in the UE5.7 editor Python interpreter via execute_unreal_python:
    exec(compile(open(r"<repo>\\engine\\scripts\\import_ball_strike_sfx.py",
        encoding="utf-8").read(), "import_ball_strike_sfx.py", "exec"))

bridge note: execute_unreal_python returns output:None. Feedback via unreal.log() under LogPython.
"""
import os
import unreal

DIR = "/Game/Audio/BallStrike"
NAME = "SW_BallStrike"
PATH = f"{DIR}/{NAME}"

PRE_S = 0.06     # seconds of lead-in kept before the transient (catch the attack)
TOTAL_S = 0.55   # total trimmed clip length

eal = unreal.EditorAssetLibrary
at = unreal.AssetToolsHelpers.get_asset_tools()


def _log(m):
    unreal.log("BALLSTRIKE_SFX: " + str(m))


def _make_trimmed_wav(aiff_path, wav_path):
    """Read the AIFF, find the loudest ~20ms window (the impact), write a ~TOTAL_S WAV around it."""
    import aifc
    import audioop
    import wave

    af = aifc.open(aiff_path, "rb")
    nch, sw, fr, nf = af.getnchannels(), af.getsampwidth(), af.getframerate(), af.getnframes()
    be = af.readframes(nf)
    af.close()
    le = audioop.byteswap(be, sw)          # AIFF is big-endian PCM -> little-endian for WAV
    bpf = nch * sw                          # bytes per frame

    win = max(1, int(fr * 0.02)) * bpf      # 20 ms window (frame-aligned)
    best_off, best_rms = 0, -1
    pos = 0
    while pos + win <= len(le):
        rms = audioop.rms(le[pos:pos + win], sw)
        if rms > best_rms:
            best_rms, best_off = rms, pos
        pos += win
    start = max(0, best_off - int(PRE_S * fr) * bpf)
    end = min(len(le), start + int(TOTAL_S * fr) * bpf)
    clip = le[start:end]

    w = wave.open(wav_path, "wb")
    w.setnchannels(nch)
    w.setsampwidth(sw)
    w.setframerate(fr)
    w.writeframes(clip)
    w.close()
    return len(clip) / float(bpf) / fr


def main():
    proj = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
    src_dir = os.path.join(proj, "Content", "Audio", "BallStrike", "_src")
    trimmed = os.path.join(src_dir, "golf_strike_s0455_trim.wav")
    aiff = os.path.join(src_dir, "golf_swing_s0455.aiff")

    if not os.path.isfile(trimmed):
        if os.path.isfile(aiff):
            dur = _make_trimmed_wav(aiff, trimmed)
            _log("trimmed %s -> %s (%.2fs)" % (os.path.basename(aiff), os.path.basename(trimmed), dur))
        else:
            _log("FATAL: no trimmed wav and no AIFF in " + src_dir)
            return

    task = unreal.AssetImportTask()
    task.set_editor_property("filename", trimmed)
    task.set_editor_property("destination_path", DIR)
    task.set_editor_property("destination_name", NAME)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", True)
    at.import_asset_tasks([task])

    sw_asset = eal.load_asset(PATH)
    if sw_asset is None:
        _log("FATAL: import failed for " + trimmed)
        return
    try:
        sw_asset.set_editor_property("looping", False)
    except Exception as exc:
        _log("looping set note: " + str(exc)[:60])
    eal.save_asset(PATH)

    dur = "?"
    try:
        dur = sw_asset.get_editor_property("duration")
    except Exception:
        pass
    _log("imported %s (duration=%s s, looping=False)" % (PATH, dur))


main()
