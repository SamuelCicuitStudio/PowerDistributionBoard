# Fast Warm-Up and Equilibrium Control Implementation Plan

This document describes, step by step, how the control refactor will be
implemented, exactly what will be removed, and which files will change.
It is intended to be executed in order and verified after each phase.

## Step 1: Baseline review and checkpoints
1. Identify all control mode branches (sequential, advanced, planner) and
   where they are selected or configured.
2. List current NVS keys and UI fields related to control modes.
3. Note current calibration flows (model calibration and NTC calibration).
4. Confirm presence calibration requirements and where continuous checks will run.

Files to review:
- `src/system/DeviceLoop.cpp`
- `src/system/DeviceControl.cpp`
- `src/wire/WireSubsystem.cpp`
- `src/system/Config.hpp`
- `src/services/NVSManager.cpp`
- `src/comms/WiFiManager.cpp`
- `FlutterApp/adminapp/assets/admin/admin.html`
- `FlutterApp/adminapp/assets/admin/js/admin.js`

## Step 2: Remove legacy control modes and planner
1. Remove sequential/advanced control branches so only the fast warm-up +
   equilibrium control remains.
2. Remove planner/mask logic that selects outputs by a PWM planning mask.
3. Remove any enums, constants, and control-mode keys tied to those modes.

Files to change:
- `src/system/DeviceLoop.cpp` (remove mode branches and planner logic)
- `src/system/Config.hpp` (remove planner and legacy mode keys)
- `src/services/NVSManager.cpp` (remove defaults for removed keys)
- `src/comms/WiFiManager.cpp` (remove legacy control fields)
- `FlutterApp/adminapp/assets/admin/admin.html` (remove mode selectors and planner UI)
- `FlutterApp/adminapp/assets/admin/js/admin.js` (remove legacy fields and bindings)

## Step 3: Lock to fast warm-up + equilibrium control
1. Ensure the scheduling frame is the only timing structure in use
   (10-300 ms, default 120 ms).
2. Enforce single-wire activation per frame and full voltage when ON.
3. Use two phases:
   - Boost (fast floor warm-up): command high total demand to raise wire temps
     as fast as possible up to the wire max (`NICHROME_FINAL_TEMP_C_KEY`),
     while distributing energy across wires so one wire does not run away.
   - Equilibrium (hold): once the floor is close to target, switch to a
     smoother control that holds the floor temperature.
4. Apply wire safety caps: skip/reduce any wire with
   `T_wire_est >= wire_max - margin_wire` (margin_wire is fixed, not a user key).
5. Apply the floor guard: limit total demand so predicted
   `T_floor_next <= T_target - floor_margin`.
6. Clamp ON-time to min/max and max-average; scale if total exceeds frame.
7. Enforce safety limits (max temp, sensor validity).

Files to change:
- `src/system/DeviceLoop.cpp`

## Step 4: Target selection rules
1. Normal RUN: use NTC floor temperature as the control target.
2. Wire temps are safety caps only (wire max from `NICHROME_FINAL_TEMP_C_KEY`).
3. Boost -> equilibrium switch when:
   - `T_floor >= T_target - FLOOR_SWITCH_MARGIN_C_KEY`, or
   - the floor model predicts overshoot in the next step.
4. Wire test + model calibration: use the NTC-attached wire as feedback and target.
5. Run start: wait for cool-down from assumed 150 C unless the user confirms wires are cool.
6. After any restart or power cycle, require an explicit RUN command; never auto-resume.
7. Apply allowed/present wire filtering in all paths.
8. Run continuous presence checks during any heating activity (RUN, wire test, model calibration).

Files to change:
- `src/system/DeviceLoop.cpp`
- `src/system/DeviceControl.cpp`
- `src/comms/WiFiManager.cpp`

## Step 5: Presence calibration and runtime checks
1. Add presence NVS keys and defaults (thresholds, window, fail count, enable flag).
2. Implement `WirePresenceManager` (probe + update).
3. Run presence checks on every running/heating tick while outputs are active.
4. Disable any wire that fails presence and recompute allowed outputs immediately.
5. Expose presence flags via telemetry.

