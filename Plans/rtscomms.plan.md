---
name: RTSComms HTTP client
overview: Add RTSComms (STL + POSIX/WinSock TCP) as a non-blocking matchmaking client. One I/O thread does HTTP; SimRTS only enqueues calls and pumps results. Session token/player id live on the client. Compile the module; no SimRTS gameplay wiring yet.
todos:
  - id: module-skel
    content: Add RTSComms.Build.cs, RTSCommsAPI.h, IMPLEMENT_MODULE glue (NoPCHs + Core only)
    status: completed
  - id: types-client
    content: "Non-blocking CommsClient: enqueue Login/GetRooms/Create/Join/Leave; TryPop events; session stored on client"
    status: completed
  - id: io-thread
    content: One std::thread worker, mutex queues for requests and completions; HTTP stays blocking only on that thread
    status: completed
  - id: http-json
    content: TCP HTTP/1.0 (POSIX + WinSock shim) + tiny JSON for known shapes, used only by the I/O thread
    status: completed
  - id: ubt-wire
    content: Register RTSComms in uproject and ExtraModuleNames on game + editor targets
    status: completed
  - id: compile-check
    content: npm run compile and confirm UnrealEditor-RTSComms.dylib; no SimRTS.Build.cs / gameplay changes
    status: completed
---

# RTSComms HTTP client module

New Runtime module next to RTSEngine. It is the client API layer for [`RTSServer`](../RTSServer/) (local `http://127.0.0.1:8080`). SimRTS will call it later; this slice only adds the module and makes UBT build it.

SimRTS must stay **block-free**: never wait on sockets, HTTP, or the I/O thread. Enqueue + `TryPop` only. Documented in [`Project.md`](../Project.md).

```mermaid
flowchart LR
  Game["SimRTS game thread later"] -->|"enqueue Login/GetRooms never waits"| Client["CommsClient"]
  Client --> InQ["request queue"]
  InQ --> Io["I/O thread blocking HTTP"]
  Io -->|"HTTP JSON + X-Session-Token"| Host["RTSServer :8080"]
  Io --> OutQ["result queue"]
  Game -->|"TryPop non-blocking"| OutQ
```

## Constraints (locked)

- Comms `.h`/`.cpp`: STL + a **private TCP shim** ([`CommsSockets.h`](../SimRTS/Source/RTSComms/Private/CommsSockets.h): `#ifdef _WIN32` WinSock, else POSIX). That is the only file that knows the OS socket API. HTTP, JSON, and the I/O thread call `TcpConnect` / `TcpSend` / `TcpRecv` / `TcpClose` only. No `UObject`, no Unreal `HTTP`/`Json`.
- Module glue may depend on `Core` (same as [`RTSEngine.Build.cs`](../SimRTS/Source/RTSEngine/RTSEngine.Build.cs): `PCHUsage = NoPCHs`).
- **Public API never blocks.** `Login` / `GetRooms` / … enqueue and return immediately. Completions via non-blocking `TryPop` (SimRTS later pumps this on the game thread).
- Internally, **one `std::thread` I/O worker** may block on TCP/HTTP. That wait is not visible to SimRTS. Not async sockets; not Unreal `FRunnable`.
- No retries, reauth, TLS, or UDP. No callbacks from the I/O thread (unsafe for Unreal later).
- Session **token, player id, nickname** stored on `CommsClient` after a successful Login (worker updates, getters mutex-protected). Join/Leave send only room `id` (token attached on the worker).
- **No** [`SimRTS.Build.cs`](../SimRTS/Source/SimRTS/SimRTS.Build.cs) dependency in this slice (no gameplay wiring).

## Unreal wiring (required so the module actually compiles)

- [`SimRTS.uproject`](../SimRTS/SimRTS.uproject): add Runtime module `RTSComms`
- [`SimRTS.Target.cs`](../SimRTS/Source/SimRTS.Target.cs) and [`SimRTSEditor.Target.cs`](../SimRTS/Source/SimRTSEditor.Target.cs): `ExtraModuleNames.Add("RTSComms")`

## Files

```text
SimRTS/Source/RTSComms/RTSComms.Build.cs
SimRTS/Source/RTSComms/Public/RTSCommsAPI.h
SimRTS/Source/RTSComms/Public/CommsTypes.h
SimRTS/Source/RTSComms/Public/CommsClient.h
SimRTS/Source/RTSComms/Private/CommsSockets.h
SimRTS/Source/RTSComms/Private/CommsClient.cpp
SimRTS/Source/RTSComms/Private/RTSCommsModule.cpp
```

## Client API (non-blocking)

Default host `127.0.0.1:8080`.

Enqueue (return immediately): `Login`, `GetRooms`, `CreateRoom`, `JoinRoom`, `LeaveRoom`.

Pump: `TryPop(CommsEvent&)` — never waits for the network.

Desktop: Windows + Mac via the socket shim. Not Linux/consoles/mobile.

## Out of scope

- SimRTS GameMode / HUD / calling the client
- Unreal `FHttpModule` / `FSocket`
- Retries, token refresh, UDP, libcurl
