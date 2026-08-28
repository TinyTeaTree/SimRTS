# Agent types

When the user names a type (Simulation / Engine / Comms / Server), follow **Shared** plus that type’s section. Stay in that type’s files unless they explicitly ask you to cross a boundary. Architecture details: [`Rules.md`](Rules.md), [`Project.md`](Project.md), [`Server.md`](Server.md).

## Shared

- **Do not run git.** No `git status`, commit, push, checkout, or any other git command. The user owns version control.
- **Do not run npm.** No `npm run compile`, `compile_open`, `compile:hot`, `open`, `troubleshoot`, `server_build`, `server_local_host`, `server_nat_host`, `server_shutdown`, or other npm scripts. The user compiles, opens the editor, and starts/stops the server.
- Do not start or kill Unreal Editor, `rtsserver`, or bind port 8080/8081.
- Prefer the smallest change that matches existing style. Do not add Unreal NetDriver, Steam, or EOS.
- Casts: follow [`Rules.md`](Rules.md) (no ceremonial `static_cast`).

## Simulation Agent

Unreal game layer: display, input, HUD, UI, JSON I/O, bridge into the sim. Editor bake tools count as this type.

**Scope:** `SimRTS/Source/SimRTS/`, `SimRTS/Source/SimRTSEditor/`, Content JSON under `SimRTS/Content/Data/`.

**Do:**

- Keep SimRTS **block-free**. Game thread never waits on HTTP, sockets, or RTSComms I/O. Enqueue work, pump `TryPop` / sim ticks only.
- Selection and camera stay Unreal-only. Orders go into RTSEngine through `FSimBridge` (`SubmitMoveOrder`), not by mutating `BattleState` from actors.
- Load levels with `LevelLoader` → `Level` → `TickEngine::LoadLevel`. Do not put JSON parsers in RTSEngine.
- Unit actors follow sim positions each tick via `UUnitViewManager`. Do not replicate unit actors.

**Do not:** add Unreal types to RTSEngine or RTSComms; block the game thread on `recv` / Login; own a second sim clock.

## Engine Agent

Deterministic tick sim. Same inputs + same tick → same `BattleState`.

**Scope:** `SimRTS/Source/RTSEngine/`, `DeterministicMath/`, `tools/simrts_smoke_test.cpp`. Hash recipe: [`Plans/gameplay_hash.plan.md`](Plans/gameplay_hash.plan.md).

**Do:**

- Pure C++ (STL / data-only). No Unreal types, UObjects, sockets, HTTP, or JSON in sim sources. Module glue may depend on `Core` (`NoPCHs`).
- Gameplay math stays integer / fixed-point. Do not expand float use in persisted state (see `DeterministicMath/`).
- Orders in, `StepForward` out. Pathfinding, sparse goals, idle push, and movement stay inside the engine.
- Keep apply order: `ApplyOrders` → `AdvanceMovement` → `ApplyOrders` → `ApplyIdlePush` → `tick++`.
- Prefer extending `Order` / `TickEngine` over leaking sim rules into Unreal.
- When adding lockstep fields, extend `TickEngine::GameplayHash()` per [`Plans/gameplay_hash.plan.md`](Plans/gameplay_hash.plan.md).

**Do not:** talk to RTSServer; include RTSComms; use platform `libm` for gameplay (sqrt/trig on lockstep state).

## Comms Agent

Vendor-agnostic matchmaking **client**. Talks HTTP to RTSServer. Not the sim, not Unreal UI.

**Scope:** `SimRTS/Source/RTSComms/`. Protocol contract: [`Server.md`](Server.md). Plans: [`Plans/rtscomms.plan.md`](Plans/rtscomms.plan.md), [`Plans/endian_independent.plan.md`](Plans/endian_independent.plan.md).

**Do:**

- Pure C++ (STL + private TCP shim in `CommsSockets.h` only). No Unreal HTTP/Json, no `UObject`.
- Public API never blocks: `Login` / `GetRooms` / `CreateRoom` / `JoinRoom` / `LeaveRoom` enqueue and return. Completions via `TryPop` only. No callbacks from the I/O thread.
- One I/O worker thread may block on HTTP. Session token, player id, and nickname live on `CommsClient` after Login. Join/Leave send room `id` only; the worker attaches `X-Session-Token`.
- HTTP/1.0 over TCP plus UDP relay (Hello/Ack/Order) on a second port. Public API never blocks. Keep sync messages within one MTU.
- UDP integers are little-endian via `Put`/`Read` shift-and-mask, never memcpy. Same for 16-, 32-, and 64-bit.

**Do not:** pump `TryPop` inside RTSComms for Unreal; put lobby widgets here; change RTSServer unless the user asks both types.

## Server Agent

Go matchmaking **host**. Discoverable process for login/rooms. Same binary later on a VPS; locally it is `127.0.0.1:8080`.

**Scope:** `RTSServer/`. Behavior and curl examples: [`Server.md`](Server.md).

**Do:**

- HTTP for login/rooms; UDP Hello seats a player and maps their address; UDP Order is rebroadcast to the room (including sender). In-memory sessions and rooms are fine; they vanish on process exit.
- UDP integers are little-endian via `appendU32` / `readU32` shift-and-mask, never host `encoding/binary`. Same for 16- and 64-bit when those appear.
- Keep the existing HTTP routes and auth: `POST /Login` (no token); all other HTTP calls require `X-Session-Token`. HTTP Join only authorizes; seating is UDP Hello. Leave uses the session’s `player_id` (do not take `player_id` in the body).
- Preserve status codes: `401` bad/missing token, `404` missing room / not in room, `409` duplicate create.
- `username` and room `id`: 1–64 letters, digits, `_`, or `-`.
- Vendor-agnostic: this process is the hosting environment. Do not add Steam/EOS/Unreal dedicated-server targets.

**Do not:** run `npm run server_*` or bind 8080 yourself; implement ICE/coturn; put TickEngine or Unreal in this process unless the user expands the host role.
