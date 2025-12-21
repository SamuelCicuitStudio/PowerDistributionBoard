# Device Manager (Device) Guide

This document explains how the firmware is organized around the `Device` class: which tasks run, how the state machine transitions, which temperature sources are used, and what changes between normal RUN, calibration, and wire test.

## Key modules (who owns what)

- `Device` (`src/system/Device.h`): state machine + task orchestration; the only place that is allowed to energize heaters in RUN.
- `DeviceTransport` (`src/system/DeviceTransport.*`): a safe facade for WiFi/UI/HTTP; forwards commands to `Device` and exposes snapshots.
- `WiFiManager` (`src/comms/WiFiManager.*`): HTTP API, SSE state stream, periodic telemetry snapshot for the web UI.
- `HeaterManager` (`src/control/HeaterManager.*`): owns GPIO outputs and output masks; stores per-wire `WireInfo` (presence, resistance, estimated temp).
- Sensors:
  - `TempSensor` (`src/sensing/TempSensor.*`): DS18B20 board/heatsink temperatures.
  - `NtcSensor` (`src/sensing/NtcSensor.*`): analog NTC temperature + "button pressed" detection on the shared analog pin.
  - `CurrentSensor` (`src/sensing/CurrentSensor.*`): continuous current sampling + safety limit.
  - `BusSampler` (`src/sensing/BusSampler.*`): synchronized V/I history at a fixed period (default 5 ms); can include NTC fields when requested.
- Thermal model: `DeviceThermal` (`src/system/DeviceThermal.cpp`, `src/system/WireSubsystem.h`) maintains virtual wire temperatures using first-order parameters `tau` / `kLoss` / `Cth`.
- Control loop: `DeviceControl` (`src/system/DeviceControl.cpp`) runs the energy-based sequential controller and wire target test logic.
- Calibration:
  - `CalibrationRecorder` (`src/services/CalibrationRecorder.*`): captures a time series of `{V, I, NTC}` samples with a single time base.
  - `ThermalEstimator` (`src/services/ThermalEstimator.*`): provides conservative suggestions for thermal parameters (tau/k/C) and can persist them.
- Energy tracking: `PowerTracker` (`src/services/PowerTracker.*`) integrates energy (Wh) and records session history.

## State machine (high level)

The `Device::loopTask()` RTOS task is the dispatcher that moves between:
- `DeviceState::Shutdown`: safe OFF state (relay off, heaters off).
- `DeviceState::Idle`: powered and ready, but not running the main heater loop.
- `DeviceState::Running`: main loop active (`Device::StartLoop()`).
- `DeviceState::Error`: fatal fault latched; relay/heaters forced off.

Transition triggers are posted as event bits (`EVT_WAKE_REQ`, `EVT_RUN_REQ`, `EVT_STOP_REQ`) via `DeviceTransport` (UI), and/or physical inputs.

## Background tasks (what runs all the time)

### Sensor sampling

- `CurrentSensor` continuous sampling is started in `Device::begin()` (see `src/system/DeviceCore.cpp`).
- `BusSampler` runs a dedicated task that pushes `{timestampMs, voltageV, currentA}` into a ring buffer at ~200 Hz (`periodMs=5` by default).
- `TempSensor` is updated by the temperature monitor task when enabled (board + heatsink DS18B20s).

### Thermal integration (virtual wire temperatures)

File: `src/system/DeviceThermal.cpp`
- Started in `Device::begin()` and kept alive outside RUN.
- Initializes the wire model once (`initWireThermalModelOnce()`), using ambient from DS18B20.
- Periodically consumes:
  - recent bus history (V/I) and/or current history,
  - current heater output mask states (from `HeaterManager`),
  - persisted thermal parameters (`WIRE_TAU_KEY`, `WIRE_K_LOSS_KEY`, `WIRE_C_TH_KEY`).
- Publishes estimated wire temperature back into `HeaterManager` (`WireInfo.temperatureC`), so all other code reads a single "wire temp" source.

### Control task (energy-based adjustment loop)

File: `src/system/DeviceControl.cpp`
- Started in `Device::begin()` and runs continuously, but it only drives outputs in specific modes.
- The controller allocates energy packets per wire in a sequential frame and adjusts packet sizes slowly based on temperature error (no PID/PI loop).
- Output driving rules (important):
  - The energy-based controller only energizes heaters in `DeviceState::Running` or when an Idle wire-target test is active.
  - Calibration energy runs and wire tests are mutually exclusive.

