# Windows machine setup

This is the checklist for getting the Windows PC ready to do UE5 development against the `golfsim` repo. Run through it once per fresh machine.

## Prerequisites

Install these first, in roughly this order. Time estimates assume a fast connection.

### 1. Visual Studio 2022 Community (~30 min, ~30 GB)

UE5.7 cannot build C++ without it. During the installer's workload picker, select:

- **Game development with C++** workload
- **.NET desktop development** workload

Then under **Individual components**, confirm:

- Windows 11 SDK (latest)
- MSVC v143 (latest 64-bit) build tools
- C++ AddressSanitizer

Reboot after install completes.

### 2. Git for Windows + Git LFS (~5 min)

Install [Git for Windows](https://git-scm.com/download/win) with default options (it bundles a usable Bash shell — `Git Bash`).

Then Git LFS:

```bash
git lfs install
```

LFS is non-optional for this repo — `.gitattributes` declares LFS rules for `*.uasset`, `*.umap`, and large PNGs. Cloning without LFS active will silently produce broken pointer files.

### 3. Python 3.11+ (~3 min)

Install from [python.org](https://www.python.org/downloads/) — check **Add Python to PATH** during install. If you'd like uv too (recommended for any Python work): in PowerShell:

```powershell
winget install astral-sh.uv
```

### 4. Node.js LTS (~3 min)

[nodejs.org](https://nodejs.org/) — used by some MCP servers. Default install is fine.

### 5. Windows Terminal + PowerShell 7 (~3 min)

Install from the Microsoft Store. `cmd.exe` is painful for this kind of work; PowerShell 7 in Windows Terminal is much better.

### 6. Epic Games Launcher + UE5.7

You likely already have these. Marketplace plugins (Cesium for Unreal, FlopAI, etc.) install through the launcher.

## Clone the repo

In Git Bash or Windows Terminal:

```bash
cd %USERPROFILE%\code   # or wherever you keep code
git clone git@github.com:pucho/golfsim.git
cd golfsim
git lfs pull            # ensure binary assets are fetched, not pointer-files
```

If you don't have an SSH key wired to GitHub on this machine, either set one up or use the HTTPS URL: `git clone https://github.com/pucho/golfsim.git`.

## Create the UE5 project in `engine/`

The repo expects the UE5 project files to live under `engine/`. From inside the UE5.7 editor:

1. **File → New Project**
2. **Games → Blank** template, **C++** (not Blueprint), **Starter Content: No**, target **Desktop**, **Maximum Quality**
3. Set project location to `<repo>/engine/` and name it `Golfsim` (or whatever — but pick once and stick with it; multiple `.uproject` files in `engine/` would confuse things)
4. UE5 will scaffold the project and trigger a Visual Studio first-build. Let it finish.

Verify by compiling a trivial C++ class from within the editor (`Tools → New C++ Class → Actor`). If that succeeds, the toolchain is wired correctly.

## Wire up an MCP server (choose one)

### Option A: Hosted Flop MCP (paid, ~$15/mo)

The recommended path if you're willing to spend the $15 — has the landscape + PCG + foliage tools that matter for course building.

1. Sign up at [flopperam.com/account](https://flopperam.com/account), get an API key.
2. Install the FlopAI Unreal plugin per [flopperam.com/docs](https://flopperam.com/docs).
3. Wire Claude Desktop by editing `%APPDATA%\Claude\claude_desktop_config.json`:

```json
{
  "mcpServers": {
    "flopperam-unreal": {
      "url": "https://agent.flopperam.com/mcp",
      "headers": { "Authorization": "Bearer YOUR_API_KEY" }
    }
  }
}
```

Restart Claude Desktop. In a new conversation, ask Claude to list its tools — `bp_create`, `landscape_edit`, `pcg_graph_edit` etc. should appear.

### Option B: UnrealClaudeMCP (free, MIT)

If you'd rather not pay yet. Has the viewport screenshot tool and a smaller toolset.

```bash
git clone https://github.com/NAJEMWEHBE/UnrealClaudeMCP.git
```

Follow its README to install the UE5 plugin into `engine/Plugins/` and wire Claude Desktop's `mcpServers` block to point at the local Python script.

Either way: the success milestone is "Claude can spawn a cube in your running UE5 editor from a chat message." Pause there before moving on.

## First milestone (Milestone 0)

Once everything is set up, the first concrete deliverable is to import the Pebble Beach test heightmap and verify a ball rolls on it. From the repo:

1. `courses/pebble-beach-test/heightmap.png` is the 1009×1009 16-bit heightmap.
2. `courses/pebble-beach-test/heightmap.json` tells you the elevation range (135.81m) and the suggested UE5 Z scale (26.53%).
3. In UE5: `Landscape Mode → Manage → New → Import from File`, point at the PNG.
4. Set `RelativeScale3D` to (100, 100, 26.53) on the resulting Landscape actor.
5. Drop a default Material on it (anything green-ish).
6. Spawn a sphere with Chaos Physics, drop from height, watch it roll on real Pebble Beach topography.

That's Milestone 0 done. The remainder of the plan is in `docs/plan.md`.

## Pitfalls to know about

- **Don't let Git LFS fall asleep.** If your repo ever feels "small" after a clone (no PNGs visible in `courses/`, or `engine/` has weird tiny `.uasset` files), you're seeing pointer files. Run `git lfs pull`.
- **Windows path length limits.** UE5 projects under deeply nested folders sometimes hit the 260-char path limit. Keep the repo near the drive root (e.g. `C:\code\golfsim`).
- **Antivirus on `engine/Intermediate`.** Defender scanning build output dramatically slows iteration. Add the `engine/` folder to Defender exclusions.
- **First C++ build is slow.** UE5's first compile after a clean clone can take 15–30 minutes. Don't panic.
