# WireSafetyPolicy

## Role
Filters a requested wire mask according to device state and per‑wire safety constraints before any GPIO changes occur.

## Inputs
- `requestedMask`: mask from planner or user intent.
- `WireConfigStore`: access flags.
- `WireStateModel`: `present`, `locked`, `overTemp`, `allowedByAccess`.
- `DeviceState`: current device state.

## Rules
1. If `DeviceState` is not `Running`, return mask 0.
2. For each bit set in `requestedMask`, clear it if:
   - Access flag is false, or
   - Wire is not present, or
   - Wire is `overTemp` or `locked`.

## Output
- A safety‑filtered `mask` that is safe to apply.

## Where it runs
- Called inside `WireActuator::applyRequestedMask` before toggling hardware.

