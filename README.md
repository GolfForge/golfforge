# golfsim

Open-source, cross-platform golf simulator with AI-assisted course building, walking/treadmill integration, and a clean BLE-based hardware story for launch monitors.

## Why

GSPro and the rest of the closed-source sim-golf market are good products gated by three structural weaknesses any OSS project can attack:

1. **Course pipeline lock-in.** The community builds the courses; the platform charges for access. Make the pipeline 10x cheaper using open LIDAR + OSM + AI-assisted UE5 import.
2. **No walking integration.** Sim golf sits you on a couch. Wire it to a treadmill via Bluetooth FTMS and you compete with Zwift, not couch-golf.
3. **Closed platforms.** Build on UE5 with proper cross-platform targets (Windows / Mac / Linux desktop, iPad mobile tier) and don't gate hardware behind a single OS.

See `docs/plan.md` for the current full plan.

## Architecture in one paragraph

Each platform target (Windows / Mac / Linux / iPad) ships as a **single monolithic binary** containing the sim, the renderer, and the platform-appropriate hardware drivers (CoreBluetooth on Apple, Windows.Devices.Bluetooth on Windows, BlueZ on Linux). Drivers and sim communicate via an **in-process normalized event bus** — every hardware source publishes events of the same shape, the sim subscribes. Multiplayer is the same event shape over the network between peers running the same binary. See `docs/event-protocol.md`.

## Repo layout

```
.
├── README.md                  # you are here
├── docs/
│   ├── plan.md                # current full project plan, MVP ladder, decisions
│   └── event-protocol.md      # event envelope spec — read before writing any driver
├── pipeline/                  # Python data pipeline (runs on Mac/Linux)
│   ├── README.md
│   ├── build_heightmap.py     # bbox + LIDAR → 16-bit UE5 heightmap
│   ├── build_splatmap.py      # OSM golf features → 4-channel splatmap PNG
│   ├── example.sh
│   └── requirements.txt
├── engine/                    # UE5 project lives here (developed on Windows)
└── courses/                   # processed heightmap/splatmap outputs per course (LFS-tracked)
```

## Where work happens

- **Mac (this machine):** the Python data pipeline, all docs, future BLE driver prototypes (using `bleak`), Mac/iOS UE5 builds (eventually).
- **Windows PC:** primary UE5 development for the Windows/Linux build targets. Pulls from this repo over Git.
- **All platforms at runtime:** one binary per platform. No services, no IPC, no paired machines for the end user.

## Getting started

### Pipeline (Mac/Linux)

```bash
cd pipeline
./setup.sh
source .venv/bin/activate
./example.sh
```

### Engine (Windows)

See [`docs/windows-setup.md`](docs/windows-setup.md) for the full Windows-side setup checklist — prerequisites, clone steps, UE5 project creation, MCP wiring, and Milestone 0.

## License

MIT. See `LICENSE`.
