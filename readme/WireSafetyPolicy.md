# WireSafetyPolicy

## Role
Filters a requested wire mask according to device state and per-wire safety constraints before any GPIO changes occur.

## Inputs
- `requestedMask`: mask from the scheduler/controller or explicit test logic.
- `WireConfigStore`: access flags.
- `WireStateModel`: `present`, `locked`, `overTemp`, `allowedByAccess`.
- `DeviceState`: current device state (plus any explicit test/calibration mode flags).

## Rules
1. If the device is not in a heating-allowed state (Running, or Idle with an explicit test/calibration active), return mask 0.
2. For each bit set in `requestedMask`, clear it if:
   - Access flag is false, or
   - Wire is not present, or
   - Wire is `overTemp` or `locked`.

## Output
- A safety-filtered `mask` that is safe to apply.

## Where it runs
- Called inside `WireActuator::applyRequestedMask` before toggling hardware.
