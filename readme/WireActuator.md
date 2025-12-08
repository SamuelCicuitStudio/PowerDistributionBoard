# WireActuator

## Role
Glues requested masks to hardware by running them through `WireSafetyPolicy`, applying the result to `HeaterManager`, and recording it in `WireStateModel`.

## Inputs
- `requestedMask`: from planner or manual command.
- `WireConfigStore`: for access flags.
- `WireStateModel`: for runtime availability/safety flags.
- `DeviceState`: current device state.

## Behavior
1. Instantiates `WireSafetyPolicy` and filters `requestedMask`.
2. Applies the safe mask via `HeaterManager::setOutputMask` (through the `WIRE` singleton).
3. Writes the applied mask into `WireStateModel::lastMask` for telemetry/thermal continuity.

## Output
- Returns the `appliedMask` (may be zero if filtered out).

## Where it runs
- `DeviceLoop` (advanced mode) uses `WireActuator().applyRequestedMask(...)` after planning.
- Telemetry reads `WireStateModel::getLastMask()` to expose outputs to UI/monitoring.

