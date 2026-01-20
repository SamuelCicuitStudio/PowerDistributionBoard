# Device Loop & Safety Guide

This document describes how the device loop orchestrates outputs, monitors thermal limits, and enforces safety/transport-layer protections.

## Responsibilities
- State machine: Shutdown -> Idle -> Running -> Error -> Shutdown.
- Manage relay and up to 10 outputs to run fast warm-up + equilibrium control with per-wire balancing.
- Estimate virtual temperatures per wire and the floor temperature model.
- Run continuous presence checks whenever any heating is active; disable disconnected wires immediately.
- Monitor temperatures (board + floor NTC + wire estimates) and enforce thermal limits.
- Track session stats (energy, duration, peaks) via PowerTracker.
- Obey access flags and transport-level commands; no direct external mutations.

## State Machine (high level)
- **Idle**: 12V detected, relay on, outputs off. Preparation stage with cool-down wait (assume wires could be at 150 C unless user confirms cool) before RUN.
- **Running**: Relay on, outputs pulsed sequentially; NTC floor temperature is the control target, wire model is display/safety only.
- **Error**: Entered on critical fault (over-temp, over-current, invalid config). Relay and outputs are disabled.
- **Shutdown**: Deep-off; used for sleep or stop/finish. Relay/outputs off, fans may stop, waits for wake.
- After any restart or power cycle, the device stays in Shutdown/Idle and waits for an explicit RUN command; it never auto-resumes.
- Transitions happen only inside `Device::setState()`; external classes cannot set state directly.

## Command Path (Transport-Layer Protection)
- All write operations come through `DeviceTransport` as queued commands with ACKs (DevCmdType).
- Device owns sequencing and validation; transport never edits NVS or hardware directly.
- Each command handler checks for change before persisting (avoids NVS churn).
- On failure or invalid input, an ACK with `success=false` is returned.

## Energy Distribution & Balance
- The scheduler allocates one energy packet per allowed wire per frame (sequential, one output at a time).
- Boost (fast warm-up): command high total demand to raise wire temps as fast as possible up to the wire max (`NICHROME_FINAL_TEMP_C_KEY`), while distributing energy so one wire cannot run away.
- Floor guard: cap total demand so predicted `T_floor_next <= T_target - FLOOR_SWITCH_MARGIN_C_KEY`.
- Equilibrium: once the floor is close to target, switch to smooth control to hold the floor target while keeping wires under the cap.
- Presence checks run every heating tick; any wire marked missing is removed from allowed outputs.
- If no valid outputs are allowed, the loop idles or exits safely.

## Temperature Tracking (Virtual & Physical)
- Physical sensors: board temps from DS18B20 and the NTC floor temperature (control target in RUN).
- Floor model: first-order model using the NTC floor temperature and room/ambient from the heatsink sensor.
- Virtual wire temps: per-wire model using per-wire tau/k/C from NVS and bus voltage history.
- Thresholds:
  - Global over-temperature threshold from NVS (`TEMP_THRESHOLD_KEY`).
  - Per-wire limit enforced by the thermal model (estimated wire temp <= 150 C).

## Session & Telemetry
- PowerTracker accumulates:
  - Current session energy (Wh), duration (s), peak power/current.
  - Lifetime totals and last session snapshot.
- StatusSnapshot surfaces: capVoltage, current, temps[], wireTemps[], outputs[], wirePresent[], relay, fan speed, session stats.
- SSE stream exposes `{state, seq, sinceMs}` for zero-lag UI updates.

## Safety & Fault Handling
- Relay and outputs are forcibly turned off when:
  - Over-temp detected (board or any wire).
  - Wire disconnect detected while active (presence check failure).
  - Invalid resistance configuration.
  - Transport command requests shutdown/stop.
  - Error state entered (e.g., hardware fault).
- Buzzer/LED overlays signal Wi-Fi/auth, errors, and run/stop events.

## Access & Permissions
- Each output has an access flag (OUT01_ACCESS_KEY ... OUT10_ACCESS_KEY). If false, that output is never enabled.
- Presence flags gate outputs in addition to access flags; missing wires are not driven.
- Only the device can change access flags via transport commands; UI/WiFiManager do not touch outputs directly.

## Fan & Thermal Aid
- Fan speed can be set via transport (CTRL_FAN_SPEED -> DevCmdType::SET_FAN_SPEED).
- Device may override/limit fan commands if in fault or deep-off states.

## Deep Sleep / Shutdown Coordination
- When Wi-Fi is disabled and state is Shutdown, Device prepares for deep sleep:
  - Relay/outputs off, heaters disabled, buzzer muted, Wi-Fi off.
  - SleepTimer configures wake sources (BOOT, POWER_ON_SWITCH) and enters deep sleep.

## How to Extend Safely
- Add new control: define DevCmdType, handle in Device command switch with validation, return ACK.
- Add new safety: gate enablement in Running loop; transition to Error/Shutdown when tripped.
- Add new telemetry: extend StatusSnapshot and SSE payload; keep it bounded (small JSON).

