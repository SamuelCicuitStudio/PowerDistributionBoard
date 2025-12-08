# WirePlanner

## Role
Pure logic that chooses a wire mask whose equivalent resistance is closest to a target, honoring availability flags from `WireStateModel`.

## Inputs
- `WireConfigStore`: per‑wire cold resistances, target resistance.
- `WireStateModel`: per‑wire `present`, `locked`, `allowedByAccess`.
- `targetResOhm`: requested target (falls back to config if invalid).

## Behavior
- Iterates over all non‑zero masks (1..(1<<10)-1).
- Rejects any mask containing a wire that is not present, locked, or disallowed by access.
- Computes `Req = 1 / Σ(1/Ri)` for that mask.
- Picks the mask with minimal `|Req - target|`.

## Outputs
- Returns the candidate `requestedMask` (not yet safety‑filtered or applied).

## Where it runs
- `DeviceLoop` (advanced mode): calls `wirePlanner.chooseMask(...)` before safety filtering and actuation.

