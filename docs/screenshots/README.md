# Screenshots

Drop screenshots here for the main README to pick up. PNG, 1920×1080 ideal (the README scales them down). Capture from PIE or the cooked binary; UE's high-res screenshot console command works well:

```
~  HighResShot 1920x1080
```

(screenshots land in `Saved/Screenshots/Windows/`.)

## Filenames the README references

| File | What it should show |
|---|---|
| `v0.0.3-picker.png` | Main menu with the **Play Course** button + the pre-round picker overlay on top (course dropdown + difficulty + name field). Either composited side-by-side or two stacked. |
| `v0.0.3-on-course.png` | Mid-round on Black 1 or 2. Live HUD top-left (hole / par / strokes / total / distance to pin), yellow tracer arc from a recent shot, gimme ring visible on the distant green, swing meter at the bottom. |
| `v0.0.3-scorecard.png` | The end-of-round scorecard modal — 18 rows with colored +/- column, "Round complete -- &lt;Name&gt;" header, total footer row, "Back to Menu" button. |

Add more as the project grows — update the README image refs as they land.

## Sizing / file size

GitHub recommends README images stay under ~1 MB each for snappy loads. PNGs from `HighResShot` at 1080p land around 500 KB–2 MB depending on scene complexity; if a shot's heavy, compress via `optipng` or run through `tinypng.com`.

Don't commit screenshots over 5 MB — they bloat the clone. LFS them if you really need 4K hero shots later.
