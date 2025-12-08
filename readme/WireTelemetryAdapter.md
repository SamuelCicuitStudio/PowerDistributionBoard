# WireTelemetryAdapter

## Role
Maps internal wire runtime state onto telemetry structures (`StatusSnapshot` and JSON for /monitor).

## Inputs
- `WireConfigStore`: currently unused in fillSnapshot, available for future expansion.
- `WireStateModel`: source of temps, outputs, last power, presence/locks.
- `StatusSnapshot`: destination structure used by Device/WiFi.

## Behavior
### fillSnapshot(StatusSnapshot&)
- Copies per‑wire temperatures from `WireRuntimeState.tempC` into `snap.wireTemps`.
- Flags outputs based on `WireStateModel::lastMask` into `snap.outputs`.
- Tracks total power internally (ready for future exposure).

### writeMonitorJson(JsonObject&)
- Emits arrays/objects:
  - `wireTemps`: array of temperatures.
  - `outputs`: object with `outputX` booleans reflecting `snap.outputs`.

## Where it runs
- `DeviceTransport::getTelemetry` populates `WireStateModel` from HeaterManager, then calls `WireTelemetryAdapter::fillSnapshot` to populate `StatusSnapshot` before UI/monitor publishing.
- `WireTelemetryAdapter::writeMonitorJson` is used by monitor endpoints to format JSON output.

