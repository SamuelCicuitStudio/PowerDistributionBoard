# Setup Wizard and Calibration Guide

This document describes the UI setup wizard for first-time configuration and calibration. The goal is to guide a user through all required inputs and calibrations, and to persist wizard state in NVS so the UI can resume or enforce completion before RUN.

## Scope
- Setup runs in admin mode only; user mode is unavailable until setup is complete.
- Covers configuration inputs, calibration steps, and NVS "done" flags.
- Focuses on the minimum required data to safely start RUN.

## Required configuration inputs (NVS-backed)
These settings must be confirmed or set before calibration:

1) Identity and access
- Device ID: `DEV_ID_KEY`
- Admin login: `ADMIN_ID_KEY`, `ADMIN_PASS_KEY`
- User login (optional): `USER_ID_KEY`, `USER_PASS_KEY`

2) Wi-Fi
- Station SSID/password: `STA_SSID_KEY`, `STA_PASS_KEY`
- AP SSID/password: `DEVICE_WIFI_HOTSPOT_NAME_KEY`, `DEVICE_AP_AUTH_PASS_KEY`

3) Safety thresholds
- Over-temp trip and warn: `TEMP_THRESHOLD_KEY`, `TEMP_WARN_KEY`
- Floor max and nichrome target: `FLOOR_MAX_C_KEY`, `NICHROME_FINAL_TEMP_C_KEY`
- Current limit: `CURR_LIMIT_KEY`

4) Electrical baseline
- AC frequency and voltage: `AC_FREQUENCY_KEY`, `AC_VOLTAGE_KEY`
- Charge resistor value: `CHARGE_RESISTOR_KEY`
- Cap bank capacitance (calibration writes): `CAP_BANK_CAP_F_KEY`

5) Wire configuration
- Per-wire access flags: `OUT01_ACCESS_KEY`..`OUT10_ACCESS_KEY`
- Per-wire resistance: `R01OHM_KEY`..`R10OHM_KEY`
- Nichrome properties (global): `WIRE_OHM_PER_M_KEY`, `WIRE_GAUGE_KEY`
- NTC wire index: `NTC_GATE_INDEX_KEY`

6) Control timing (if exposed in UI)
- Mixed/energy timing params: `MIX_FRAME_MS_KEY`, `MIX_MIN_ON_MS_KEY`, `MIX_MAX_ON_MS_KEY`, `MIX_MAX_AVG_MS_KEY`
- Boost/hold tuning: `MIX_BOOST_K_KEY`, `MIX_BOOST_MS_KEY`, `MIX_HOLD_UPDATE_MS_KEY`, `MIX_HOLD_GAIN_KEY`

## Calibration tasks (must be completed for RUN)
1) Cap bank calibration (device-level)
- Run the capacitance discharge calibration.
- Saves `CAP_BANK_CAP_F_KEY` when complete.

2) Idle current calibration (device-level)
- Run idle current capture with relay on, heaters off.
- Saves `IDLE_CURR_KEY` when complete.

3) Per-wire thermal model calibration (wire-level)
- For each enabled wire (Wire1..Wire10):
  - Ensure the wire is installed in its physical position.
  - Connect the wire to its assigned gate/output.
  - Attach the NTC to that wire before starting calibration.
  - Run the per-wire calibration procedure.
  - Store per-wire tau/k/C parameters in NVS.
  - Notify the admin when that wire calibration completes.

4) Floor model calibration (device-level)
- Use the NTC as the floor temperature sensor.
- Use the heatsink DS18B20 as room/ambient temperature.
- Run the floor model calibration and store floor parameters in NVS.
- Notify the admin when the floor model calibration completes.

## NVS hooks for wizard state (to add)
To track setup completion and allow exact-step resume, add these NVS keys (<= 6 chars):

