# Remarqs: Required Work Split

This file lists explicit work items split into frontend (web UI) and backend
(firmware/server) tasks.

## Frontend (Web UI)

### Setup wizard behavior

- Enforce step order: a step cannot be opened until the previous step is
  completed or skipped.
- Skipping a step marks it complete and must not block the final "Finish"
  action.
- Steps must be readable and use proper layout; the device settings step
  cannot be cramped or unreadable.
- WiFi setup and admin credentials are optional and must have a clear Skip
  action.
- After setup is done, the User tab stays accessible.
- Disabled outputs are removed from pending wire calibration and presence
  lists.
- Wire calibration flow order is:
  1. Presence check (voltage drop)
  2. Select outputs to enable/disable
  3. Edit NTC parameters
  4. Wire calibration section (select wire, start, or skip)
- Floor calibration flow:

  - Requires wire calibration completed (not skipped).
  - Provide "Go edit NTC parameters", "Go to wire calibration", and "Start
    floor calibration" actions.

    all the calibrations should be skippable.

### Device tab

- Add NTC parameter fields with defaults:
  - Beta (default 3977)
  - T0 in degC
  - R0 / R25 (resistance at T0, default 8063)
  - Fixed pull-up resistor value
- Add a current source selector for calculations: ACS or CSP discharge
  estimate.
- Add a "Presence Min Voltage Drop (V)" setting.
- Show wire and floor model parameters after calibration:
  - Dropdown to select wire index or floor
  - Show tau/k/C and allow manual edits

### Dashboard and layout

- Current gauge always shows ACS measured current.
- Setup banner does not push content down or break the layout.
- Dashboard content is scrollable.
- Replace the sidebar "wires cool" button with a timed popup prompt that
  auto-dismisses after about 5 seconds.

### Calibration UI

- Force calibration button runs "capacitance calibration + ACS zero
  calibration" and shows progress/feedback.
- Remove any NTC calibration actions from the UI.

## Backend (Firmware/Server)

### Status/monitor data

- Provide ACS current separately in status/monitor data for UI display.
- Keep estimated current separate and never label it as the main "current"
  when idle.

### Calibration control loops (wire/floor)

- Create dedicated calibration loop classes for wire model calibration and
  floor calibration; they must be independent of the normal Device control
  loop.
- Calibration loops must ensure no other control loop is running (device is
  idle) before starting.
- Calibration loops must read the current source selection from NVS and use
  that choice during calibration.
- Wire calibration must verify the target wire presence before starting the
  calibration loop, then run heat/cool capture and compute tau/k/C.
- Floor calibration uses its own loop and is not allowed to reuse the normal
  control loop.

### Configuration and persistence

- Persist current source selection.
- Persist NTC Beta, T0, R0/R25, and fixed pull-up resistor.
- Persist "Presence Min Voltage Drop (V)" threshold.

### Presence detection

- Replace ratio-based presence with minimum voltage drop (Volts).
- Measure voltage drop by turning on a wire and sampling CSP discharge
  voltage before and after.

### Calibration and force calibration

- Capacitance calibration:
  - Discharge capacitors through wires, then recharge.
  - Measure voltage via CSP discharge path and compute capacitance.
- Force calibration uses its own calibration routine (not the normal control
  loop) and runs:
  1. Capacitance calibration
  2. ACS zero-offset calibration
- Only ACS zero-offset calibration is required (no NTC calibration flow).

### Setup gating and outputs

- Ignore disabled outputs when evaluating pending wire calibration and
  presence requirements.
- After setup is done, do not block access; pending lists reflect enabled
  outputs only.

### Model parameters

- Store and expose wire and floor tau/k/C for display and manual edits.