## Temperature sources (what is used when)

### Board / heatsink temperatures (always physical)

- Source: DS18B20 readings in `TempSensor`.
- Used for: UI display, ambient estimate, floor proxy, safety.

### Wire temperature during normal RUN (energy-based sequential)

- Source: virtual temperature computed by the thermal task and stored in `HeaterManager` (`WireInfo.temperatureC`).
- Reason: only one wire has a physical NTC; other wires must be estimated.

### Wire temperature during Wire Test

- Source: NTC (`NtcSensor`) if valid.
- Reason: wire test is explicitly "control the NTC-attached wire to a setpoint".

### Temperature in calibration capture

- Source: NTC if attached and valid, captured as `CalibrationRecorder::Sample.tempC`.
- Note: the capture buffer stores `tMs` (time since capture start), not real wall-clock time.

## Use cases (what changes depending on mode)

### 1) Normal RUN: `Device::StartLoop()`

File: `src/system/DeviceLoop.cpp`
- Entered only from the state machine when transitioning to `DeviceState::Running`.
- The loop allocates sequential energy packets across allowed wires; all output pulses are guarded:
  - outputs are forced off after every pulse,
  - delays use `delayWithPowerWatch()` so STOP/power-loss/faults abort quickly.
- The thermal task continues updating wire temperature estimates while RUN is active.
- `PowerTracker` session runs to compute energy (Wh), peaks, and session history.

### 2) Wire Test (Idle-only energy target)

File: `src/system/DeviceControl.cpp`
- Triggered by HTTP endpoints (`/wire_test_start`, `/wire_test_stop`, `/wire_test_status`).
- Runs only in `DeviceState::Idle`.
- Uses the energy-based controller to hold a target temperature across allowed wires.
- Does not create a logged calibration buffer.

### 3) NTC calibration (no heating)

- Triggered by HTTP endpoint `/ntc_calibrate`.
- Intended behavior: recompute NTC calibration parameters using heatsink temp as the reference (or a user-entered reference), without energizing heaters.

### 4) Thermal model capture (logged energy-based drive)

- Triggered by HTTP endpoints:
  - `/calib_start`, `/calib_stop`, `/calib_status`, `/calib_data`, `/calib_file`, `/calib_clear`
- Behavior:
  - Starts `CalibrationRecorder` to capture `{V, I, NTC}` at the requested interval into a bounded buffer.
  - In "model capture" mode it also starts an energy-based calibration run (Idle-only).
- What the data is used for:
  - Plotting the temperature curve in the UI.
  - Feeding `ThermalEstimator` for conservative tau/k/C suggestions.

## UI/HTTP data flow (admin page)

- Live telemetry:
  - `WiFiManager` periodically calls `DeviceTransport::getTelemetry(StatusSnapshot&)` and serves it via `/monitor`.
  - State transitions are streamed via SSE (`/state_stream`) using `DeviceTransport::waitForStateEvent(...)`.
- Settings:
  - `/load_controls` loads persisted settings from NVS and returns them to the UI.
  - `/control` applies changes (queued in WiFiManager and forwarded to DeviceTransport).
- Calibration:
  - `GET /calib_data` returns captured samples (for the chart).
  - `GET /calib_pi_suggest` returns suggested (and current) thermal parameters.
  - `POST /calib_pi_save` persists the shown thermal values and the UI reloads settings.

## Where control loops are activated

- `Device::startThermalTask()` and `Device::startControlTask()` are called during device startup (`src/system/DeviceCore.cpp`).
- The main heater loop is separate: it is only entered from `Device::loopTask()` when RUN is requested, and it blocks inside `Device::StartLoop()` until stop/fault.

## Primary entry points (quick links)

- Orchestration/task startup: `src/system/DeviceCore.cpp`
- State machine + RUN loop: `src/system/DeviceLoop.cpp`
- Thermal integration + virtual temps: `src/system/DeviceThermal.cpp`, `src/system/WireSubsystem.h`
- Energy-based control + wire test + calibration runs: `src/system/DeviceControl.cpp`
- UI transport facade: `src/system/DeviceTransport.cpp`
- HTTP routes and snapshots: `src/comms/WiFiManager.cpp`
- Calibration capture: `src/services/CalibrationRecorder.cpp`
- Suggestions/persist: `src/services/ThermalEstimator.cpp`
