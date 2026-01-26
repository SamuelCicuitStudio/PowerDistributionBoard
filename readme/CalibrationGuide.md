# Calibration & Wire Test Guide

## What the tools do
- **NTC Calibration**: recompute NTC R0 from a reference temp (heatsink if left blank). No heating.
- **Temp Model Calibration**: heat the NTC-attached wire to a target, then log the heat-up and cool-down curve (saved to SPIFFS).
- **Wire Test**: use the NTC-attached wire as feedback to hold a target temp (no logging).
- **Floor Model Calibration**: estimate floor thermal parameters using NTC as floor temp and heatsink DS18B20 as room/ambient temp.
- **Cap Bank Calibration**: estimates capacitor bank capacitance by timed discharge through heater outputs; requires at least one connected wire.
- Tests/calibrations return to Shutdown (relay off) when stopped or finished.

## How sampling works
- When you start NTC or Model calibration, the recorder samples at the requested interval (default 500 ms, up to 2048 samples).
- Each sample stores: `t_ms`, `voltageV`, `currentA`, `tempC` (NTC), `ntcVolts`, `ntcOhm`, `ntcAdc`, validity/press flags.
- `currentA` follows `CURRENT_SOURCE_KEY`; in estimate mode it includes the charge/discharge resistor path.
- Model mode starts a fixed-duty calibration run so the curve captures heat-up/cool-down under a repeatable drive.
- Data is paged via `/calib_data?offset=&count=` and summarized by `/calib_status`.

## Cap bank calibration (discharge-based)
- Uses heater outputs to discharge the capacitor bank briefly, then computes `CAP_BANK_CAP_F_KEY`.
- **Requires at least one connected wire** (a real discharge path) or the calibration is skipped/failed.

## Per-wire thermal model calibration (Wire1..Wire10)
- Each wire has its own thermal parameters (tau, k, C) stored in NVS after calibration.
- Calibration is done per wire with the NTC attached to that wire (NTC temperature is the wire temperature).
- The firmware supports 10 wires; calibrate only the outputs you have enabled.

### Procedure (per wire)
1) Baseline: make sure the wire is cold, then record `T_amb` at start using the board + heatsink
   temperatures (or the current NTC reading). No stabilization wait.
2) Apply a constant power step: turn only that wire ON with fixed duty/ON time and log at 5-10 Hz:
   - time `t`, temperature `T(t)` from NTC, and voltage `V(t)` across the wire.
3) Compute average ON power:
   - `P = avg(V(t)^2 / R_i)`
4) Extract `tau` and `deltaT_inf`:
   - `T(t) = T_amb + deltaT_inf * (1 - exp(-t / tau))`
   - 63 percent method:
     - `T_inf` = average of the last 30-60 s of the ON period.
     - `deltaT_inf = T_inf - T_amb`
     - `T_63 = T_amb + 0.632 * deltaT_inf`
     - `tau ~= t_63` where `T(t)` first reaches `T_63`
   - Log-linear fit (robust if not fully steady):
     - `y(t) = ln(1 - (T(t) - T_amb) / deltaT_inf)`
     - Fit `y(t) = -t / tau` so slope = `-1 / tau`
5) Compute:
   - `k = P / deltaT_inf`
   - `C = k * tau`
6) Store per-wire `tau/k/C` in NVS for Wire1..Wire10.

## Presence calibration (wire connectivity)
- Presence calibration defines thresholds used to detect wire disconnects during heating.
- Run a presence probe in a safe idle state (relay on, outputs off) and store:
  - `CALPRS`, `PMINR`, `PWIN`, `PFAIL`.
- Full procedure and runtime behavior are specified in `readme/WireThermalAndPresence.md`.

## Floor model calibration (NTC floor + heatsink ambient)
This calibration builds a first-order floor model at the NTC location using:
- Floor sensor (NTC): bonded to a small metal spreader, firmly attached to the floor layer of interest.
- Room sensor (DS18B20 heatsink): free air, away from heaters.
- Do not move sensors after calibration.

### What you are calibrating
First-order floor model:
- `C_f * dT_f/dt = P_tot(t) - k_f * (T_f - T_room)`
- `tau_f = C_f / k_f`

Where:
- `T_f` = floor temperature from NTC
- `T_room` = ambient temperature from heatsink DS18B20
- `C_f` = thermal capacitance (J/K)
- `k_f` = heat-loss coefficient (W/K)

Power model (no current sensing):
- With fixed gate schedule:
  - `P_tot = V^2_avg * sum(d_i / R_i)`
  - `V` is measured bus voltage (log it)
  - `R_i` are cold resistances
  - `d_i` are fixed duties per wire during the test

### Calibration: step by step
Step 0 - Preparation (2-3 min)
- Pick moderate constant power (not full blast).
- Aim to raise the floor ~5-10 C above ambient.
- Choose a fixed cycle (e.g., 100 ms) and fixed duties `d_i`.
- Start logging at 1-2 Hz: time, `T_floor`, `T_room`, `V`.

Step 1 - Ambient reference (no wait)
- Heating OFF.
- Capture `T_room,amb` and `T_floor,amb` from current readings (or a short moving average).

Step 2 - Apply a constant power step (20-40 min)
- At t = 0, turn heating ON with the fixed gate schedule.
- Keep power constant (no regulation).
- Log continuously until:
  - the floor rise clearly slows/levels, or
  - it reaches target + 3-5 C.

Step 3 - Optional cool-down check (10-15 min)
- Turn heating OFF and keep logging.
- Optional cross-check for `tau_f` from cooling.

### Parameter extraction
A) Average power during ON:
- `P = avg(V^2_avg * sum(d_i / R_i))`

B) Steady rise:
- `T_floor,inf` = average floor temp over last 2-3 min of ON period
- `deltaT_inf = T_floor,inf - T_room,amb`

C) Heat-loss coefficient:
- `k_f = P / deltaT_inf`

D) Time constant (63 percent method):
- `T_63 = T_room,amb + 0.632 * deltaT_inf`
- `tau_f ~= t_63` when `T_floor(t)` first reaches `T_63`

E) Thermal capacitance:
- `C_f = k_f * tau_f`

Store `k_f` and `tau_f` (or `C_f`) in NVS.

## Normal run behavior
- **Control target**: the NTC temperature is the floor target; boost switches to equilibrium when the floor is close to target (using `FLOOR_SWITCH_MARGIN_C_KEY`).
- **Boost**: raise wire temps as fast as possible up to `NICHROME_FINAL_TEMP_C_KEY` while distributing energy across wires and respecting the floor guard.
- **Equilibrium**: hold the floor target smoothly while keeping wire estimates under the cap.
- **Thermal model**: used only for UI display and a hard safety cap (estimated wire temp <= 150 C or `NICHROME_FINAL_TEMP_C_KEY`).

## Parameters
- Thermal model (per wire): tau/k/C stored for each wire (Wire1..Wire10).
- Floor model (device-level): `FLTAU/FLKLS/FLCAP` for the NTC floor model.
- Safety: `tempWarnC`, `tempTripC`, `floorMaxC`, `nichromeFinalTempC`, `floorSwitchMarginC`.

## Step-by-step
1) Open **Calibration** (Live tab).
2) (Optional) **NTC Calibration** with a reference temp (blank = heatsink).
3) **Temp Model Calibration** to log a heating/cooling curve.
4) Compute tau/k/C from the captured data and store per-wire values in NVS.
5) **Wire Test** to verify holding a target temperature.
6) **Floor Model Calibration** to capture floor response and save floor params.
