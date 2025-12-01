# Device Loop & Safety Guide

This document describes how the device loop orchestrates outputs, reaches target resistance, monitors thermal limits, and enforces safety/transport-layer protections.

## Responsibilities
- State machine: Idle → Running → Error → Shutdown.
- Manage relay and up to 10 outputs to achieve target resistance/current goals.
- Monitor temperatures (board + wires) and enforce thermal limits.
- Track session stats (energy, duration, peaks) via PowerTracker.
- Obey access flags and transport-level commands; no direct external mutations.

## State Machine (high level)
- **Idle**: Relay off, outputs off, safe standby. Await start request from transport.
- **Running**: Relay on, outputs selected to meet target resistance; continuous monitoring of temps/current/voltage.
- **Error**: Entered on critical fault (over-temp, over-current, invalid config). Relay and outputs are disabled.
- **Shutdown**: Deep-off; used for sleep or forced stop. Relay/outputs off, fans may stop, waits for wake.
- Transitions happen only inside `Device::setState()`; external classes cannot set state directly.

## Command Path (Transport-Layer Protection)
- All write operations come through `DeviceTransport` as queued commands with ACKs (DevCmdType).
- Device owns sequencing and validation; transport never edits NVS or hardware directly.
- Each command handler checks for change before persisting (avoids NVS churn).
- On failure or invalid input, an ACK with `success=false` is returned.

## Output Selection & Target Resistance
- Desired resistance: `targetRes` from NVS or control command.
- Each wire has a stored resistance (R01..R10). Device computes combinations to approximate target:
  - Selects outputs in parallel to reach effective resistance near `targetRes`.
  - Skips outputs that are disabled by access flags or marked missing/faulty.
  - Prefers minimal number of outputs to limit heat/load while meeting goal.
- If no valid combination is safe, the device falls back to Idle/Error depending on context.

## Temperature Tracking (Virtual & Physical)
- Physical sensors: up to MAX_TEMP_SENSORS board temps; per-wire temps via HeaterManager (if present).
- Virtual safeguards:
  - If a sensor is missing/invalid, the wire is considered unavailable.
  - Missing temp data will halt output enablement to avoid uncontrolled heating.
- Thresholds:
  - Global over-temperature threshold from NVS (`TEMP_THRESHOLD_KEY`).
  - Per-wire threshold enforced in HeaterManager; outputs exceeding threshold are shut off and flagged.

## Session & Telemetry
- PowerTracker accumulates:
  - Current session energy (Wh), duration (s), peak power/current.
  - Lifetime totals and last session snapshot.
- StatusSnapshot surfaces: capVoltage, current, temps[], wireTemps[], outputs[], relay, fan speed, session stats.
- SSE stream exposes `{state, seq, sinceMs}` for zero-lag UI updates.

## Safety & Fault Handling
- Relay and outputs are forcibly turned off when:
  - Over-temp detected (board or any wire).
  - Invalid resistance configuration.
  - Transport command requests shutdown/stop.
  - Error state entered (e.g., hardware fault).
- Buzzer/LED overlays signal Wi‑Fi/auth, errors, and run/stop events.

## Access & Permissions
- Each output has an access flag (OUT01_ACCESS_KEY ... OUT10_ACCESS_KEY). If false, that output is never enabled.
- Only the device can change access flags via transport commands; UI/WiFiManager do not touch outputs directly.

## Fan & Thermal Aid
- Fan speed can be set via transport (CTRL_FAN_SPEED → DevCmdType::SET_FAN_SPEED).
- Device may override/limit fan commands if in fault or deep-off states.

## Deep Sleep / Shutdown Coordination
- When Wi‑Fi is disabled and state is Shutdown, Device prepares for deep sleep:
  - Relay/outputs off, heaters disabled, buzzer muted, Wi‑Fi off.
  - SleepTimer configures wake sources (BOOT, POWER_ON_SWITCH) and enters deep sleep.

## How to Extend Safely
- Add new control: define DevCmdType, handle in Device command switch with validation, return ACK.
- Add new safety: gate enablement in Running loop; transition to Error/Shutdown when tripped.
- Add new telemetry: extend StatusSnapshot and SSE payload; keep it bounded (small JSON).

