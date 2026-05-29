# Launch-monitor drivers

Connectors that turn a real launch monitor's shot data into our `shot.taken` event on the EventBus.
Each driver lives in our binary and talks to its device/service over a socket or BLE — the sim
only ever sees the normalized `FShotTakenEvent`, never the hardware.

## ⚠️ License note — read before touching `OpenFlightDriver`

**OpenFlight and this project are both AGPL-3.0**, so there is no MIT-vs-AGPL "firewall" to police
here. But the separate-process socket boundary still matters, for a different reason:

**This project is dual-licensed — AGPL-3.0 plus a paid commercial exception (`COMMERCIAL.md`).** To
sell a commercial (closed-source) license we must own or hold permissive rights to *everything in
our binary*. If we vendor or copy OpenFlight's AGPL source into our process, the binary then
contains third-party copyleft we can't relicense — which would break the commercial-exception
model. So we keep OpenFlight as a **separate process we talk to over a socket**, never code we
absorb. (This also lines up with architecture invariants #1/#2: OpenFlight runs on the user's Pi or
a localhost process and just publishes shots that our driver normalizes into `FShotTakenEvent`.)

Rules:

1. Do **not** vendor or copy OpenFlight source into our driver (it would taint our ability to offer
   commercial licenses). Document their WebSocket protocol from their README/docs only — a schema is
   facts, not code.
2. Our driver runs in our binary; OpenFlight runs as a **separate process** (the user's Pi, or
   `localhost` via its `--mock` mode). They communicate only over the socket.

The same separate-process boundary applies to any future third-party-protocol driver: talk to it,
don't absorb it.

## Framework

- **`ULaunchMonitorDriver`** (abstract `UObject`) — the connector contract: `GetDriverId()`,
  `GetDisplayName()`, `Connect()`, `Disconnect()`, `IsConnected()`, `InjectTestMessage()`, an
  `OnStatusChanged` callback, and a protected `PublishShot()` / `SetStatus()`.
- **`ULaunchMonitorManager`** (`UGameInstanceSubsystem`) — owns the drivers, tracks the **active** one
  (one at a time), exposes `GetAvailableDrivers()` / `SetActiveDriver()` / `Connect/DisconnectActive`,
  provides EventBus access (`PublishShot`), and forwards the active driver's status via
  `OnActiveStatusChanged`. This is the API a future settings UI drives.
- Config in `Config/DefaultGame.ini`: `[LaunchMonitor] ActiveDriver` + `bAutoConnect`; per-driver
  sections like `[OpenFlight] Url`.

**Adding a driver (e.g. Square Omni):** subclass `ULaunchMonitorDriver`, implement the contract,
normalize the device's shot into `FShotTakenEvent` (SI: ball speed m/s, angles degrees, spin rpm),
call `PublishShot`, and add one `RegisterDriver(NewObject<UYourDriver>(this))` line in
`ULaunchMonitorManager::Initialize`. The settings UI and console commands pick it up automatically.

Console: `golfsim.LMSelect [id]` (list/select), `golfsim.LMConnect` / `golfsim.LMDisconnect`,
`golfsim.LMSimulate [nospin | <json>]` (feed a payload through the active driver with no socket),
`golfsim.LMTrigger` (ask the connected device to emit a shot -- OpenFlight mock mode).

## OpenFlight driver

**Transport: Socket.IO (Engine.IO v4) over Flask-SocketIO — not a raw WebSocket.** The driver speaks
a minimal Socket.IO/EIO4 layer on top of UE's `IWebSocket`:

- Connects to `ws://<host>:<port>/socket.io/?EIO=4&transport=websocket` (host/port from `[OpenFlight]`;
  the scheme + `//` live in code because UE's INI parser truncates a `ws://...` value at `//`).
- Handshake: server sends Engine.IO open `0{...}` → driver replies Socket.IO connect `40` → server
  acks `40` → status goes green.
- Heartbeat: server `2` (ping) → driver `3` (pong).
- Shot events arrive framed as `42["shot",{ "shot": <fields>, "stats": <stats> }]`. The driver
  strips the frame, confirms the event is `shot`, and passes the payload to `ParseShot`.
- `golfsim.LMTrigger` sends `42["simulate_shot"]` to make the mock emit a shot (real round-trip).

Server→client events other than `shot` (`session_state`, `trigger_status`, `club_changed`, …) are
ignored. Reconnects with exponential backoff on drop.

### Shot schema (`shot` event payload)

`ParseShot` is defensive: it accepts the `{shot,stats}` wrapper (or `data`/`payload`, or a bare shot
object) and tolerates missing optional fields. Fields read from the inner shot object:

| field (verbatim)          | type  | units | -> our envelope (SI)                         | notes |
|---------------------------|-------|-------|----------------------------------------------|-------|
| `ball_speed_mph`          | float | mph   | `BallSpeedMps` (× 0.44704)                   | **required** |
| `launch_angle_vertical`   | float | deg   | `LaunchAngleDeg` (as-is)                     | optional |
| `launch_angle_horizontal` | float | deg   | `AzimuthDeg` (as-is, L/R of target)          | optional |
| `spin_rpm`                | float | rpm   | total spin (decomposed below)               | optional, ~50-60% detection |
| `spin_axis_deg`           | float | deg   | tilt: + = fade/right, - = draw/left          | 0 = pure backspin |
| `club`                    | str   | —     | `Club` (e.g. "driver", "7-iron")             | "unknown"/empty dropped |
| `smash_factor`            | float | —     | `SmashFactor` (provenance)                   | optional |
| `estimated_carry_yards`   | float | yd    | (unused this pass)                           | future: trace mode |

The envelope keeps **degrees + rpm** (mirrors `FShotInput`); only ball speed converts. Total spin is
split about its axis: `BackspinRpm = spin_rpm·cos(axis)`, `SidespinRpm = spin_rpm·sin(axis)`.

**Missing spin** (~40-50% of shots): backspin is estimated from launch angle
(`clamp(launch_angle_vertical × 350, 1500, 9000)` rpm), `SidespinRpm = 0`, and `bSpinEstimated` is set
so the range panel marks it `est` (computed, not measured). Tunable first cut.

The driver logs every raw frame at `Verbose` — enable `LogTemp Verbose` to inspect the real Socket.IO
traffic on the first `--mock` run.

### Running OpenFlight without hardware (`--mock`)

OpenFlight ships a mock mode that emits shots without the radar (separate process, e.g.
`scripts/start-kiosk.sh --mock`; see their docs). Then in our sim: `golfsim.LMConnect` (or set
`[LaunchMonitor] bAutoConnect=True`) → the panel's status dot turns green → `golfsim.LMTrigger`
(or click "simulate" in OpenFlight's kiosk, which broadcasts to all clients) to fly a shot.

For an in-engine check with **no** server at all, use `golfsim.LMSimulate` (and `LMSimulate nospin`
for the estimated-spin path) — it runs a canned payload straight through `ParseShot` → bus → panel.
