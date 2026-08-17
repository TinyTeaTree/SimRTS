# Deterministic math

Notes for making RTSEngine movement (and later combat, etc.) **bit-identical across devices** for lockstep / UDP PVP and reversible ticks.

**Status:** segment progress / idle-push pressure / disc overlap are integer (or fixed-point) in the sim. Rotation yaw still uses `atan2`/`double` (visual only). Do not expand float use in gameplay state.

## Goal

- Same inputs + same tick → **same** `BattleState` on every platform
- Prefer **integer / fixed-point** ops only in the sim hot path
- No dependency on platform libm for gameplay math

## Subjects

| Note | Topic |
|------|--------|
| [why_floats_are_risky.md](why_floats_are_risky.md) | Why IEEE floats break lockstep |
| [fixed_point.md](fixed_point.md) | SCALE so measurable values survive division |
| [segment_progress.md](segment_progress.md) | Linear Euclidean move: isqrt once + SCALE lerp |
| [integer_sqrt.md](integer_sqrt.md) | Deterministic `isqrt` and Euclidean length |
| [overflow.md](overflow.md) | Width / product checklist |
| [integer_state.md](integer_state.md) | What must stay integer forever |
| [disc_overlap.md](disc_overlap.md) | Integer disc overlap (in use today) |
| [non_goals.md](non_goals.md) | Scope boundaries and when to implement |

## Remaining

Core movement fixed-point is done. Later: identical yaw if lockstep needs it; overflow audits for max tick × speed × SCALE.
