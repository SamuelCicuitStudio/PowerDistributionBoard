# Setup Wizard Backend Linking Reference

Scope: login page authentication and setup wizard only.
Backend endpoints and payloads live in `src/comms/` (see `WifiEnpoin.hpp` and
`WiFiRoutes_*.cpp`).

## Transport and auth
- Requests and responses use CBOR (`Content-Type: application/cbor`).
- CBOR reference for encoding/decoding: `D:\Freelancer\JOBS\@sertody\PowerDistributionBoard\.pio\libdeps\esp32-s3-devkitc1-n16r8\TinyCBOR`.
- Most endpoints require an authenticated session.
- Admin-only: `/setup_update`, `/setup_reset`, `/presence_probe`, `/ap_config`.
- Base address: use `http://powerboard.local` (mDNS) for API calls in both AP
  and station mode; do not use raw IPs.

## Login and session gating
- GET `/login` -> login page HTML.
- Localization: include login UI strings (login page and login error page) in
  the language pack so they follow the selected UI language.
- POST `/connect` with CBOR `{ username, password }` -> `{ ok, role, token,
  setupDone, setupRunAllowed, setupCalibPending }`.
  - If role is `user` and setup is incomplete, server returns
    `setup_required`.
- After login:
  - GET `/setup_status` to decide whether to show wizard or dashboard.
  - GET `/load_controls` to prefill wizard fields.
  - Keep the wizard open and non-closable until `setupDone` is true.
- POST `/disconnect` with `{ action: "disconnect" }` to end session.

## Wizard progress tracking
- GET `/setup_status` -> `setupDone`, `stage`, `substage`, `wireIndex`,
  `missingConfig`, `missingCalib`, `configOk`, `calibOk`, `runAllowed`,
  `calibPending`, plus per-wire and floor/presence/cap calibration state.
- POST `/setup_update` with any of:
  - `setup_done` (bool), `stage` (int), `substage` (int), `wire_index` (int).
  - Server rejects `setup_done=true` if required config is missing.
- POST `/setup_reset` with any of:
  - `clear_models`, `clear_wire_params`, `clear_floor_params`.

## Step-by-step linking

### Step 1: Welcome
- Language select:
  - Set: POST `/control` `{ action: "set", target: "uiLanguage", value: "en" }`
    (alias: `target: "language"`).
  - Prefill: `/load_controls` -> `uiLanguage`.
- Progress: `/setup_update`.

### Step 2: Credentials
- User credentials:
  - POST `/control` `{ action: "set", target: "userCredentials", value:
    { current, newPass, newId } }`.
- Admin credentials:
  - POST `/control` `{ action: "set", target: "adminCredentials", value:
    { current, username, password, wifiSSID, wifiPassword } }`.
- Progress: `/setup_update`.

### Step 3: Wi-Fi
- Station credentials:
  - POST `/control` `{ action: "set", target: "wifiSSID", value: "..." }`.
  - POST `/control` `{ action: "set", target: "wifiPassword", value: "..." }`.
- AP credentials (admin-only):
  - POST `/ap_config` `{ apSSID, apPassword }`.
- Progress: `/setup_update`.

### Step 4: Allowed outputs
- Prefill: `/load_controls` -> `outputAccess` map.
- Save toggles:
  - POST `/control` `{ action: "set", target: "Access1", value: true }`
    ... `Access10`.
- Progress: `/setup_update`.

### Step 5: Device settings
- Sampling and power:
  - `acFrequency` -> `/control` target `acFrequency` (sampling rate Hz).
  - `chargeResistor` -> `/control` target `chargeResistor`.
  - `currLimit` -> `/control` target `currLimit`.
  - `currentSource` -> `/control` target `currentSource` (int or
    string `"acs"` / `"estimate"`).
- Thermal safety:
  - `tempWarnC` -> `/control` target `tempWarnC`.
  - `tempTripC` -> `/control` target `tempTripC`.
