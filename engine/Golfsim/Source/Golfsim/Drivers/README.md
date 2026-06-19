# Launch-monitor drivers

Connectors that turn a real launch monitor's shot data into our `shot.taken` event on the EventBus.
Each driver lives in our binary and talks to its device/service over a socket or BLE — the sim
only ever sees the normalized `FShotTakenEvent`, never the hardware.

> [OpenFlight](https://github.com/jewbetcha/openflight) is an open-source DIY launch monitor
> (OPS243-A Doppler radar + Raspberry Pi 5) and is the first concrete `ULaunchMonitorDriver`
> implementation. Repo: github.com/jewbetcha/openflight.

## License note — read before touching `OpenFlightDriver`

**OpenFlight and this project are both AGPL-3.0.** Since GolfForge is AGPL-only (no commercial
dual-license — dropped 2026-06-10), vendoring OpenFlight's AGPL source would be perfectly
license-compatible. So this is **no longer a licensing constraint** — it's an architecture one.

We keep OpenFlight as a **separate process we talk to over a socket** because that's the right shape
(invariants #1/#2): OpenFlight runs on the user's Pi or a localhost process and just publishes shots
that our driver normalizes into `FShotTakenEvent`. The sim never sees the hardware, only the
normalized event. Process isolation also keeps a third-party connector's crashes/deps out of our
binary.

Rules:

1. Our driver runs in our binary; OpenFlight runs as a **separate process** (the user's Pi, or
   `localhost` via its `--mock` mode). They communicate only over the socket. Document their protocol
   from their README/docs — a schema is facts, not code.
2. If a future third-party connector is AGPL (or otherwise AGPL-compatible) and it ever makes sense to
   absorb its code, that's now allowed by the license. Default to the separate-process boundary anyway
   for the architecture reasons above; absorb only with a deliberate reason.

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

## GSPro Open Connect driver (`gsproconnect`, GOL-178)

**The single highest-leverage LM driver: one TCP server inherits six launch monitors.** GSPro's "Open
Connect V1" is the de-facto open protocol the community connectors speak. Normally GSPro is the *server*
and an LM connector is the *client*. **GolfForge plays the GSPro/server role** — so any connector that
targets a GSPro server can target us instead, with **no GSPro subscription required** and **no per-LM
driver work from us**:

| Launch monitor | Community connector | Lang |
|---|---|---|
| Rapsodo MLM2PRO | [Duwaynef/MLM2PRO-BT-APP](https://github.com/Duwaynef/MLM2PRO-BT-APP) (MIT) — **validated ✅** (via generic `gsproconnect`; test shot, real-ball pending) | C# |
| FlightScope Mevo+ | [springbok/MLM2PRO-GSPro-Connector](https://github.com/springbok/MLM2PRO-GSPro-Connector) | Python |
| SkyTrak | [OpenSkyPlus2/OpenSkyPlus2](https://github.com/OpenSkyPlus2/OpenSkyPlus2) | C# |
| Garmin Approach R10 | [travislang/gspro-garmin-connect-v2](https://github.com/travislang/gspro-garmin-connect-v2) | — |
| Square Omni / Square Golf | [brentyates/squaregolf-connector](https://github.com/brentyates/squaregolf-connector) (MIT) — **validated ✅** | Go |
| Foresight GC2 | [matthew-johnston/gspro-gc2-connector](https://github.com/matthew-johnston/gspro-gc2-connector) | mixed |

Thanks to these maintainers — GolfForge interoperates with their connectors over the open protocol
(separate processes, not vendored). See the repo-root `README.md` Acknowledgements.

Auth-gated devices whose vendor connectors do a server-side handshake (Garmin R50, Bushnell Launch Pro,
Foresight GCQuad/GC3, possibly Uneekor) are **out of scope** here — they go under the GOL-180 native
-connector epic. Per-connector compatibility is validated under GOL-181 before any public claim.

**Transport: raw TCP, `127.0.0.1:921`, no auth** (verbatim spec: "the socket connection is open and does
not require authentication"). No websockets, so it sidesteps GOL-36 entirely. **Framing varies by
connector** — some newline-delimit their JSON, others concatenate it with no delimiter — so the listener
uses a streaming brace-depth object extractor (`UGSProConnectDriver::ExtractJsonObjects`, string/escape
aware) that handles both. Don't split on `\n`.

- **Dropdown entries + per-connector profiles:** one `UGSProConnectDriver` class backs several selectable
  entries, each registered with an `FGSProConnectProfile` (`SetProfile`) — shipping today: `gsproconnect`
  ("GSPro Connect", generic) + `squaregolf` ("Square Golf", **validated**). The `springbok`
  ("Springbok (MLM2PRO / Mevo+)") entry is **commented out in `LaunchMonitorManager::Initialize`** (hidden
  from the shipping dropdown) until it's validated (GOL-181) — the springbok project looks stale and its
  full release expects the GSPro APIv1 connect-window handshake we don't implement yet; likely needs an
  official path with them first. Its profile + parser quirks stay in the driver, so re-enabling is just
  un-commenting the registration block. **Do not re-add it to the dropdown before then.** For the
  **MLM2PRO** the recommended connector is **[Duwaynef/MLM2PRO-BT-APP](https://github.com/Duwaynef/MLM2PRO-BT-APP)**
  (MIT, direct BLE), which talks plain Open Connect and **works against the generic `gsproconnect` entry**
  (newline-framed, no arm model) — validated live via BT-APP's "send test shot," so it needs **no dedicated
  profile**. (The user supplies BT-APP's Rapsodo "Web Api secret"; nothing proprietary lives in this repo.)
  Adding `skytrak` / `r10` / `gc2` is one profile each. **The open-source connectors do NOT all implement Open
  Connect identically**, so each connector's quirks live in its profile — tuning one cannot break
  another. Only the **active** entry binds the port.

  | profile field | squaregolf | springbok | meaning |
  |---|---|---|---|
  | framing | newline-delimited | concatenated, no delimiter | handled universally by the extractor |
  | `bArmModel` | **true** | false | arm `{Code:201}` on connect + re-arm after club-data (one shot per arm) |
  | `{Code:201}` meaning | "ready"/arm signal | club change only (**`KeyError` without `Player.Club`**) | so we always send `Player.Club` |
  | `BackSpin` key | `BackSpin` | `Backspin` (lowercase s) | parser accepts either |

  (Source for the springbok contract: `springbok/MLM2PRO-GSPro-Connector` — `gspro_connect.py`,
  `ball_data.py`, `gspro_connection.py`. squaregolf: `brentyates/squaregolf-connector` `connection.go`.)
- **Listener:** `Connect()` binds a listening `FSocket` on the configured port and spawns a dedicated
  `FRunnable` (`FGSProConnectListener`) for the blocking accept + recv loop. Parsed shots + status changes
  cross to the game thread over SPSC `TQueue`s, drained by an `FTSTicker` — publishing MUST be on the game
  thread (synchronous EventBus dispatch touches the world). One client at a time, **last-wins** (a new
  connection closes the old). Config: `[LaunchMonitor.GSProConnect] Port` (default 921).
- **Responses:** every message is acked `{"Code":200,"Message":"Shot received"}` (connectors only check
  non-empty == received; the body isn't parsed). **Ack promptly** — springbok blocks on `recv` after each
  shot with a **~2 s socket timeout**; no reply → it times out after 2 retries, closes the socket, and the
  next shot fails (`WinError 10038`, "not a socket"). The player push is
  `{"Code":201,"Message":"GSPro Player Information","Player":{"Handed":"RH","Club":"<code>"}}` — the
  `Message` MUST be the verbatim string `GSPro Player Information` (connectors switch on it; squaregolf
  treats it as the arm signal, a custom string is "Unknown GSPro message type" and never arms), and we
  **always include `Player.Club`** (springbok `KeyError`s without it; default `DR`). `Club` is a GSPro club
  code: `DR`, `W3`, `H4`, `I7`, `PW`/`GW`/`SW`/`LW`, `PT` — mapped from our display name via
  `DisplayClubToGSPro`; handedness defaults RH. (springbok never sends heartbeats — `IsHeartBeat` is always
  false — and needs no periodic server message beyond the per-shot ack.)
- **Arm/fire/re-arm protocol (`bArmModel` profiles only, e.g. `squaregolf`):** the connector fires **one
  shot per arm**, then resets to idle (~2-3s, no "reset done" signal). Per-shot order:
  `LaunchMonitorIsReady:true` (ready) → **ball-data** (the shot) → **club-data** (*end of shot*) → reset.
  So the connect-time `{Code:201}` arms shot #1; on **club-data** we wait the profile's **~3s settle** then
  send **exactly one** re-arm `{Code:201}`. Re-arming on ball-data / immediately / on a timer lands
  mid-reset and **freezes** the loop. **Non-arm connectors (springbok, generic) send shots autonomously
  and read any 201 as a club change — so we never re-arm them.**
- **Status:** `Online` once a client is connected, else `Off` (bound, no client) — the driver→manager
  seam carries a connected bool; surfacing the `LaunchMonitorIsReady` heartbeat as a player-facing
  "ready to fire" state (and the 4-state `Pairing`) is GOL-186.

### Shot schema (GSPro Open Connect V1 message)

`ParseShot` is pure + defensive (heartbeat- and missing-field-tolerant), unit-tested under
`Golfsim.GSProConnect.*`. Fields read from the message:

| field (verbatim) | type | units | → our envelope (SI) | notes |
|---|---|---|---|---|
| `BallData.Speed`     | float | mph | `BallSpeedMps` (× 0.44704) | **required** (mph always; `Units` governs distance only) |
| `BallData.VLA`       | float | deg | `LaunchAngleDeg` (as-is) | vertical launch angle |
| `BallData.HLA`       | float | deg | `AzimuthDeg` (as-is, + = right) | horizontal launch angle |
| `BallData.BackSpin` + `SideSpin` | float | rpm | `BackspinRpm`/`SidespinRpm` (as-is) | **preferred** — measured, connector pre-signs (+ = fade/right) |
| `BallData.TotalSpin` + `SpinAxis` | float | rpm,deg | back/side (decomposed) | fallback when components absent; `+` axis = fade/right |
| `ShotDataOptions.ContainsBallData` | bool | — | gate | only read `BallData` when `true` |
| `ShotDataOptions.ContainsClubData` | bool | — | end-of-shot | triggers the re-arm (see protocol above) |
| `ShotDataOptions.IsHeartBeat`      | bool | — | gate | `true` → acked, not a shot |

**Gate on the `Contains*` flags, never on key presence.** Each message always carries **both**
`BallData` and `ClubData` keys — one real, the other an all-zero filler (a Go `omitempty`/zero-struct
quirk). Ball-data and club-data of one shot share a `ShotNumber` (increments on ball-data, starts at 1).
Spin: the connector reports measured `BackSpin`/`SideSpin` **and** `TotalSpin`/`SpinAxis`, and they need
not be self-consistent (`TotalSpin != hypot(Back,Side)`), so the **measured components are authoritative**
— decompose `TotalSpin·cos/sin(axis)` only when they're absent. **No spin at all** → backspin estimated
`clamp(VLA × 350, 1500, 9000)`, side 0, `bSpinEstimated` set. Only ball speed converts; angles + rpm
stay as-is. Note: a connector's *simulate* mode may emit placeholder metrics (tiny launch/spin) that fly
as worm-burners — that's mock data, not a mapping fault; real-device shots fly normally.

**Validate with no hardware:** `golfsim.LMSelect gsproconnect` (or `squaregolf`) → `golfsim.LMConnect`
(binds 921) → `golfsim.LMSimulate` runs a GSPro-shaped canned shot through the parser with no socket. For
the real wire path, point a connector (or any TCP client) at `127.0.0.1:921`; expect `{"Code":200}` per
message. **Validated live** end-to-end against `brentyates/squaregolf-connector` (simulate mode, incl.
continuous arm→fire→re-arm) and against `Duwaynef/MLM2PRO-BT-APP` (MLM2PRO, "send test shot" → generic
`gsproconnect`; real-ball still pending). The remaining community connectors validate against this same
listener (GOL-181); `log LogTemp Verbose` dumps every raw frame for troubleshooting.
