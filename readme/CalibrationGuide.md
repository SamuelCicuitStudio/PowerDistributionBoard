# Calibration & Wire Test Guide

## What the tools do
- **NTC Calibration**: recompute NTC R0 from a reference temp (heatsink if left blank). No heating.
- **Temp Model Calibration**: log V/I/NTC temp over time while driving a fixed PWM profile (saved to SPIFFS).
- **Wire Test**: run the PI loop on the NTC wire to hold a target temp (no logging).
- **Model & PI suggestions**: compute τ/k/C and wire/floor PI gains from the latest calibration buffer + current settings; optionally persist to NVS.

## How sampling works
- When you start NTC or Model calibration, the recorder samples at the requested interval (default 500 ms, up to 2048 samples).
- Each sample stores: `t_ms`, `voltageV`, `currentA`, `tempC` (NTC), `ntcVolts`, `ntcOhm`, `ntcAdc`, validity/press flags.
- Model mode also starts the calibration PWM (Ton/Toff) so the curve captures heat-up/cool-down under a repeatable drive.
- Data is paged via `/calib_data?offset=&count=` and summarized by `/calib_status`.

## Sensors / estimators in run mode
- Wire loop: uses NTC directly when valid; otherwise, the virtual thermal model.
- Floor loop: uses the heatsink sensor as floor temperature input and outputs a wire target temperature.
- Virtual model: runs in the thermal task, integrating V/I history to maintain estimated wire temps for non‑NTC wires.

## Parameters
- Thermal model: `wireTauSec`, `wireKLoss`, `wireThermalC`.
- PI gains (persisted): `wireKp`, `wireKi`, `floorKp`, `floorKi`.
- Safety: `tempWarnC`, `tempTripC`, `floorMaxC`, `nichromeFinalTempC`.

## PI/thermal suggestions flow
- Click **Refresh** (Model & PI suggestions) to call `/calib_pi_suggest`, which uses:
  - Current τ/k/C
  - Max observed power from the latest calibration buffer (or a conservative guess)
  - Suggested wire PI (duty output) and floor PI (wire-target output)
- Click **Persist & Reload** to POST `/calib_pi_save`, write τ/k/C + PI gains to NVS, then reload settings in the UI.

## Step-by-step
1) Open **Calibration** (Live tab).
2) (Optional) **NTC Calibration** with a reference temp (blank = heatsink).
3) **Temp Model Calibration** to log a heating/cooling curve with PWM.
4) **Refresh** suggestions to see τ/k/C and suggested wire/floor Kp/Ki.
5) **Persist & Reload** to save them and repopulate settings.
6) **Wire Test** to verify holding a target temperature on the NTC wire.