- Thermal model (manual entry):
  - Wire target -> `/control` targets `wireTau{n}`, `wireK{n}`, `wireC{n}`.
  - Floor target -> `/control` targets `floorTau`, `floorK`, `floorC`.
- Floor settings:
  - `floorThicknessMm`, `floorMaterial`, `floorMaxC`,
    `floorSwitchMarginC`, `nichromeFinalTempC`.
  - NTC linked channel -> `ntcGateIndex`.
- Nichrome calibration:
  - `wireOhmPerM`, `wireRes{1..10}`.
- Energy control:
  - `wireGauge`.
- Prefill: `/load_controls` -> keys above (`chargeResistor`, `currLimit`,
  `currentSource`, `tempWarnC`, `tempTripC`, `wireTau`, `wireK`, `wireC`,
  `floorThicknessMm`, `floorMaterial`, `floorMaxC`, `floorSwitchMarginC`,
  `nichromeFinalTempC`, `ntcGateIndex`, `wireOhmPerM`, `wireRes`, `wireGauge`).
- Progress: `/setup_update`.

### Step 6: Sensor zero
- Start calibration sequence:
  - POST `/control` `{ action: "set", target: "calibrate" }`.
- Live readouts (poll `/monitor` or `/monitor_since`):
  - `currentAcs` (ADC zero value),
  - `capacitanceF`.
- Progress: `/setup_update`.

### Step 7: NTC params
- Set:
  - `ntcBeta`, `ntcT0C`, `ntcR0`, `ntcFixedRes`.
- Live temperature:
  - `/monitor` -> `floor.temp_c` (NTC).
- Progress: `/setup_update`.

### Step 8: Wire calibration
- Link NTC to gate:
  - POST `/control` `{ action: "set", target: "ntcGateIndex", value: gate }`.
- Start model calibration:
  - POST `/calib_start` `{ mode: "model", wire_index, target_c }`
    (backend uses defaults for duty and interval).
- Status and progress:
  - GET `/calib_status` -> `progress_pct`, `result_tau`, `result_k`,
    `result_c`, `result_wire`.
- Chart data:
  - GET `/calib_data?offset=0&count=200` -> samples with `t_ms`, `v`, `i`,
    `temp_c`, `room_c`, `ntc_v`, `ntc_ohm`, `ntc_adc`.
- Stop:
  - POST `/calib_stop`.
- Save results:
  - POST `/control` with `wireTau{n}`, `wireK{n}`, `wireC{n}`,
    `wireCalibrated{n}` = true.
  - R0 (Ohm) tile shows `wireRes[{wire_index}]` from `/load_controls`.
- Presence probe (admin-only):
  - POST `/presence_probe` `{ presenceMinRatioPct }` -> `wirePresent`,
    `calibrated`.
  - Persist ratio via `/control` target `presenceMinRatioPct`.
- Wire test:
  - POST `/wire_test_start` `{ target_c }`.
  - GET `/wire_test_status` -> `running`, `target_c`, `active_wire`,
    `ntc_temp_c`, `active_temp_c`, `purpose`.
  - POST `/wire_test_stop`.
- Progress: `/setup_update` (stage/substage/wire_index).

### Step 9: Floor calibration
- Start:
  - POST `/calib_start` `{ mode: "floor" }` (backend uses defaults and
    current NTC gate).
- Status:
  - GET `/calib_status` -> `mode`, `running`, `count`, `target_c`,
    `wire_index`.
- Chart:
  - GET `/calib_data` -> samples with `temp_c`, `room_c`, `v`, `i`.
  - Display raw sample values only; no derived calculations in the UI.
- Stop:
  - POST `/calib_stop`.
- Save results:
  - Floor model values are stored in config; read via `/load_controls`
    (`floorTau`, `floorK`, `floorC`) and set `floorCalibrated`.
- Progress: `/setup_update`.

### Step 10: Finish
- Re-check `/setup_status` for `configOk` and `calibOk`.
- POST `/setup_update` `{ setup_done: true }` when required items are complete.
- On success, allow wizard close and route to dashboard.