- `SETUP` (bool): overall setup completed.
- `STAGE` (int): last completed wizard step.
- `SUBSTG` (int): last completed sub-step within the wizard step.
- `STWIRE` (int): wire index currently being calibrated (0-based or 1-based, pick one).
- `CALCAP` (bool): cap bank calibration done.
- `CALIDC` (bool): idle current calibration done.
- `CALNTC` (bool): NTC calibration done (if used).
- `CALW1`..`CALW10` (bool): per-wire thermal calibration done.
- `CALFLR` (bool): floor model calibration done.
- `W1STG`..`W10STG` (int): per-wire calibration sub-step (ambient, heat, cool, save).
- `W1RUN`..`W10RUN` (bool): per-wire calibration in-progress flag.
- `W1TS`..`W10TS` (int): last calibration step timestamp (optional, seconds).
- `FLSTG` (int): floor calibration sub-step (ambient, heat, cool, save).
- `FLRUN` (bool): floor calibration in-progress flag.
- `FLTS` (int): last floor calibration step timestamp (optional, seconds).
- `CALVER` (int): calibration schema version (increment if math changes).

Floor model parameters to persist (<= 6 chars):
- `FLTAU` (double): floor time constant tau_f [s].
- `FLKLS` (double): floor heat-loss coefficient k_f [W/K].
- `FLCAP` (double): floor thermal capacitance C_f [J/K].

Per-wire model parameters to persist (<= 6 chars):
- `WnTAU`, `WnKLS`, `WnCAP` where n = 1..10 (e.g., `W1TAU`, `W10CAP`).

If a wire is disabled, the wizard may mark its `CALWn` as not-required.

## UI wizard flow (recommended)
1) Welcome + device status
- Show firmware version, device ID, Wi-Fi mode, and a "Setup Required" banner if `SETUP` is false.

2) Credentials and Wi-Fi
- Configure admin/user credentials.
- Configure STA/AP settings.
- Save and verify connection.

3) Safety limits
- Set temp warn/trip, floor max, nichrome target, current limit.
- Confirm safety summary.

4) Wire configuration
- Select which outputs will be used (enabled). The firmware supports 10 outputs even if only 4 are installed.
- Skipped outputs are automatically marked "not allowed" in NVS.
- Enter per-wire resistances for enabled outputs.
- Select the NTC-linked wire index.

5) Electrical baseline
- Set AC frequency/voltage and charge resistor.
- Run cap bank calibration (set `CALCAP`).
- Run idle current calibration (set `CALIDC`).

6) Per-wire thermal calibration
- For each enabled wire:
  - Confirm the wire is placed, connected to its gate, and the NTC is attached.
  - Run calibration and store tau/k/C for that wire.
  - Set `CALWn` for that wire.
  - Show a completion notification in the admin UI.

7) Floor model calibration
- Confirm NTC is attached to the floor location and the heatsink sensor is in free air.
- Run calibration and store `FLTAU/FLKLS/FLCAP`.
- Set `CALFLR`.
- Show a completion notification in the admin UI.

8) Review and finish
- Show a checklist of required items and their status.
- Set `SETUP = true` when all required steps are complete.

## UI behavior rules
- If `SETUP` is false, the UI should show a blocking banner and redirect to the wizard.
- The wizard should resume from `STAGE`/`SUBSTG` and show which items are missing.
- If a calibration was interrupted, resume at that wire using `STWIRE` and `WnSTG`, and restart the pending calibration step.
- If floor calibration was interrupted, resume using `FLSTG` and restart the pending floor calibration step.
- User mode should be hidden/locked until setup is complete.
- The "Run" action should be disabled unless:
  - all required config fields are present, and
  - required calibration flags are true.

## Reset and re-calibration
- Provide a "Reset setup" action that clears:
  - `SETUP`, `STAGE`, `SUBSTG`, `STWIRE`
  - `CALCAP`, `CALIDC`, `CALNTC`, `CALW1`..`CALW10`, `CALFLR`
  - `W1STG`..`W10STG`, `W1RUN`..`W10RUN`, `W1TS`..`W10TS`
  - `FLSTG`, `FLRUN`, `FLTS`
  - per-wire tau/k/C parameters (if re-calibration is requested)
  - floor model parameters `FLTAU/FLKLS/FLCAP` (if re-calibration is requested)
- After reset, the wizard should restart from step 1.

## Notes for implementers
- The wizard should store only validated values in NVS.
- Use the same HTTP endpoints as the existing settings pages.
- Each calibration action should be guarded to prevent concurrent runs.
