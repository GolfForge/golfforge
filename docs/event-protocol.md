# Event protocol

The internal contract every hardware driver, every UI element, and every multiplayer peer agrees on. **One event shape, one stream, opaque to the sim — drivers come and go, the sim doesn't change.**

## Where this lives

The sim ships as a single monolithic binary per platform. Inside that binary:
- **Drivers** (walking sensor, launch monitor, HR strap, manual UI) publish typed events to an in-process pub/sub.
- **The sim** subscribes to those events.
- **Multiplayer peers** exchange the same event types over the network using the same envelope.

This document describes the *shape* of events. The transport is whatever the layer needs:

| Layer | Transport |
|---|---|
| In-process (driver → sim, sim → analytics) | direct function calls / in-process pub/sub |
| Multiplayer peer-to-peer | UE5 replication or QUIC, serialized as JSON or MessagePack |
| Dev-only Python prototype driver → UE5 dev build | localhost socket (JSON over WebSocket or UDP) |
| Session log persistence | newline-delimited JSON on disk |

Same envelope shape everywhere. The transport detail is up to the implementer of each layer; the spec is the events.

## Design rules

1. **Append-only.** Events are facts about the past. No mutations, no deletes.
2. **One envelope, many payloads.** Every event has the same outer shape; only the `payload` varies by `type`.
3. **Source-tagged.** Every event identifies which driver published it. Helps with debugging and lets the sim prefer e.g. a real launch monitor over the manual shot dialog when both are connected.
4. **Player-scoped.** Every event carries a `player_id`. Singleplayer uses a fixed UUID; multiplayer lets the sim merge streams from multiple peers.
5. **Monotonic timestamps.** `ts_ms` is Unix milliseconds, source-clock. The sim is responsible for clock-skew correction across peers, not the drivers.

## Envelope

Every event looks like this:

```json
{
  "v": 1,
  "ts_ms": 1747000000000,
  "source": "ftms-walker-7f3a",
  "player_id": "0193a0d7-...",
  "type": "walk.tick",
  "payload": { ... }
}
```

| field | type | notes |
|---|---|---|
| `v` | int | Protocol version. Currently 1. |
| `ts_ms` | int | Unix epoch ms in publisher's clock |
| `source` | string | Stable driver identifier, e.g. `ftms-walker-7f3a`, `square-omni-bridge`, `manual-shot-dialog` |
| `player_id` | string (uuid) | Which player this event is about |
| `type` | string | Dotted namespace, see below |
| `payload` | object | Schema depends on `type` |

## Event types

### `walk.*` — walking / treadmill

#### `walk.session_start`

```json
{ "type": "walk.session_start", "payload": { "device": "esp32-ftms-7f3a", "mode": "compressed_3x" } }
```

`mode` is one of `strict_1x` (1m walked = 1m in game), `compressed_3x` (1m = 3m), `cart` (no walking).

#### `walk.tick`

Emitted at ~10 Hz while walking.

```json
{
  "type": "walk.tick",
  "payload": {
    "speed_mps": 1.42,
    "distance_increment_m": 0.142,
    "cadence_spm": 102,
    "session_distance_m": 248.6,
    "session_elevation_gain_m": 4.2
  }
}
```

#### `walk.session_end`

```json
{ "type": "walk.session_end", "payload": { "total_distance_m": 7421.0, "total_kcal": 412 } }
```

#### `walk.incline_command` — game → device (control direction)

```json
{ "type": "walk.incline_command", "payload": { "target_incline_pct": 4.2, "transition_ms": 1500 } }
```

Sent by the sim when the player crosses an elevation boundary on the course. FTMS-control-capable treadmills act on it; others ignore.

### `shot.*` — golf shots

#### `shot.address`

Player begins setup. Optional — most launch monitors don't emit this.

```json
{ "type": "shot.address", "payload": { "club": "7i" } }
```

#### `shot.taken`

The canonical shot event. All units SI.

```json
{
  "type": "shot.taken",
  "payload": {
    "ball_speed_mps": 64.8,
    "launch_angle_deg": 14.2,
    "azimuth_deg": -2.1,
    "backspin_rpm": 6800,
    "sidespin_rpm": -420,
    "smash_factor": 1.42,
    "club": "7i",
    "lie": "fairway"
  }
}
```

`azimuth_deg` is signed: negative left, positive right, relative to target line.
`sidespin_rpm` follows the same sign convention.
`lie` is one of `tee`, `fairway`, `rough`, `bunker`, `green`, `fringe`, `water`, `cart_path`, `unknown`.

#### `shot.cancel`

Player aborts a shot before contact.

```json
{ "type": "shot.cancel", "payload": { "reason": "practice_swing" } }
```

