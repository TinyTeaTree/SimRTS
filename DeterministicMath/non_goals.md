# Non-goals (for now)

- Do not add reverse-tick history again just for math experiments
- Visual interpolation in Unreal can stay float; **sim authority** must not
- Do not place units along a move segment by dividing squared distances (wrong position along the line — see [segment_progress.md](segment_progress.md))

## Done / remaining

Movement uses fixed-point + `isqrt` once per segment ([segment_progress.md](segment_progress.md)). Remaining: identical yaw if lockstep needs it; overflow audits for max tick × speed × SCALE.
