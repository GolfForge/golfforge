# Open Golf Sim — Project Plan

Working notes. Living doc. Update as decisions firm up.

## Vision & positioning

An open-source, cross-platform golf simulator. Differentiates from GSPro and competitors on three axes that closed-source incumbents structurally cannot match:

1. **Course-pipeline tooling.** The community produces courses faster than GSPro's CourseForge can. AI-assisted import from open LIDAR + OSM data.
2. **Walking/exercise integration.** Treadmill-driven, FTMS-compliant, incline-matched to course elevation. Competes with Zwift and Peloton, not just sim-golf.
3. **Genuinely cross-platform.** PC/Mac/Linux desktop tier with Nanite/Lumen; iPad mobile tier with traditional LODs. BLE/WiFi connect to launch monitors and fitness hardware regardless of host.

## Runtime model: monolithic per-platform binary

Each platform target ships as **one binary** containing the sim, the rendering, and the platform-appropriate hardware drivers. End users run a single executable — no services, no IPC setup, no paired machines.

- Mac build → universal Apple Silicon binary, uses CoreBluetooth for BLE.
- Windows build → uses Windows.Devices.Bluetooth.
- Linux build → uses BlueZ.
- iPad build → iOS app, uses CoreBluetooth.

Multiplayer is the only inter-machine concern, and it's network replication between *peers running the same binary*, not service discovery between heterogeneous services on one player's setup.

Two-machine setups (e.g., dev workstation + streaming client) are a **developer convenience only**. They don't change the user-facing runtime model.

## Core architectural commitment: normalized event bus (in-process)

Inside the single binary, drivers and the sim communicate through a normalized event bus — same typed event envelopes regardless of source. Walking sensor, launch monitor, HR strap, manual UI: they all publish the same shape of event to an in-process pub/sub. The sim subscribes.

This is an internal contract, not a network protocol. The events flow through function calls, not sockets. The shape of events is specified in `docs/event-protocol.md` and stays stable across platforms.

**Why bother with a "protocol" if it's in-process?** Three reasons:
1. New hardware = new driver, no sim changes. Drivers conform to the event shape; the sim only knows the events.
2. Multiplayer replication uses the same event types, serialized for wire transit between peers. Same envelope, different transport.
3. During development, a Python prototype driver on a Mac can speak the same event shape over a localhost socket while a real C++ driver is being written. The contract is identical, only the transport changes.

Adopting this on day one is the single most important architectural decision. Everything else assumes it.

## Four pillars

### 1. Course pipeline

End-to-end flow per course:

1. Pull 3DEP LIDAR point cloud from AWS Entwine for bounding box (PDAL).
2. Ground-classify and rasterize to GeoTIFF DEM at ~50cm/pixel.
3. Convert to 16-bit PNG R16 heightmap at UE5-friendly dimensions (4033×4033 or 8129×8129).
4. Build splatmap from OSM golf polygons + NAIP ortho segmentation (fairway / rough / bunker / green masks).
5. Replace green areas of heightmap with high-res photogrammetry meshes (Metashape pro tier, OpenDroneMap hobbyist tier).
6. Import Landscape into UE5 via flopperam MCP. Paint weight layers from splatmap.
7. Scatter foliage via PCG from OSM tree data.
8. Manual pass for hazards, cart paths, aesthetics.

Steps 1–3 fully scriptable. Step 4 mostly scriptable. Steps 5 + 8 are where humans add value.

### 2. Sim engine

Unreal Engine 5 (5.7+). Chaos physics for ground interaction; custom C++ aerodynamics solver for ball flight (lift/drag/spin decay). ~500 lines of careful C++ plus a lot of tuning data.

**MCP tooling (revised 2026-05-14):**
- Primary: **NAJEMWEHBE/UnrealClaudeMCP** (free, MIT, v0.9.1+) — 96 tools including `inspect_landscape`, `import_texture`, `execute_unreal_python`, `spawn_actor`, viewport screenshots. Covers Milestone 0 / 0.5 and keeps the OSS contribution surface clean.
- Escalation A: flopperam Flop MCP ($15/mo hosted, 50+ tools across 9 domains) — has first-class `landscape_edit` and `pcg_graph_edit`. Trial when NAJEMWEHBE starts feeling thin on foliage / PCG / splatmap painting.
- Escalation B: StraySpark Unreal MCP Server ($89.99 lifetime, 359 tools, full C++ source) — heaviest toolset, Git-LFS-aware source control, closed-loop PIE control for ball-flight tuning. Worth the spend if the project's lifetime is confidently >6 months.
- Fallback: UE5 Python editor scripts directly via `execute_unreal_python`, no MCP-specific tool, for batch work.

### 3. Walking integration

**Protocol of record:** Bluetooth FTMS (Fitness Machine Service, UUID 0x1826). Bidirectional — read for speed/distance/cadence, write to command incline.

**Reference hardware (production / store-bought):** NPE Runn (~$100) — retrofit sensor that mounts to the rail, broadcasts FTMS, works on any treadmill.

**Reference hardware (DIY / validation):** ESP32 (LilyGo TTGO T-Display, ~$15) + TCRT5000 optical sensor + painted/taped marks on belt + 3D-printed bracket. Firmware: lefty01/ESP32_TTGO_FTMS, single-sensor optical mode on GPIO12, marks-per-revolution configured via `treadmill.txt` (distance-per-pulse). No code changes expected for v1.

**Optional but high-value:** BLE chest strap or Apple Watch heart-rate. Adds calorie/HR-zone tracking and the Strava-style post-round summary that makes the fitness feature feel real.

