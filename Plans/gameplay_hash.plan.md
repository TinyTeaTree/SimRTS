---
name: GameplayHash approach
overview: "How TickEngine::GameplayHash() is computed: portable FNV-1a over integer field values. Mix committed lockstep BattleState only. Expand this function when adding lockstep fields. Not a security hash. Desync transport lives in gameplay_state_hash.plan.md."
---

# GameplayHash approach

Living Engine contract. [`TickEngine::GameplayHash()`](../SimRTS/Source/RTSEngine/Private/TickEngine.cpp) turns committed sim state into one `uint64`. Same inputs must produce the same number on every host.

How that `uint64` is compared across clients (UDP piggyback, HUD, sticky desync) is [`gameplay_state_hash.plan.md`](gameplay_state_hash.plan.md). This file is the **recipe**: algorithm, mix order, what belongs in the hash when gameplay grows.

## Why not `std::hash` / `memcpy`

`std::hash` is implementation-defined. Hashing a struct’s raw bytes follows host endianness, padding, and layout. Either would make a correct lockstep look like a desync.

The hash is **not** cryptographic. It only has to make “these two `BattleState`s differ” visible. Do not replace it with SHA unless a later slice asks for that.

## Algorithm: FNV-1a 64-bit

Fowler–Noll–Vo 1a, 64-bit. One running `uint64`. For each **byte**:

1. `hash ^= byte`
2. `hash *= 1099511628211` (FNV prime)

Start from offset basis `14695981039346656037`. Those two constants are the public FNV-1a 64-bit pair. Do not invent new ones.

Wider integers are mixed **little-endian**, low byte first, same rule as the UDP wire ([`endian_independent.plan.md`](endian_independent.plan.md)):

| Helper | What it does |
|---|---|
| `mix8` | one byte (enums, bools as `0`/`1`) |
| `mix32` | four bytes, `value`, `value >> 8`, `>> 16`, `>> 24` |
| `mix_i32` | `static_cast<uint32_t>` then `mix32` (two’s-complement bits) |
| `mix64` | eight bytes, shifts `0, 8, …, 56` |

No floats. No `memcpy` of `int32_t` / structs. Signed values go through the unsigned bit pattern, then the same byte splits.

`bool` → one byte `0` or `1`. Enums (`UnitType`, `OrderType`) → `uint8_t` of the enumerator. Keep those enumerators in `0..255` or switch that mix to `mix32`.

## What the hash describes

Hash **committed `BattleState` only**: live `units` and live waypoint `orders`. Snapshot time is **after** `StepForward` (including `++tick_`). Tick **0** is the post-`LoadLevel` state.

Do **not** mix `tick_` into the hash. The pair on the wire is `(hash_tick = GetTick(), state_hash = GameplayHash())`. Mixing the tick would be redundant and would hide “same state, wrong clock” as a match.

### Include (today)

Units **sorted by `id`** (vector order is not the contract). Mix `uint32` count, then each unit:

- `id`, `type`
- `position.x/y`
- `move.active`, `move.start`, `move.end`, `move.start_tick`, `move.length_fp`
- `push_pressure_x/y`

Then live `orders` in **vector order** (queue order is gameplay). Mix `uint32` count, then each order:

- `player_id`
- `unit_ids` count, then each id
- `type`, `target.x/y`, `is_next`

Mix the collection **count first**, then elements, so length mismatches cannot alias.

### Exclude (today)

| Leave out | Why |
|---|---|
| `Unit.rotation` | Visual yaw; still `atan2` / `double` in `UpdateRotationToward`. Not lockstep-safe. |
| `scheduled_` cache | In-flight commands: A may have a packet B has not applied yet. Same sim tick, different cache → false desync. Once `scheduled_tick` runs, the result is in `BattleState` and is hashed. |
| `tick_` | Key of the pair, not payload. |
| `StaticBattleData` / `PathingGrid` / `UnitDef` | Loaded from the same JSON; not mutated per tick. Huge, and it would not catch unit divergence. If a future feature **mutates** static data mid-battle, that mutation is live state and must be hashed. |
| Unreal | Actors, selection, camera, HUD, colors, overlays. |
| Actual Tick, RTT, kickoff ids | Scheduling / comms, not `BattleState`. |

## Expanding `GameplayHash()`

Any new field that other clients must simulate the same way belongs in the hash. HP, cooldowns, combat timers, facing-as-gameplay (if it becomes integer and lockstep), resources, fog-of-war bits that affect orders — mix them.

Checklist when you add lockstep state:

1. Put the field on `Unit`, `Order`, or another `BattleState` member — not only in Unreal.
2. Mix it in `GameplayHash()` with `mix8` / `mix_i32` / `mix64` as above. Prefer **appending** after the existing fields of that object so current mix order stays stable.
3. Do not reorder or drop existing mixes without treating it as a hash-version break (every peer must ship the same recipe).
4. Extend [`tools/simrts_smoke_test.cpp`](../tools/simrts_smoke_test.cpp): two engines, same script → same hash; one extra mutation → different hash.
5. If the value is float/double today, it is **not** ready to hash. Integer / fixed-point first (`DeterministicMath/`).

Collections: mix `uint32` length, then elements in a **defined** order. Sort by a stable id when order is not itself gameplay (units). Keep vector order when it is (pending `orders`).

Do not hash:

- Visual-only or platform math (`rotation` until it is integer lockstep).
- Anything that can differ while the sim is still correct (scheduled cache, Unreal interpolation, ping).
- Pointers, `std::string` layout, padding.

## Out of scope here

UDP `hash_tick` / `state_hash`, history ring, HUD Synced/Desynced — [`gameplay_state_hash.plan.md`](gameplay_state_hash.plan.md).

Putting the `uint64` on the datagram — [`endian_independent.plan.md`](endian_independent.plan.md) (`PutU64`).
