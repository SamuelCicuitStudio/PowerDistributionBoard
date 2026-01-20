# WirePresenceManager

NOTE: Presence checks must run continuously whenever the system is in a running/heating state so wire disconnects are detected immediately.

## Role
Centralizes wire presence detection. Probes all wires safely, updates both `WireInfo` (HeaterManager) and `WireStateModel`, and tracks disconnections during runtime pulses.

## Inputs
- `HeaterManager`: for wire resistances, output control, and storing `WireInfo.connected`.
- `CurrentSensor`: to measure current during probes (or estimate current when `CURRENT_SOURCE_KEY` selects estimate mode).
- `WireStateModel`: runtime presence flags.
- `busVoltage`: supply voltage used to compute expected current.
- Config: divider/charge resistor (`CHARGE_RESISTOR_KEY`) for leak current compensation.

## Calibration keys (NVS)
- `CALPRS` (bool): presence calibration done.
- `PMINR` (float): min ratio for probe and runtime checks.
- `PWIN` (int): probe window in ms.
- `PFAIL` (int): consecutive failures before missing.

## Behavior
### probeAll(...)
- Saves current output states, forces all outputs off.
- For each wire:
  - Skips invalid R.
  - Enables only that channel, samples current, computes expected `Iexp = V/R`.
  - Subtracts baseline divider/ground-tie leak: `Ileak = V / (Rtop + Rbot + Rgnd)`.
  - Computes `ratio = (Imeas - Ileak) / Iexp`.
  - If `ratio >= PMINR`, mark present; otherwise mark missing.
  - Updates `WireInfo.connected/presenceCurrentA` via `heater.setWirePresence` and mirrors `WireRuntimeState.present`.
- Restores previous output states and updates `WireStateModel.lastMask`.

### updatePresenceFromMask(...)
- For a given active mask + measured total current:
  - Computes expected conductance from wires that are present and cool (<150 C).
  - Subtracts leak current using the same divider/tie model.
  - If `Imeas_wire / Iexp < PMINR`, increments a failure counter.
  - If failures reach `PFAIL`, marks those mask wires as not present in both `WireStateModel` and `WireInfo`.
  - If ratio recovers, clears the failure counter.

### hasAnyConnected(...)
- True if any `WireRuntimeState.present` is true.

## Safety/guards
- Skips presence evaluation for wires at/above 150 C to avoid misclassifying hot wires.
- Returns early if bus voltage is invalid or no eligible conductance exists.

## Leak compensation
- Uses the actual bus voltage and `Rtop + Rbot + Rgnd` (with `Rtop=470 kOhm`, `Rbot=3.9 kOhm`, `Rgnd` from `CHARGE_RESISTOR_KEY`) to remove the static divider/ground-tie current before making presence decisions.

## Where it runs
- `HeaterManager::probeWirePresence` delegates to `WirePresenceManager::probeAll`.
- `HeaterManager::updatePresenceFromMask` delegates to `WirePresenceManager::updatePresenceFromMask`.
- `updatePresenceFromMask` is called on every running/heating loop while outputs are active (normal run, wire test, model calibration).
- Presence is surfaced to UI/telemetry through `WireStateModel` and `WireTelemetryAdapter`.
