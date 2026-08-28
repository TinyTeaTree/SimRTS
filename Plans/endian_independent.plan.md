---
name: Endian-independent wire
overview: UDP multi-byte integers are little-endian via shift-and-mask, never memcpy. The rule is the same for 16-, 32-, and 64-bit. HTTP JSON is already text. Future checksums mix field values, not raw struct bytes.
todos:
  - id: le-helpers-16-32
    content: Put/Read U16 I32 U32 in CommsClient; Go appendU32/readU32 for host-parsed fields
    status: completed
  - id: server-md-le
    content: State little-endian integer packing in Server.md UDP section
    status: completed
  - id: le-helpers-64
    content: Add PutU64/ReadU64 (and Go) when a 64-bit field first appears on the wire
    status: completed
  - id: hash-by-value
    content: Mix checksum/hash from integer field values, never memcpy of structs
    status: completed
---

# Endian-independent wire

Living plan. Lockstep peers and the Go host must read the same datagram as the same numbers, even if a host is big-endian.

Endianness is **not** a 64-bit-only problem. It is the byte order of **any** integer wider than one byte: `uint16`, `int32` / `uint32`, and `int64` / `uint64`. A single `uint8_t` has no endianness.

```text
value 0x12345678
  little-endian bytes: 78 56 34 12
  big-endian bytes:    12 34 56 78
```

Same pattern for 16-bit (two bytes) and 64-bit (eight bytes).

## Wire contract

**UDP payload integers are little-endian.** Low byte first. Signed values use the two's-complement bit pattern, then pack as unsigned.

Do **not** use POSIX `htons` / `htonl` (big-endian "network byte order") for our payload. Those stay only where the OS socket API requires them (`sockaddr_in.sin_port` in [`CommsSockets.h`](../SimRTS/Source/RTSComms/Private/CommsSockets.h)).

HTTP JSON is decimal text. It is already endian-independent. Do not invent a parallel binary packing there.

## How to pack (the only allowed method)

Shift and mask into individual bytes. Never `memcpy` / `reinterpret_cast` a native integer onto the wire, and never `encoding/binary` of a host `uint32` in Go.

C++ (already in [`CommsClient.cpp`](../SimRTS/Source/RTSComms/Private/CommsClient.cpp)):

```cpp
void PutU32(std::vector<uint8_t>& out, uint32_t value) {
	out.push_back(static_cast<uint8_t>(value));
	out.push_back(static_cast<uint8_t>(value >> 8));
	out.push_back(static_cast<uint8_t>(value >> 16));
	out.push_back(static_cast<uint8_t>(value >> 24));
}
```

Go (already in [`udp.go`](../RTSServer/udp.go)):

```go
func appendU32(buf []byte, v uint32) []byte {
	return append(buf, byte(v), byte(v>>8), byte(v>>16), byte(v>>24))
}
```

This produces little-endian on **any** host. `memcpy` of a `uint32_t` would not.

`PutU64` / `ReadU64` pack eight bytes little-endian (`value >> (8 * i)` for `i` in `0..7`, implemented as two `PutU32`/`ReadU32` lo then hi). Used for UDP Order `state_hash`. The Go host does not parse Order bodies, so it has no `appendU64`.

## What already follows this

| Width | Helpers | On the wire today |
|---|---|---|
| 8-bit | `PutU8` / `ReadU8`, length-prefixed strings | magic `RTS1`, kind, ids, `type`, `is_next` |
| 16-bit | `PutU16` / `ReadU16` | unit-id count (Order; server relays opaque) |
| 32-bit | `PutI32` / `PutU32` / `ReadI32` / `ReadU32`; Go `appendU32` / `readU32` | coords, unit ids, `order_id`, `actual_tick`, ping `seq`, kickoff id / `remaining_ms` |
| 64-bit | `PutU64` / `ReadU64` | Order `state_hash` (server relays opaque) |

The server parses Ping / Pong / Kickoff / KickoffAck integers. Order bodies are rebroadcast as raw datagrams, but every client still encodes/decodes them with the same helpers.

## Checksums and hashes

Mixing `hash ^= (uint64_t)field` (or FNV over field values) is endian-independent.

Hashing the **raw memory** of a struct or an `int32_t` is not: the same sim state would hash differently on a big-endian host, and a leaked hash would look like a desync.

`TickEngine::GameplayHash()` mixes integer field values (FNV-1a), then `PutU64` puts `state_hash` on the Order datagram. The mix recipe and what to hash when gameplay grows: [`gameplay_hash.plan.md`](gameplay_hash.plan.md).

## Out of scope

- In-process `TickEngine` / `BattleState` layout (never sent as raw structs).
- Unreal actor transforms and JSON level files.
- Socket-address `htons` / `htonl` (OS requirement, not payload).
- Changing already-shipped UDP fields to big-endian.
