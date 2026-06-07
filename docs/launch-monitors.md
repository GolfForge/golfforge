# Connecting a launch monitor

GolfForge plays the **GSPro server** role: it speaks the open **GSPro Open Connect** protocol that the
community launch-monitor connectors already talk to. So **your existing connector works with GolfForge —
no GSPro subscription required.** There's also a native driver for the DIY **OpenFlight** monitor, and a
manual-shot dialog + keyboard swing for playing with no hardware at all.

You don't need a launch monitor to play — the keyboard swing meter drives the range and a full round. This
guide is for hooking up a real one.

## Quick start (order matters)

1. **Launch GolfForge → Practice Range.**
2. **Pick your launch monitor in GolfForge first.** In the range control panel, open the **Launch Monitor**
   dropdown and select your device/connector (e.g. *Square Golf*, *Springbok (MLM2PRO / Mevo+)*, *GSPro
   Connect*, *OpenFlight*). GolfForge immediately starts **listening on `127.0.0.1:921`**.
   Do this **before** starting the connector — some connectors (notably springbok) only dial once and won't
   retry, so the server has to be up first. Selecting in GolfForge first is the safe order for all of them.
3. **Start your launch monitor's connector app** and point its GSPro/destination at **`127.0.0.1` port
   `921`** (this is the connector's default — most need no change). Power on / pair your monitor per the
   connector's own instructions.
4. The status pill turns **green (Online)** when the connector links up. **Take a shot** — it flies in
   GolfForge.

Only one launch monitor is active at a time (the selected one binds the port). Switch back to *Simulated
(no device)* to free it and return to the keyboard swing.

## Supported launch monitors

GolfForge talks to these as separate processes over the open protocol — their connectors are third-party
open-source projects, not bundled with GolfForge. Pick the matching dropdown entry; install the connector
from its project page.

| Launch monitor | GolfForge dropdown entry | Connector to install | Status |
|---|---|---|---|
| Square Omni / Square Golf | **Square Golf** | [brentyates/squaregolf-connector](https://github.com/brentyates/squaregolf-connector) | ✅ validated |
| Rapsodo MLM2PRO | **Springbok (MLM2PRO / Mevo+)** | [springbok/MLM2PRO-GSPro-Connector](https://github.com/springbok/MLM2PRO-GSPro-Connector) | ✅ validated |
| FlightScope Mevo+ | **Springbok (MLM2PRO / Mevo+)** | [springbok/MLM2PRO-GSPro-Connector](https://github.com/springbok/MLM2PRO-GSPro-Connector) | ✅ validated |
| SkyTrak | **GSPro Connect** (generic) | [OpenSkyPlus2/OpenSkyPlus2](https://github.com/OpenSkyPlus2/OpenSkyPlus2) | ⏳ verifying |
| Garmin Approach R10 | **GSPro Connect** (generic) | [travislang/gspro-garmin-connect-v2](https://github.com/travislang/gspro-garmin-connect-v2) | ⏳ verifying |
| Foresight GC2 | **GSPro Connect** (generic) | [matthew-johnston/gspro-gc2-connector](https://github.com/matthew-johnston/gspro-gc2-connector) | ⏳ verifying |
| OpenFlight (DIY radar) | **OpenFlight** | built-in (talks to the OpenFlight app over a socket) | ✅ validated |

"⏳ verifying" entries should work via the generic **GSPro Connect** option (it's the same protocol); we
just haven't formally signed each one off yet. If you try one, tell us how it goes. Validated devices get
their own named dropdown entry; more are added over time.

**Auth-gated monitors are not supported here.** Some vendor connectors (Garmin R50, Bushnell Launch Pro,
Foresight GCQuad/GC3, possibly Uneekor) do a paid server-side handshake and won't talk to an open server.
Those need a native GolfForge driver (planned separately), not a GSPro connector.

## Troubleshooting

- **No shots showing up / connector says it can't connect.** Make sure you selected the launch monitor in
  GolfForge **before** starting the connector (step 2). If the connector was already running, restart it
  after GolfForge is listening.
- **Status pill never goes green.** Confirm the connector is pointed at `127.0.0.1` port `921`, and that
  nothing else is using that port (e.g. GSPro itself running — close it; only one program can listen on
  921). One launch monitor at a time.
- **Status pill states:** *Simulated (no device)* = keyboard swing; *Off* = selected but not connected;
  *Pairing* = connecting; *Online* = connected, take your shot.
- **Shots fly weirdly short/flat.** If your connector has a *simulator / test* mode, it often sends
  placeholder numbers (very low launch/spin) that fly like worm-burners — that's the mock data, not
  GolfForge. Real shots from the device fly normally.
- **Want it to auto-start?** Advanced: set `[LaunchMonitor] ActiveDriver=<id>` and `bAutoConnect=True` in
  `engine/Golfsim/Config/DefaultGame.ini` (ids: `openflight`, `gsproconnect`, `squaregolf`, `springbok`).

No launch monitor? Use **Simulated (no device)** (keyboard swing meter) or the **manual-shot dialog** (press
`M` in the range) to enter numbers by hand.

---
*Connector internals, the wire protocol, and per-connector quirks (framing, arming, field casing) are
documented for developers in [`engine/Golfsim/Source/Golfsim/Drivers/README.md`](../engine/Golfsim/Source/Golfsim/Drivers/README.md).*
