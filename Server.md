# RTSServer

Go matchmaking host for SimRTS. HTTP for login/rooms, UDP for order relay. Rooms and sessions are in memory and vanish when the process exits.

Default local address: `http://127.0.0.1:8080` and UDP `127.0.0.1:8081`

## Install

From the repo root. Needs **Node** (for the npm scripts) and **Go 1.22+**.

macOS (Homebrew):

```bash
brew install go
go version
```

Confirm `go` is on your `PATH`. The npm scripts also look in `/opt/homebrew/bin/go` and `/usr/local/go/bin/go`.

## npm scripts

Run these from the **repo root** (`SimRTS/`, next to `package.json`).

```bash
npm run server_build        # compile RTSServer/bin/rtsserver
npm run server_local_host   # listen on 127.0.0.1:8080 (this computer only)
npm run server_nat_host     # listen on 0.0.0.0:8080 (LAN); prints ip for other computers
npm run server_shutdown     # free port 8080 (SIGTERM, then SIGKILL)
npm run troubleshoot        # probe Networking.json host; on Mac, Local Network hints
```

`server_local_host` and `server_nat_host` occupy the terminal. Use a second terminal for curl.

`troubleshoot` POSTs `/Login` to the `ip`/`port` in `SimRTS/Content/Data/Networking.json`. On macOS 15+, Local Network permission cannot be read or turned on from a script. Terminal (npm) is usually exempt, so a successful probe does not mean Unreal Editor can connect. `npm run open` launches `UnrealEditor.app` directly; enable that app (and Epic Games Launcher if you open from there) in System Settings → Privacy & Security → Local Network. The script opens that pane unless you pass `--no-open`.

`server_nat_host` binds every interface and prints the LAN IPv4 to put in the other machine's `Networking.json` (`ip` / `port` / `udp_port`). Same Wi-Fi or ethernet segment; not the public internet.

If you see `bind: address already in use`:

```bash
npm run server_shutdown
npm run server_local_host
```

## HTTP

Anonymous **login** issues a session. Every other call must send that token:

```
X-Session-Token: <session_token>
```

`username` / room `id` are 1–64 letters, digits, `_`, or `-`. HTTP `JoinRoom` only checks that the room exists. The player is seated when the client’s first **UDP Hello** is acked (token + room + player id). Leave uses the session’s `player_id` (do not send `player_id` in the body).

| Method | Path | Auth | Body |
|---|---|---|---|
| POST | `/Login` | no | `{"username":"alice"}` |
| GET | `/GetRooms` | token | — |
| POST | `/CreateRoom` | token | `{"id":"alpha"}` |
| POST | `/JoinRoom` | token | `{"id":"alpha"}` |
| POST | `/LeaveRoom` | token | `{"id":"alpha"}` |
| POST | `/StartRoom` | token | `{"id":"alpha"}` |

Login returns `{ "session_token", "player_id", "nickname" }` (`nickname` = username). Each login is a new anonymous player. HTTP Join does not add the player to `player_ids`. `StartRoom` marks the session player ready; 404 if they are not seated (UDP Hello). Duplicate Start is 200. The response is the room `{ "id", "player_ids", "seats" }` of everyone seated at that call. `player_ids` stay login strings for the lobby; `seats` is the aligned join-order `u8` (0, 1, 2, …, never reused in that room). When every seated player has started, the server broadcasts UDP Kickoff. Duplicate create → `409`. Missing room or player not in room → `404`. Missing/bad token → `401`.

## UDP relay (`:8081`)

One server socket. Clients bind an ephemeral port and `sendto` the host.

| Kind | When |
|---|---|
| Hello | After HTTP Join. Body: room id, player id, session token. Server seats the player (join-order `u8`), maps the datagram address, Acks with that seat. |
| Ack | Reply to Hello. Body: `u8 seat`. Client treats Join as complete only after this and stores the seat. |
| Order | Move command. Body includes originator `u8 seat`, actual_tick, a `(hash_tick, state_hash)` pair, an ack vector, and optional piggybacked clicks. Server parses the trailer, caches clicks, and writes a per-recipient bounce (fully-acked watermarks + missing clicks). |
| Ping | After Join. Body: seq. Server echoes Pong to the sender only (not the room). |
| Pong | Reply to Ping. Same seq. Client measures RTT on the I/O thread. |
| Kickoff | After all seated players HTTP Start. Body: kickoff id, remaining_ms from first send. Repeated until KickoffAck from everyone or remaining hits 0. |
| KickoffAck | Client reply to Kickoff. Server does not relay it. |

