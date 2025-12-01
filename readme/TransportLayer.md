# Transport Layer Guide

This document explains how the transport layer mediates between WiFiManager/SwitchManager and Device, ensuring safe, serialized command handling with acknowledgments.

## Role & Goals
- Provide a thin command pipe from frontends to Device.
- Prevent direct NVS/hardware mutation from outside Device.
- Serialize changes, validate inputs, and return ACK/NAK.
- Emit state snapshots and events for zero-lag UI updates.

## Data Structures
- **DevCommand**: { DevCmdType type, int32_t i1, float f1, bool b1, uint32_t id }
- **DevCommandAck**: { uint32_t id, DevCmdType type, bool success }
- **StateSnapshot**: Device-owned structure with { state, seq, sinceMs, … } used for SSE/UI.

## Command Flow
1. Caller (WiFiManager/SwitchManager) builds a DevCmdType (e.g., SET_OUTPUT, SET_FAN_SPEED, REQUEST_RESET).
2. `DeviceTransport::sendCommandAndWait()` enqueues the command to Device’s queue and blocks (with timeout) for an ACK.
3. Device’s command handler:
   - Validates arguments and current state (Idle/Running/Shutdown/Error).
   - Applies changes if safe; writes to NVS only when the value actually changed.
   - Emits ACK {success=true} or {success=false}.
4. Caller gets a bool return; WiFiManager returns `applied` or `apply_failed` to the client.

## State & Telemetry
- `getStateSnapshot()` returns the latest Device StateSnapshot for quick reads (no blocking queue).
- `waitForStateEvent()` blocks for next state change event (used by SSE task).
- `/state_stream` uses `waitForStateEvent` to push `{state, seq, sinceMs}` to the UI.
- `/monitor` and `/load_controls` use the snapshot getters and persisted NVS values.

## Safety Rules
- Only Device changes its own state via `setState()`.
- Transport never touches NVS directly; it only conveys intent.
- Commands are idempotent where possible; no NVS write if value is unchanged.
- Access flags and output enablement are enforced inside Device; invalid/locked outputs are ignored/NAK’d.

## Supported Commands (examples)
- RUN / STOP / SHUTDOWN
- Relay on/off; per-output on/off (with access checks)
- Fan speed
- Buzzer mute / LED feedback
- Timing: on/off durations, inrush delay
- Voltage/frequency settings
- Target resistance, per-wire resistance, wire ohm/m
- Access flags per output
- Reset flag + restart request

## Extending the Transport
1. Add a new DevCmdType enum value in Device.
2. Implement handler in Device’s command switch with validation, state gating, and NVS change-on-diff.
3. Add a thin wrapper in DeviceTransport (e.g., `setFoo(...)`) that calls `sendCommandAndWait`.
4. Update WiFiManager/SwitchManager to use the new wrapper; return ACK to UI.
5. If UI needs it, add the control type in `/control` and adjust frontend to interpret ACKs.

## Debugging Tips
- Every command enqueue logs: `[Transport] Cmd enqueue type=X i1=... f1=... b1=...`
- Every ACK logs: `[Transport] ack type=X id=Y success=Z`
- WiFiManager logs control handling: `[WiFi] Handling control type:X` and result.
- If UI shows stale state, verify SSE stream is connected; fallback polling should still reflect snapshots.

