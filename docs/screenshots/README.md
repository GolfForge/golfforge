# Screenshots

In-game captures for the main README + future store pages (itch, Steam, Epic). Capture from PIE or the cooked binary via UE's console:

```
~  HighResShot 1920x1080
```

(captures land in `Saved/Screenshots/Windows/`.)

## Current set (v0.0.3-alpha)

| File | What it shows | Referenced from |
|---|---|---|
| `mainmenu.png` | Main menu (GolfForge title + Range / Previous Sessions / Play Course / Exit) over the blurred live range. | README "Try it" |
| `playcoursemenu.png` | Pre-round picker — name + course + difficulty + Start Round / Back. | README "Try it" |
| `startcourse.png` | Just-fired tee shot. Yellow tracer arc visible, live range panel on the right showing the resolved shot stats (ball speed / launch / spin / carry / total / offline). Two pins visible in the distance. | _unused — candidate for store pages_ |
| `greenshot.png` | Standing near a green. Gimme ring (cream-yellow disc) around the pin, sand bunker bottom-left, character mid-frame, pin flag visible. The "this is real golf on a real course" hero shot. | README "Try it" |
| `optionsmenu.png` | Settings modal showing the Display section + the new **Main Menu** button (GOL-125) between Close and Quit Game. | _unused — store pages or release-notes_ |

## File size note

PNGs from `HighResShot` at 1080p land at 3-10 MB each. That's heavy for a repo. Options if it bites:

- **Compress before commit** — `tinypng.com` (drag-drop UI, ~70% size reduction with no visible quality loss) or `optipng -o7 *.png`.
- **Convert to JPEG** — lossy but acceptable for screenshots; ~10x smaller. `magick mainmenu.png -quality 85 mainmenu.jpg`.
- **LFS-track** — overkill for a handful of screenshots; revisit only if the set grows past ~50 MB.

For v0.0.3-alpha the raw PNGs are fine. Re-evaluate at v0.0.4+.

## Filename convention going forward

Stable feature names (`mainmenu.png`, `playcoursemenu.png`) rather than version-tagged (`v0.0.3-mainmenu.png`) — the README always references the latest. Replace in place when the UI changes.