### `bio.*` — biometrics

#### `bio.hr_tick`

Heart rate sample.

```json
{ "type": "bio.hr_tick", "payload": { "bpm": 142, "hrv_ms": 38 } }
```

#### `bio.kcal_total`

Cumulative calories burned this session.

```json
{ "type": "bio.kcal_total", "payload": { "kcal": 287 } }
```

### `session.*` — game session

#### `session.start` / `session.end`

```json
{ "type": "session.start", "payload": { "course_id": "augusta-national", "tees": "blue", "players": [...] } }
```

#### `session.hole_change`

```json
{ "type": "session.hole_change", "payload": { "hole_num": 7, "par": 4, "yardage": 412 } }
```

#### `session.shot_outcome`

Computed by the sim from `shot.taken` + course collision. Not published by drivers.

```json
{
  "type": "session.shot_outcome",
  "payload": {
    "in_hole": false,
    "distance_to_pin_m": 38.4,
    "final_lie": "green",
    "carry_m": 142.1,
    "total_m": 148.3
  }
}
```

## Transport details (per layer)

### In-process (production path)

Drivers and sim live in the same UE5 binary. Drivers publish events as C++ structs through a single `EventBus` subsystem; subscribers register callbacks by type. No serialization, no network. This is the path the shipped product takes.

### Multiplayer peer-to-peer

Peers exchange events over UE5's networking layer (or a future replacement like QUIC). The envelope serializes as JSON or MessagePack depending on what we benchmark; the shape stays identical to the in-process struct. Sim-internal events (`session.shot_outcome`) are computed locally on each peer from authoritative game state — they're not replicated; only the inputs (`shot.taken`, `walk.tick`) are.

### Dev-only: external prototype drivers

During development, it's useful to write a driver in Python on a different machine than the UE5 dev build, talk to UE5 over a localhost or LAN socket using the JSON form of the envelope, then port the working driver to in-process C++ once it's proven. Suggested dev port: `ws://localhost:7878/events`. This path **does not exist in shipped builds.**

### Persistence

The sim writes every event to a JSON-lines log (`session-YYYY-MM-DD-HHMMSS.jsonl`) for replay, debugging, and the post-round summary. Use this log to regenerate stats; don't maintain parallel state.

## Versioning

- `v` field in every envelope.
- Adding a new event type is backwards-compatible — old subscribers ignore unknown types.
- Adding a field to an existing payload is backwards-compatible — old subscribers ignore unknown fields.
- Renaming or removing a field is a breaking change. Bump `v`.

## Example: a full minute of events

```
{"v":1,"ts_ms":1747000000000,"source":"sim","player_id":"...","type":"session.start","payload":{"course_id":"pebble-beach","tees":"blue","players":[...]}}
{"v":1,"ts_ms":1747000000050,"source":"sim","player_id":"...","type":"session.hole_change","payload":{"hole_num":1,"par":4,"yardage":380}}
{"v":1,"ts_ms":1747000001000,"source":"esp32-ftms-7f3a","player_id":"...","type":"walk.session_start","payload":{"device":"esp32-ftms-7f3a","mode":"compressed_3x"}}
{"v":1,"ts_ms":1747000001100,"source":"esp32-ftms-7f3a","player_id":"...","type":"walk.tick","payload":{"speed_mps":1.40,"distance_increment_m":0.14,"cadence_spm":98,"session_distance_m":0.14,"session_elevation_gain_m":0.0}}
{"v":1,"ts_ms":1747000001200,"source":"esp32-ftms-7f3a","player_id":"...","type":"walk.tick","payload":{"speed_mps":1.42,"distance_increment_m":0.14,"cadence_spm":99,"session_distance_m":0.28,"session_elevation_gain_m":0.0}}
{"v":1,"ts_ms":1747000005000,"source":"hr-strap-polar-h10","player_id":"...","type":"bio.hr_tick","payload":{"bpm":118,"hrv_ms":42}}
{"v":1,"ts_ms":1747000060000,"source":"manual-shot-dialog","player_id":"...","type":"shot.taken","payload":{"ball_speed_mps":64.8,"launch_angle_deg":14.2,"azimuth_deg":-2.1,"backspin_rpm":6800,"sidespin_rpm":-420,"smash_factor":1.42,"club":"7i","lie":"tee"}}
{"v":1,"ts_ms":1747000060150,"source":"sim","player_id":"...","type":"session.shot_outcome","payload":{"in_hole":false,"distance_to_pin_m":92.3,"final_lie":"fairway","carry_m":142.1,"total_m":148.3}}
```

That's the entire surface area of the protocol. Every driver, every UI element, every multiplayer peer, every analytics tool reads and writes these.