**The "wow" feature:** incline-matching. When the next treadmill is incline-capable, the game commands FTMS incline from the elevation profile of the hole you're walking. This is the Zwift-for-golf moment.

**Interaction model defaults:** stop-and-go (treadmill follows your motion), compressed walk mode by default (~3:1 game-meters-to-pad-meters), strict mode opt-in for fitness diehards, cart mode opt-in for time-constrained play. Fitness stats are a parallel reward layer — they do **not** affect shot accuracy in v1.

### 4. Launch monitor

Square Omni when it arrives, as an in-process BLE driver. Publishes `shot.taken` events to the in-process bus. Shot dialog (manual carry/spin/launch sliders) covers earlier phases as another in-process "driver" of the same shape.

## Cross-platform strategy

| Tier | Targets | Renderer | Notes |
|---|---|---|---|
| Desktop | Win / Mac / Linux | Nanite + Lumen | Hardware Lumen on M3+, RTX, RDNA2+ |
| Mobile | iPad Pro (M2+) | Software Lumen, Nanite optional | M-series Apple Silicon |
| Low | iPad base / Android | Mobile renderer, no Nanite, baked lighting | Defer past v1 |

Author for desktop and mobile tiers from day one. Share textures, branch materials by platform. For multiplayer, peers running the same binary network with each other; one is authoritative for game state. A two-machine setup (e.g., powerful PC running the sim and streaming over Sunshine/Moonlight to a Mac or iPad client in another room) is a deployment choice individual users make, not part of the architecture.

## MVP ladder

| Version | Scope | Proves |
|---|---|---|
| v0 | One course, tap-to-advance, manual shot input | Course pipeline works end-to-end |
| v0.1 | Multiple courses, documented contributor workflow | Pipeline scales |
| v0.2 | FTMS walking integration, HR strap, post-round summary | Differentiator landing |
| v0.3 | Square Omni driver | Real shot input |
| v0.4 | Multiplayer (synchronized walks, voice chat) | Social loop |
| v0.5 | Mobile-tier rendering, iPad target | Cross-platform real |
| v1.0 | Public release | — |

Walking integration deliberately lands before launch monitor — it's a bigger differentiator and doesn't depend on physics being perfect.

## Open decisions

- **License — DECIDED (2026-05-28):** AGPL-3.0 + a paid commercial-exception dual-license (sell closed-source licenses; external contributions under a CLA). See `LICENSE`, `COMMERCIAL.md`, Linear GOL-43. Public name: **GolfForge**.
- **Course IP.** Real course names vs. generic "Coastal Links" style. GSPro plays this carefully. Probably real names with disclaimers and clear takedown process.
- **Walking mode default.** Compressed (~3:1) is recommended default; strict 1:1 opt-in.
- **Multiplayer host model.** PC-authoritative is the cleanest; needs decision on whether iPad-only households can host.

## Settled working decisions

- **First course: GolfForge Demo Black.** 1.4km × 1.4km bbox `-73.4540,40.7423,-73.4374,40.7549` (incidentally picks up adjacent GolfForge Demo State Park courses; fine).
- **Claude Code is the surface for engine work; Cowork stays for repo/docs work.** `.mcp.json.example` at the repo root is the template; each machine copies it to `.mcp.json` (gitignored). Cowork's MCP set is curated and won't load arbitrary MCPs, so drive UE5 from Claude Code (CLI), not Cowork.
- **Third-party MCP plugin is not vendored.** Cloned per-machine to `engine/UnrealClaudeMCP-upstream/` (gitignored), plugin folder copied into `engine/Golfsim/Plugins/UnrealClaudeMCP/` (also gitignored). New contributors clone + copy themselves.

## Immediate next moves

Two de-risking moves before any sim work:

1. **ESP32 + optical sensor build (hardware).** Validates walking integration architecture. Target: Zwift sees the ESP32 as a smart treadmill and accurately reports your walking speed. ~$20 in parts, weekend build.
2. **PDAL pipeline draft (software).** Validates course pipeline architecture. Target: bounding box in → UE5-ready R16 heightmap out. Pick a course with good 3DEP coverage to test against.

Both are independent and can run in parallel. Only after both work does the UE5 project itself become worth starting.

## Useful references

### MCP servers
- flopperam/unreal-engine-mcp — https://github.com/flopperam/unreal-engine-mcp
- NAJEMWEHBE/UnrealClaudeMCP — https://github.com/NAJEMWEHBE/UnrealClaudeMCP
- remiphilippe/mcp-unreal — https://github.com/remiphilippe/mcp-unreal

### LIDAR pipeline
- OpenTopography 3DEP workflows — https://github.com/OpenTopography/OT_3DEP_Workflows
- USGS Entwine on AWS — https://github.com/hobuinc/usgs-lidar
- GeotiffLandscape UE plugin — https://github.com/iwer/GeotiffLandscape
- Cesium for Unreal — https://cesium.com/platform/cesium-ion/

### Walking integration
- lefty01/ESP32_TTGO_FTMS — https://github.com/lefty01/ESP32_TTGO_FTMS
- eborchardt/BluetoothTreadmill — https://github.com/eborchardt/BluetoothTreadmill
- benb0jangles/Smart-Treadmill-Adapter-FTMS-Bluetooth — https://github.com/benb0jangles/Smart-Treadmill-Adapter-FTMS-Bluetooth
- NPE Runn (commercial reference) — https://npe.fit/products/runn
