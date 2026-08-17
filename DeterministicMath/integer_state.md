# What we keep integer forever

Already aligned with future lockstep:

- Unit `position` (discrete cells)
- Order targets
- `speed` (points per second, integer)
- `ticks_per_second` (from level JSON)
- Tick index
- Unit diameters / weights / idle-push pressure (fixed-point ints)
- `UnitMove.length_fp` (points × `kMoveScale`)

Float/double, if any, should stay **out of** RTSEngine gameplay state.

Visual interpolation in Unreal can stay float; **sim authority** must not.
