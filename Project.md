This project is an Unreal 5.6.1 RTS game Simulator.

## Modules

**RTSEngine** — pure C++ sim core (STL / data-only OOP). No Unreal gameplay types in sim sources.
Deterministic tick engine: each step applies pending orders, advances movement, then increments the tick. No reverse/undo history.

**RTSComms** — pure C++ matchmaking client (STL + POSIX/WinSock TCP). No Unreal gameplay types. Talks HTTP to `RTSServer`. Public API is non-blocking: enqueue Login/GetRooms/CreateRoom/JoinRoom/LeaveRoom, pump completions with `TryPop`. One I/O thread may block on HTTP; SimRTS must not.

**SimRTS** — Unreal game module. Display (unit actors), input (selection, orders), HUD, level JSON I/O, and bridge into RTSEngine.
**Block-free:** SimRTS (game thread) must never wait on network, sockets, or RTSComms I/O. It only enqueues work and pumps `TryPop` / sim ticks. A hitch from a blocking `Login` or `recv` on the game thread is a bug.

**SimRTSEditor** — Editor-only module. Tools menu → **SimRTS Level Bake...**: bake placed obstruction actors (`ASimRTSObstructionVolume` box, `ASimRTSObstructionCylinderVolume` circle) into the level `obstruction` string, and editor-only spawn markers (`ASimRTSSpawnMarker`) into the spawns JSON.

## Level configuration (JSON)

Level rules and starting state live **outside compiled code** in JSON under:

`SimRTS/Content/Data/Levels/DefaultLevel.json`

Document shape:

- `world` — discrete `width` / `height`, and required `ticks_per_second` (from JSON → engine static data). After Kickoff, Unreal saves wall time `T0` and the pacer waits until `T0 + N / ticks_per_second` (or `pacer_min_delay_ms` when behind) before the next tick attempt.
- `obstruction` — single `'0'`/`'1'` string of length `width * height` (no separators). Index `i` → `x = i % width`, `y = i / width`. `'0'` walkable, `'1'` blocked. Loaded once into a static 2D `PathingGrid` of `GridCell`; the string is not retained.
- `unit_defs` — `Soldier` / `Vehicle`: `speed` (discrete **points per second**), `radius`
- `spawns` — starting units (`id`, `type`, `x`, `y`, optional `rotation`)

Flow:

1. **SimRTS** (`LevelLoader` + Unreal `Json` module) reads/parses the file into an RTSEngine `Level`
2. `FSimBridge` passes that `Level` into `TickEngine::LoadLevel`
3. RTSEngine stays free of JSON — it only consumes the structured `Level`

Editing the JSON changes the level without recompiling C++ (restart Play / reload level). There is no code-built fallback level — a missing or invalid JSON fails level load. `Content/Data` is staged for packaged builds via `DefaultGame.ini`.

## Networking configuration (JSON)

Matchmaking host and lockstep delay live in:

`SimRTS/Content/Data/Networking.json`

- `ip` / `port` — RTSServer HTTP address
- `udp_port` — RTSServer UDP relay (Hello / order bounce / ping / kickoff)
- `future_tick_distance` — ticks from the originator's Actual Tick until the command activates in the engine (e.g. 5 ≈ 167 ms at 30 tps). Same value on every client.
- `ping_interval_ms` — how often the client sends a UDP ping to the host after Join
- `ping_keep_amount` — HUD RTT is the minimum of this many recent samples
- `pacer_min_delay_ms` — shortest wait between tick attempts during zoom catch-up (e.g. 10)

Play always goes through a relay room (localhost server + your own room is fine). After Join, Start means **ready**. A successful `StartRoom` snapshots that room's seated `player_ids` as the match roster (hashed to `sim_player_id`). When every seated player has started, the server sends a UDP Kickoff; each client waits `remaining_ms - RTT/2` then arms the sim pacer (`T0`). A click is sent immediately with the originator's **Actual Tick** (wall lattice from `T0`, not sim `GetTick()`). Idle Actual Ticks still emit an empty Order (no unit ids). Empties reuse the last click `order_id` (0 before any click); a consecutive click (`order_id = last + 1`) or a later empty with that same id fills missing Actual Ticks since that click. Every client schedules activation at `actual_tick + future_tick_distance` and pathfinds only then, sorted by player id then order id. `StepForward` waits until every seated player has a command (empty or click) for `actual_tick = GetTick() - future_tick_distance`; empties keep going out on Actual Tick while the sim is locked. UDP orders also carry a `(hash_tick, state_hash)` pair of committed `BattleState` after `StepForward` (not Actual Tick, not yaw, not the scheduled-order cache). Peers compare hashes for the same sim tick to detect desync. A missing or invalid Networking.json logs an error and does not start comms.

## Simulation model

World uses discrete integer points (e.g. 1000×1000). With `GridScale = 10` UU (1 dm per point), **speed 10 ≈ 1 m/s**.

### Movement (straight-line segments)

A Move order stores an active **`UnitMove` on the unit**: start `A`, end `B`, `start_tick`.

Each tick:

1. Elapsed seconds = `(tick - start_tick) / ticks_per_second`
2. Points traveled = `speed * elapsed_seconds` along the Euclidean segment `A → B`
3. Float position along that segment is **snapped** to the nearest integer grid point
4. On arrival (`points_traveled >= |B-A|`), position becomes `B` and the move clears

On submit, **`FindMoveWaypoints`** plans the route: if line-of-sight is clear, one segment; otherwise 8-neighbor **A\*** around blocked cells, then **LOS-safe compression** into fewer Euclidean waypoints (each skipped shortcut must pass `HasLineOfSight` / clamp). Unreachable goals fall back to the closest reachable free cell. Those waypoints are enqueued as move / `is_next` segments.

Before each segment starts, **`ClampMoveDestination`** still walks the lerp+round cells with a fixed 0.5-point step (speed-independent) and shortens the end if needed.

Each unit runs at most one active move. Pending orders in `BattleState.orders` are scanned front-to-back:

- **`is_next = false`** (normal right-click): clears that unit’s active move and any pending orders for it, then applies immediately on the next apply pass.
- **`is_next = true`** (shift+right-click): appended; applied only when the unit has no active move (waypoint chain / multiline path foundation).

`StepForward` applies orders, advances movement, then applies again so a finished segment can start the next `is_next` waypoint in the same tick.

Float/double is used only for the in-tick lerp; persisted unit position stays integer. (True bit-identical netcode / reverse ticks are future work.)

**Static battle data** (from level JSON): world size, ticks_per_second, unit definitions, pathing grid (from `obstruction`).

**Live battle data**: units (id, type, position, rotation, active move) and pending orders (`is_next` waypoints retained until applied or cancelled).

## Unreal display / input

Unreal syncs actor transforms to sim unit positions each sim tick.
Selection is Unreal-only. Ordering converts world hit → grid → `SubmitMoveOrder` into RTSEngine (shift held → `is_next`).
Unit types map to display actors via `UUnitViewManager` (e.g. soldier cylinder, vehicle cube).

## First foundation (status)

- Discrete world + Soldier / Vehicle defs
- Forward-only tick engine with straight-line moves, A* pathfinding, and `is_next` waypoint chains
- JSON level load → seed + level bake tool (obstructions + spawns)
- Unit actors, debug HUD, click select, right-click move / shift+right-click queue