Files to change:
- `src/system/Config.hpp`
- `src/services/NVSManager.cpp`
- `src/wire/WirePresenceManager.*` (new)
- `src/system/DeviceThermal.cpp` or `src/system/DeviceControl.cpp` (tick hook)
- `src/system/DeviceCore.cpp` (allowed output gating if needed)
- `src/comms/WiFiManager.cpp`
- `FlutterApp/adminapp/assets/admin/admin.html`
- `FlutterApp/adminapp/assets/admin/js/admin.js`

## Step 6: Model calibration behavior
1. Run only on the NTC-attached wire.
2. Use NTC feedback to heat to the user target temperature.
3. Sample temperature/voltage/current every 0.5 s.
4. After reaching target, stop heating and log the cool-down to baseline.
5. Store model parameters and show them in the calibration UI.

Files to change:
- `src/services/CalibrationRecorder.cpp`
- `src/system/WireSubsystem.cpp`
- `src/comms/WiFiManager.cpp`
- `FlutterApp/adminapp/assets/admin/admin.html`
- `FlutterApp/adminapp/assets/admin/js/admin.js`

## Step 7: NTC calibration behavior and model selection
1. Add NTC model selection: "Steinhart-Hart (A,B,C)" or "Beta at T0".
2. Calibration window "Calibrate" uses Beta approximation.
3. "NTC Calibration" uses a reference temp (heatsink if blank) with no heating,
   then saves the parameters to NVS.
4. UI shows only relevant parameters based on selected model.

Files to change:
- `src/system/Config.hpp`
- `src/services/NVSManager.cpp`
- `src/comms/WiFiManager.cpp`
- `src/system/DeviceControl.cpp`
- `FlutterApp/adminapp/assets/admin/admin.html`
- `FlutterApp/adminapp/assets/admin/js/admin.js`

## Step 8: API and UI alignment
1. Remove legacy fields from `/control` and `/load_controls`.
2. Keep only fast warm-up / equilibrium parameters and NTC model selection parameters.
3. Update calibration status fields for live display in the UI.
4. Ensure the calibration page does not disconnect during a run.

Files to change:
- `src/comms/WiFiManager.cpp`
- `FlutterApp/adminapp/assets/admin/admin.html`
- `FlutterApp/adminapp/assets/admin/js/admin.js`

## Step 9: Remove PI control and settings
1. Remove PI control usage in the loop/controller.
2. Remove PI gains from NVS defaults and config keys.
3. Remove PI fields from the device settings UI and calibration summary.
4. Remove any PI calculations in calibration suggestions that are tied to
   the old PI control strategy.

Files to change:
- `src/system/DeviceLoop.cpp`
- `src/system/DeviceControl.cpp`
- `src/system/Config.hpp`
- `src/services/NVSManager.cpp`
- `src/comms/WiFiManager.cpp`
- `FlutterApp/adminapp/assets/admin/admin.html`
- `FlutterApp/adminapp/assets/admin/js/admin.js`
- `src/services/CalibrationRecorder.cpp` (if PI suggestion outputs are removed)

## Step 10: Cleanup and validation
1. Remove references to deleted keys, UI nodes, and planner logic.
2. Confirm only one control mode is visible in the UI.
3. Validate:
   - Normal RUN uses the NTC floor target; wire model is safety only.
   - Wire test uses the NTC-attached wire to hold its target.
   - Model calibration runs the NTC wire and captures heat-up/cool-down.
   - Run start waits for cool-down unless the user confirms wires are cool.
   - No auto-resume after restart; RUN only starts on explicit command.
   - Presence checks run continuously and disable disconnected wires.

## Removals (explicit)
- Control modes: sequential, advanced, and planner logic.
- Planner/mask-based output selection.
- Related UI controls, labels, and JavaScript handlers.
- Related NVS keys and defaults.
- Any API fields tied to removed modes or planner settings.
- PI control logic and PI gains (wire/floor) from runtime, NVS, and UI.

## No changes planned
- `FlutterApp/adminapp/assets/admin/js/mock.js` (kept as-is; no updates required)
