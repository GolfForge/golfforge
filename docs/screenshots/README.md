# Screenshots

In-game captures for the main README + future store pages (itch, Steam, Epic). Capture from PIE or the cooked binary via UE's console:

```
~  HighResShot 1920x1080
```

(captures land in `Saved/Screenshots/Windows/`.)

## Current set (v0.0.4-alpha)

Captured at 4K (3840x2160) from the packaged build. Flat-UI screens stay PNG (sharp text, compress
tiny); photographic course/range scenes are JPEG q85 (~10x smaller than PNG at this resolution).

| File | What it shows | Referenced from |
|---|---|---|
| `mainmenu.png` | Themed main-menu bento — Play Course / Range / Practice / Settings, brand mark, live clock + conditions cluster, Lucide icons throughout. | README "Try it" |
| `roundsetup.png` | Round-setup wizard step 1 — the course grid (links / parkland / desert / heathland / resort) with the selected card + stepper. | README "Try it" |
| `coursehud.jpg` | Mid-round on the course — glass HUD (hole panel + conditions strip), hole map, launch-monitor readout, swing meter, shot tracer. | README "Try it" |
| `format.png` | Round-setup step 2 — Format: game-type + turn-order option cards with their Lucide icons, holes + hole-out segmented controls. | _unused — store pages / release notes_ |
| `range.jpg` | Practice range — launch-monitor panel, swing meter, control bar over the tree-lined range. | _unused — store pages / release notes_ |

## File size note

PNGs from `HighResShot` at 1080p land at 3-10 MB each. That's heavy for a repo. Options if it bites:

- **Compress before commit** — `tinypng.com` (drag-drop UI, ~70% size reduction with no visible quality loss) or `optipng -o7 *.png`.
- **Convert to JPEG** — lossy but acceptable for screenshots; ~10x smaller. `magick mainmenu.png -quality 85 mainmenu.jpg`.
- **LFS-track** — overkill for a handful of screenshots; revisit only if the set grows past ~50 MB.

As of v0.0.4-alpha: flat-UI screens stay PNG (they compress to <500 KB even at 4K); photographic
course/range scenes are saved as **JPEG q85** (~400 KB–1 MB vs 7–11 MB as PNG). No external tool
needed — `System.Drawing` via PowerShell re-encodes them.

## Filename convention going forward

Stable feature names (`mainmenu.png`, `playcoursemenu.png`) rather than version-tagged (`v0.0.3-mainmenu.png`) — the README always references the latest. Replace in place when the UI changes.
