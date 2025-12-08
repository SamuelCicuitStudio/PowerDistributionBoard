# WirePresenceManager

## Role
Centralizes wire presence detection. Probes all wires safely, updates both `WireInfo` (HeaterManager) and `WireStateModel`, and tracks disconnections during runtime pulses.

## Inputs
- `HeaterManager`: for wire resistances, output control, and storing `WireInfo.connected`.
- `CurrentSensor`: to measure current during probes.
- `WireStateModel`: runtime presence flags.
- `busVoltage`: supply voltage used to compute expected current.
- Config: ground‑tie/charge resistor (`CHARGE_RESISTOR_KEY`) for leak‑current compensation.

## Behavior
### probeAll(...)
- Saves current output states, forces all outputs off.
- For each wire:
  - Skips invalid R.
  - Enables only that channel, samples current, computes expected `Iexp = V/R`.
  - Subtracts baseline divider/ground‑tie leak: `Ileak = V / (Rtop + Rbot + Rgnd)`.
  - Compares `(Imeas - Ileak)` against `Iexp` with `minValidFraction/maxValidFraction`.
  - Updates `WireInfo.connected/presenceCurrentA` via `heater.setWirePresence` and mirrors `WireRuntimeState.present`.
- Restores previous output states and updates `WireStateModel.lastMask`.

### updatePresenceFromMask(...)
- For a given active mask + measured total current:
  - Computes expected conductance from wires that are present and cool (<150 °C).
  - Subtracts leak current using the same divider/tie model.
  - If measured/expected < `minValidRatio`, marks those mask wires as not present in both `WireStateModel` and `WireInfo`.

### hasAnyConnected(...)
- True if any `WireRuntimeState.present` is true.

## Safety/guards
- Skips presence evaluation for wires at/above 150 °C to avoid misclassifying hot wires.
- Returns early if bus voltage is invalid or no eligible conductance exists.

## Leak compensation
- Uses the actual bus voltage and `Rtop + Rbot + Rgnd` (with `Rtop=470 kΩ`, `Rbot=3.9 kΩ`, `Rgnd` from `CHARGE_RESISTOR_KEY`) to remove the static divider/ground‑tie current before making presence decisions.

## Where it runs
- `HeaterManager::probeWirePresence` delegates to `WirePresenceManager::probeAll`.
- `HeaterManager::updatePresenceFromMask` delegates to `WirePresenceManager::updatePresenceFromMask`.
- Presence is surfaced to UI/telemetry through `WireStateModel` and `WireTelemetryAdapter`.

