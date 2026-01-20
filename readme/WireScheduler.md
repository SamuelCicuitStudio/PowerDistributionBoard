# WireScheduler

## Role
Pure logic that schedules which wire is energized per frame and how long, based on total demand, resistance weighting, and safety flags. It emits single-wire packets for the fast warm-up + equilibrium loop.

## Inputs
- `WireConfigStore`: per-wire cold resistances and access flags.
- `WireStateModel`: per-wire `present`, `locked`, `overTemp`, `allowedByAccess`.
- `frameMs`: scheduling frame length.
- `totalOnMs`: total ON time budget for the frame (derived from control demand).
- `wireMaxC` and `wireTempEst[]`: used to skip wires near the safety cap.

## Behavior
- Build the eligible wire list (allowed + present + not locked + below temp cap).
- Compute per-wire weights (default `t_i` proportional to `R_i` to equalize energy).
- Allocate on-time per wire so `sum(onMs) <= totalOnMs`.
- Emit a sequence of `{mask, onMs}` with only one wire ON at a time.

## Outputs
- Per-frame packet schedule (single-wire masks + on-times) to apply.

## Where it runs
- In the RUN loop scheduling step (DeviceLoop/DeviceControl) before safety filtering and actuation.
