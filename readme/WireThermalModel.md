# WireThermalModel

## Role
Numerically integrates virtual wire temperatures from bus history and output history, keeps per-wire lockouts, and publishes the latest estimates back into `WireStateModel` and `HeaterManager`.
Each wire has its own calibrated model parameters stored in NVS after installation.

## Inputs
- `BusSampler::Sample[]`: timestamped voltage/current samples (current from selected source; estimate mode includes the charge/discharge resistor path).
- `HeaterManager::OutputEvent[]`: timestamped output mask transitions.
- `ambientC`: current ambient estimate.
- Per-wire cold resistance `R0` and mass (from `HeaterManager::getWireInfo`).
- Per-wire thermal parameters `tau/k/C` loaded from NVS.

## State (per wire)
- `T`: last estimated temperature.
- `R0`: cold resistance.
- `C_th`: thermal capacity (mass * specific heat).
- `tau`: time constant for cooling.
- `locked` + `cooldownReleaseMs`: thermal lockout bookkeeping.
- `lastUpdateMs`: timestamp of last integration.

## Algorithm
1. **Cooling**: For each new sample window, cool every wire toward ambient using `T = Tinf + (T - Tinf) * exp(-dt / tau)`.
2. **Heating**: For active wires in the current mask, compute conductance sum, derive per-wire power `P = V^2 / R(T)` where `V = Inet / Gtot`, and increase temperature by `dT = (P * dt) / C_th`.
3. **Clamp & publish**: Clamp temperature to `[ambient-10, WIRE_T_MAX_C]`, update `WireStateModel.tempC`, `lastPowerW`, `lastUpdateMs`, and call `HeaterManager::setWireEstimatedTemp`.
4. **Mask tracking**: Track the latest applied mask in `WireStateModel::lastMask` for continuity between windows.

## Per-wire model math (used for calibration and runtime estimates)
For wire i:

`C_i * dT_i/dt = P_i(t) - k_i * (T_i - T_amb)`

Where:
- `T_i` = wire temperature (NTC during calibration; estimated during runtime)
- `T_amb` = ambient temperature measured before heating
- `C_i` = thermal capacitance (J/K)
- `k_i` = heat-loss coefficient (W/K)
- `tau_i = C_i / k_i`

Power from voltage and resistance:
- If ON with approximately constant voltage `V_on`:
  - `P_i,on ~= V_on^2 / R_i`
- With logged voltage:
  - `P_i(t) = (V(t)^2 / R_i) * u(t)`
  - `u(t) = 1` when wire ON, else 0

## Calibration procedure (per wire, using NTC on that wire)
Step A: measure ambient
- Keep wire OFF until stable.
- `T_amb = average of last 30-60 s`.

Step B: apply a constant-power step
- Turn only wire i ON with fixed duty/ON time.
- Log at 5-10 Hz: time `t`, temperature `T(t)`, voltage `V(t)`.
- Compute `P = avg(V(t)^2 / R_i)`.

Step C: extract `tau_i` and `deltaT_inf`
- Step response:
  - `T(t) = T_amb + deltaT_inf * (1 - exp(-t / tau_i))`
- 63 percent method:
  - `T_inf` = average of the last 30-60 s of the ON period.
  - `deltaT_inf = T_inf - T_amb`
  - `T_63 = T_amb + 0.632 * deltaT_inf`
  - `tau_i ~= t_63` when `T(t)` first reaches `T_63`.
- Log-linear fit (more robust if not fully steady):
  - `y(t) = ln(1 - (T(t) - T_amb) / deltaT_inf)`
  - Fit `y(t) = -t / tau_i` (slope = `-1 / tau_i`).

Step D: compute `k_i`
- At steady state: `P = k_i * deltaT_inf`, so `k_i = P / deltaT_inf`.

Step E: compute `C_i`
- `C_i = k_i * tau_i`.

Store `(tau_i, k_i, C_i)` for each wire in NVS (Wire1..Wire10).

## Runtime temperature estimation (per wire)
Update each wire at a fixed `dt`:
- `T_i <- T_i + (dt / C_i) * (P_i - k_i * (T_i - T_amb))`

Approximate power per update window:
- `P_i ~= (V^2_avg / R_i) * d_i`
- `d_i` = duty for wire i during the window
- `V^2_avg` = average of `V(t)^2` while switching

Initialization:
- On cold start, set `T_i = T_amb`.

## Hard limit using the model
- Trip if `T_est >= T_max - margin` (use a margin like 10-20 C).
- Keep a hard stop on the real NTC channel as well.

## Safety
- Temperature clamp at `WIRE_T_MAX_C` (150 C).
- Lockout/resume handled via `locked`/`cooldownReleaseMs` (consumers can set/clear based on policy).
- Presence detection is handled separately; see `readme/WireThermalAndPresence.md`.

## Where it runs
- `DeviceThermal.cpp::updateWireThermalFromHistory` pulls history from `BusSampler` and `HeaterManager`, then calls `WireThermalModel::integrate`.

## Key constants (local copy in `src/system/WireSubsystem.cpp`)
- `NICHROME_CP_J_PER_KG = 450`
- `NICHROME_ALPHA = 0.00017`
- `DEFAULT_TAU_SEC = 1.5`
- `WIRE_T_MAX_C = 150`