Packets are binary, one datagram, max ~1200 bytes. Magic `RTS1`. Multi-byte integers are **little-endian** (low byte first) via shift-and-mask; never memcpy a native `uint16`/`uint32`/`uint64`. Same rule for 16-, 32-, and 64-bit. See [`Plans/endian_independent.plan.md`](Plans/endian_independent.plan.md). Field order and leftover bytes per kind: [`Plans/udp_packets.plan.md`](Plans/udp_packets.plan.md). Clients wait `max(0, remaining_ms - RTT/2)` then start the local sim timer. Repeats of the same kickoff id do not restart that wait.

## Curl tests

Start the host, then in another terminal:

```bash
# login (no token)
curl -sS -X POST http://127.0.0.1:8080/Login \
  -H 'Content-Type: application/json' \
  -d '{"username":"alice"}'
# {"session_token":"...","player_id":"p_...","nickname":"alice"}

# copy session_token from the login JSON into TOKEN
TOKEN='paste-session-token-here'

# no header → 401
curl -sS -w '\nHTTP %{http_code}\n' http://127.0.0.1:8080/GetRooms
# {"error":"missing X-Session-Token header"}
# HTTP 401

curl -sS http://127.0.0.1:8080/GetRooms \
  -H "X-Session-Token: $TOKEN"
# {"rooms":[]}

curl -sS -X POST http://127.0.0.1:8080/CreateRoom \
  -H 'Content-Type: application/json' \
  -H "X-Session-Token: $TOKEN" \
	-d '{"id":"alpha"}'
# {"id":"alpha","player_ids":[],"seats":[]}

curl -sS -X POST http://127.0.0.1:8080/JoinRoom \
  -H 'Content-Type: application/json' \
  -H "X-Session-Token: $TOKEN" \
  -d '{"id":"alpha"}'
# {"id":"alpha","player_ids":[],"seats":[]}
# HTTP Join authorizes only; player_ids/seats fill after UDP Hello

# after UDP Hello seats the player:
curl -sS -X POST http://127.0.0.1:8080/StartRoom \
  -H 'Content-Type: application/json' \
  -H "X-Session-Token: $TOKEN" \
  -d '{"id":"alpha"}'
```

Second player (another login):

```bash
curl -sS -X POST http://127.0.0.1:8080/Login \
  -H 'Content-Type: application/json' \
  -d '{"username":"bob"}'
TOKEN2='paste-bobs-session-token-here'

curl -sS -X POST http://127.0.0.1:8080/JoinRoom \
  -H 'Content-Type: application/json' \
  -H "X-Session-Token: $TOKEN2" \
  -d '{"id":"alpha"}'

curl -sS http://127.0.0.1:8080/GetRooms \
  -H "X-Session-Token: $TOKEN"
# {"rooms":[{"id":"alpha","player_ids":["p_...","p_..."],"seats":[0,1]}]}
```

(`player_ids` are generated ids, not the usernames.)

```bash
curl -sS -X POST http://127.0.0.1:8080/LeaveRoom \
  -H 'Content-Type: application/json' \
  -H "X-Session-Token: $TOKEN" \
  -d '{"id":"alpha"}'
```

Errors:

```bash
curl -sS -w '\nHTTP %{http_code}\n' -X POST http://127.0.0.1:8080/CreateRoom \
  -H 'Content-Type: application/json' \
  -H "X-Session-Token: $TOKEN" \
  -d '{"id":"alpha"}'
# {"error":"room already exists"}
# HTTP 409

curl -sS -w '\nHTTP %{http_code}\n' -X POST http://127.0.0.1:8080/JoinRoom \
  -H 'Content-Type: application/json' \
  -H "X-Session-Token: $TOKEN" \
  -d '{"id":"missing"}'
# {"error":"room not found"}
# HTTP 404

curl -sS -w '\nHTTP %{http_code}\n' http://127.0.0.1:8080/GetRooms \
  -H 'X-Session-Token: nope'
# {"error":"invalid session token"}
# HTTP 401
```

Optional: pull the token without pasting (needs Python 3):

```bash
TOKEN=$(curl -sS -X POST http://127.0.0.1:8080/Login \
  -H 'Content-Type: application/json' \
  -d '{"username":"alice"}' \
  | python3 -c 'import json,sys; print(json.load(sys.stdin)["session_token"])')
echo "$TOKEN"
```

Stop:

```bash
npm run server_shutdown
```
