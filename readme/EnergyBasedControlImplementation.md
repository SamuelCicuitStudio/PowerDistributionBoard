# Energy-Based Control Implementation Plan

This document describes, step by step, how the control refactor will be
implemented, exactly what will be removed, and which files will change.
It is intended to be executed in order and verified after each phase.

## Step 1: Baseline review and checkpoints
1. Identify all control mode branches (sequential, advanced, planner) and
   where they are selected or configured.
2. List current NVS keys and UI fields related to control modes.
3. Note current calibration flows (model calibration and NTC calibration).
4. Confirm allowed/present wire detection and where it is enforced.

Files to review:
- `src/system/DeviceLoop.cpp`
- `src/system/DeviceControl.cpp`
- `src/system/WireSubsystem.cpp`
- `src/system/Config.h`
- `src/services/NVSManager.cpp`
- `src/comms/WiFiManager.cpp`
- `data/admin.html`
- `data/js/admin.js`

## Step 2: Remove legacy control modes and planner
1. Remove sequential/advanced control branches so only energy-based control
   remains.
2. Remove planner/mask logic that selects outputs by a PWM planning mask.
3. Remove any enums, constants, and control-mode keys tied to those modes.

Files to change:
- `src/system/DeviceLoop.cpp` (remove mode branches and planner logic)
- `src/system/Config.h` (remove planner and legacy mode keys)
- `src/services/NVSManager.cpp` (remove defaults for removed keys)
- `src/comms/WiFiManager.cpp` (remove legacy control fields)
- `data/admin.html` (remove mode selectors and planner UI)
- `data/js/admin.js` (remove legacy fields and bindings)

## Step 3: Lock to energy-based sequential control
1. Ensure the scheduling frame is the only timing structure in use
   (10-300 ms, default 120 ms).
2. Enforce single-wire activation per frame and full voltage when ON.
3. Compute per-wire ON-time from resistance normalization plus temperature
   error, using two phases:
   - Boost: elevated energy until near target.
   - Hold: slow, bounded energy adjustments from temperature error.
4. Plateau handling is implicit: when error stops decreasing, the bounded
   energy adjustments increase packet size until the plateau is broken
   (within safety limits).
5. Clamp ON-time to min/max and max-average; scale if total exceeds frame.
6. Enforce safety limits (max temp, sensor validity).

Files to change:
- `src/system/DeviceLoop.cpp`

## Step 4: Target selection rules
1. Normal RUN: use NTC floor temperature as the only control target.
2. Wire temps are display/safety only (hard cap 150 C).
3. Wire test + model calibration: use the NTC-attached wire as feedback and target.
4. Run start: wait for cool-down from assumed 150 C unless the user confirms wires are cool.
5. Apply allowed/present wire filtering in all paths.

Files to change:
- `src/system/DeviceLoop.cpp`
- `src/system/DeviceControl.cpp`
- `src/comms/WiFiManager.cpp`

## Step 5: Model calibration behavior
1. Run only on the NTC-attached wire.
2. Use NTC feedback to heat to the user target temperature.
3. Sample temperature/voltage/current every 0.5 s.
4. After reaching target, stop heating and log the cool-down to baseline.
5. Store model parameters and show them in the calibration UI.

Files to change:
- `src/services/CalibrationRecorder.cpp`
- `src/system/WireSubsystem.cpp`
- `src/comms/WiFiManager.cpp`
- `data/admin.html`
- `data/js/admin.js`

## Step 6: NTC calibration behavior and model selection
1. Add NTC model selection: "Steinhart-Hart (A,B,C)" or "Beta at T0".
2. Calibration window "Calibrate" uses Beta approximation.
3. "NTC Calibration" uses a reference temp (heatsink if blank) with no heating,
   then saves the parameters to NVS.
4. UI shows only relevant parameters based on selected model.

Files to change:
- `src/system/Config.h`
- `src/services/NVSManager.cpp`
- `src/comms/WiFiManager.cpp`
- `src/system/DeviceControl.cpp`
- `data/admin.html`
- `data/js/admin.js`

## Step 7: API and UI alignment
1. Remove legacy fields from `/control` and `/load_controls`.
2. Keep only energy-based parameters and NTC model selection parameters.
3. Update calibration status fields for live display in the UI.
4. Ensure the calibration page does not disconnect during a run.

Files to change:
- `src/comms/WiFiManager.cpp`
- `data/admin.html`
- `data/js/admin.js`

## Step 8: Remove PI control and settings
1. Remove PI control usage in the loop/controller.
2. Remove PI gains from NVS defaults and config keys.
3. Remove PI fields from the device settings UI and calibration summary.
4. Remove any PI calculations in calibration suggestions that are tied to
   the old PI control strategy.

Files to change:
- `src/system/DeviceLoop.cpp`
- `src/system/DeviceControl.cpp`
- `src/system/Config.h`
- `src/services/NVSManager.cpp`
- `src/comms/WiFiManager.cpp`
- `data/admin.html`
- `data/js/admin.js`
- `src/services/CalibrationRecorder.cpp` (if PI suggestion outputs are removed)

## Step 9: Cleanup and validation
1. Remove references to deleted keys, UI nodes, and planner logic.
2. Confirm only one control mode is visible in the UI.
3. Validate:
   - Normal RUN uses the NTC floor target; wire model is safety only.
   - Wire test uses the NTC-attached wire to hold its target.
   - Model calibration runs the NTC wire and captures heat-up/cool-down.
   - Run start waits for cool-down unless the user confirms wires are cool.

## Removals (explicit)
- Control modes: sequential, advanced, and planner logic.
- Planner/mask-based output selection.
- Related UI controls, labels, and JavaScript handlers.
- Related NVS keys and defaults.
- Any API fields tied to removed modes or planner settings.
- PI control logic and PI gains (wire/floor) from runtime, NVS, and UI.

## No changes planned
- `data/js/mock.js` (kept as-is; no updates required)
