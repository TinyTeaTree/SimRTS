---
name: Retroactive Empties
overview: "Empties reuse last click order_id. On each Order, fill ATs since that player's last known click when the packet is a consecutive click (id = last+1) or an empty with that id. Dropped clicks cannot be inferred."
---

# Retroactive Empties

Living lockstep note. Wi‑Fi drops idle heartbeats. Lock needs a command for every `actual_tick`. Empties do not bump `order_id`. A later packet can prove the hole was idle.

Click vs empty is **unit count**, not the `type` byte (both Move). Click = `unit_ids.size() > 0`. Empty = count 0.

Still send one empty or click per Actual Tick. No new ACK kinds. Wire layout unchanged ([`udp_packets.plan.md`](udp_packets.plan.md)). Host still does not parse Order bodies. Empties never enter `SubmitScheduled`.

## Stamp

[`NextOrderId++`](../SimRTS/Source/SimRTS/Framework/SimRTSGameMode.cpp) only on clicks. Empties stamp `LastClickOrderId` (0 until the first click). Same AT: empty then click both send; click then skip empty unchanged.

## Fill

Per player: last known click `(LastClickAT, LastClickOrderId)`, start `(-1, 0)`. On each Order (send and bounce):

- Click with `order_id == last + 1`: fill `LastClickAT + 1 .. actual_tick - 1`, then this packet is the new last click.
- Empty with `order_id == last`: fill the same range.

UDP reorder: keep a bag of received `(AT, order_id, is_click)` and retry until no consecutive click applies, then apply matching empties.

Do not invent a missing click (`12` then `14` with no `13`).

## Out of scope

Resend, extra ACK kinds, raising the 1200-byte cap, changing FTD.
