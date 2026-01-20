# WireConfigStore & WireStateModel

## WireConfigStore (NVS-backed configuration)
- Holds per-wire cold resistances (`Rxx`) and access flags, plus global wire properties (ohm-per-meter, gauge).
- Loads/saves from Preferences (`*_KEY` constants).
- Provides getters/setters:
  - `getWireResistance`, `setWireResistance`
  - `getAccessFlag`, `setAccessFlag`
  - `getWireOhmPerM`, `setWireOhmPerM`
  - `getWireGaugeAwg`, `setWireGaugeAwg`

## WireStateModel (runtime state only)
- Per-wire `WireRuntimeState`:
  - `present`, `overTemp`, `locked`, `allowedByAccess`
  - `tempC`, `lastPowerW`, `lastUpdateMs`
- Tracks `lastMask` (last applied heater mask).
- No hardware or NVS access; pure runtime data used by control, safety, telemetry, and UI.

## Lifecycle / Flow
1. Device fills `WireStateModel` from HeaterManager + `WireConfigStore` at telemetry and control boundaries.
2. Thermal model updates `tempC`, `lastPowerW`, timestamps, and HeaterManager cache.
3. Presence manager updates `present`.
4. Scheduler/control reads `present/locked/allowedByAccess/overTemp` to decide packets.
5. Actuator writes `lastMask` after applying outputs.
6. Telemetry adapter publishes temps/outputs/presence to UI.
