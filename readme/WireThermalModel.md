# WireThermalModel

## Role
Numerically integrates virtual wire temperatures from current and output history, keeps per‑wire lockouts, and publishes the latest estimates back into `WireStateModel` and `HeaterManager`.

## Inputs
- `CurrentSensor::Sample[]`: timestamped current samples.
- `HeaterManager::OutputEvent[]`: timestamped output mask transitions.
- `idleCurrentA`: baseline current to subtract from the measurements.
- `ambientC`: current ambient estimate.
- Per‑wire cold resistance `R0` and mass (from `HeaterManager::getWireInfo`).

## State (per wire)
- `T`: last estimated temperature.
- `R0`: cold resistance.
- `C_th`: thermal capacity (mass · specific heat).
- `tau`: time constant for cooling.
- `locked` + `cooldownReleaseMs`: thermal lockout bookkeeping.
- `lastUpdateMs`: timestamp of last integration.

## Algorithm
1. **Cooling**: For each new current sample window, cool every wire toward ambient using `T = Tinf + (T - Tinf)·exp(-dt / tau)`.
2. **Heating**: For active wires in the current mask, compute conductance sum, derive per‑wire power `P = V²/R(T)` where `V = Inet / Gtot`, and increase temperature by `ΔT = (P·dt)/C_th`.
3. **Clamp & publish**: Clamp temperature to `[ambient-10, WIRE_T_MAX_C]`, update `WireStateModel.tempC`, `lastPowerW`, `lastUpdateMs`, and call `HeaterManager::setWireEstimatedTemp`.
4. **Mask tracking**: Track the latest applied mask in `WireStateModel::lastMask` for continuity between windows.

## Safety
- Temperature clamp at `WIRE_T_MAX_C` (150 °C).
- Lockout/resume handled via `locked`/`cooldownReleaseMs` (consumers can set/clear based on policy).

## Where it runs
- `DeviceThermal.cpp::updateWireThermalFromHistory` pulls history from `CurrentSensor` and `HeaterManager`, then calls `WireThermalModel::integrate`.

## Key constants (local copy in `src/system/WireSubsystem.cpp`)
- `NICHROME_CP_J_PER_KG = 450`
- `NICHROME_ALPHA = 0.00017`
- `DEFAULT_TAU_SEC = 1.5`
- `WIRE_T_MAX_C = 150`

