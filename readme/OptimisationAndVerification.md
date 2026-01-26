# Optimisation and Verification (Frontend ↔ Backend Linkage)

This document defines the **checks and optimisation steps** required to confirm the UI and firmware are fully linked and behaving correctly. It is used **after Stage 0–7** are implemented, and before any further feature expansion.

References:
- `readme/uilinkage.md` (behavior + payload contracts)
- `readme/linkageImplementation.md` (stage plan and deliverables)
- `readme/TransportLayer.md`
- `readme/SetupWizardGuide.md`

---

## Scope

This checklist verifies:
- **Transport** (CBOR over HTTP + SSE) and **session/auth**.
- **Telemetry loop** (single `/monitor` loop, caching, stale handling, heartbeat).
- **UI behavior** (tabs, overlays, wizard, calibration, gating rules).
- **Localization** (backend-provided strings and frontend language synchronization).
- **Data integrity** (writes round-trip, persistence, and refresh correctness).
- **Optimisation** (traffic, rendering, and backend load).

---

## Preflight (must pass before verification)

- Device reachable from browser and returns `/heartbeat` = `alive`.
- Firmware implements all endpoints referenced in `readme/uilinkage.md`.
- UI is running the **real linkage layer** (no mock data).
- Backend language key exists and `/load_controls.uiLanguage` returns `en|fr|it`.
- System clock is correct (or RTC sync will be performed at login).

---

## Verification Checklist (Frontend + Backend)

### 1) Transport & Auth
- `POST /connect` returns `{ ok:true, role, token }` in CBOR.
- All authenticated requests include `X-Session-Token` (or `?token` for SSE).
- `/state_stream` and `/event_stream` deliver base64(CBOR) and reconnect on drop.
- `401 not_authenticated` forces token clear + redirect to `login.html`.
- Single-session behavior is enforced and surfaced in the UI error feedback.

### 2) Telemetry Loop & Stale Handling
- Only **one** `/monitor` loop exists in the app.
- Polling rates match the Traffic Model:
  - Idle: 1000 ms
  - Live tab/overlay: 250 ms
  - Wizard open: 1000 ms + `/setup_status` 500–1000 ms
  - Calibration overlay: 1000 ms + `/calib_status` 500–1000 ms
  - Hidden tab: `/monitor` paused, `/heartbeat` every ~6000 ms
- `monitorCache` updates all UI surfaces (statusbar, dashboard, live, overlays).
- Stale state triggers if no successful `/monitor` within 3s; indicator is visible.

### 3) Statusbar + Sidebar
- Voltage/current/temps reflect `/monitor` values.
- Warning/error counters match SSE `event` stream and `/last_event` seed.
- Power button transitions are correct based on `/state_stream` state.
- Mute, reset, restart actions call `/control` targets and reflect new state.

### 4) Tabs (Dashboard/User/Control/Admin/Device)
- Every tab change that writes data calls `/control` or `/ap_config` and waits for refresh from `/load_controls` or `/monitor`.
- No optimistic UI updates persist without backend confirmation.
- Output access gating is enforced everywhere:
  - Disallowed outputs cannot be toggled ON.
  - Disallowed outputs are not selectable for NTC or calibration actions.

### 5) Overlays (Warnings/Errors/History/Log)
- Warnings/errors overlays seed from `/last_event` and update from `/event_stream`.
- Opening overlays marks entries as read and resets counters.
- History overlay loads from `/session_history`.
- Log overlay loads from `/device_log` and clears via `/device_log_clear`.

### 6) Setup Wizard (Progress + Gating)
- `/setup_status` polling drives all step indicators and gating.
- Wizard progress persists via `/setup_update`.
- Reset uses `/setup_reset` and all flags are cleared in UI state.
- Step 5: `POST /control` `calibrate` and `/presence_probe` work; UI blocks when no wire is connected.
- Step 8/9: calibration runs reflect `/calib_status` + `/calib_data`, and wire test endpoints are usable.

### 7) Calibration Overlay
- `/calib_start`, `/calib_status`, `/calib_data`, `/calib_stop`, `/calib_clear` work end-to-end.
- `/wire_test_*` endpoints are callable and update overlay state.
- `/ntc_calibrate`, `/ntc_cal_status`, `/ntc_cal_stop`, `/ntc_beta_calibrate` work and display results.

### 8) Localization (End-to-End)
- `/load_controls.uiLanguage` initializes the UI language if no local value exists.
- Changing UI language updates localStorage and sends `/control` `uiLanguage`.
- Backend error strings (`error`, `reason`) are localized for `en|fr|it`.
- Event stream `reason` matches the selected device language.

### 9) Persistence & Recovery
- After refresh, the UI restores from backend state, not cached UI state.
- If SSE disconnects, UI keeps last values and reconnects automatically.
- If backend rejects the session, UI returns to login and keeps language.

---

## Optimisation Checklist (traffic + performance)

### Frontend Optimisation
- Live mode uses `/monitor_since` for charts, with full `/monitor` at 2000 ms.
- `/load_controls` is fetched only on login and after successful `POST /control` writes.
- All control writes batch into a single `/control` request per action.
- DOM updates are throttled to the monitor tick (no per-element timers).
- Chart updates skip frames when the UI is hidden or minimized.

### Backend Optimisation
- `/monitor` payload stays stable and only includes required keys.
- `/monitor_since` buffer is enabled and bounded.
- SSE event reasons are short, localized, and free of formatting errors.
- `/event_stream` sends only deltas after initial snapshot.

### Traffic Budget Targets
- Idle: ≤ 1 request/s (`/monitor`) + SSE steady.
- Live: ≤ 4 requests/s (`/monitor_since`) + 1 request/2s (`/monitor`).
- Wizard open: `/setup_status` ≤ 2 requests/s.
- Calibration open: `/calib_status` ≤ 2 requests/s and `/calib_data` paged on demand.

---

## Acceptance Criteria (pass/fail)

The linkage is considered **complete** when all are true:
- All verification items pass without mocks.
- No tab or overlay runs its own `/monitor` polling.
- All UI controls round-trip to backend and refresh from live data.
- Localization is consistent for all backend-provided strings.
- Traffic rates remain within the defined budget in each UI mode.

---

## Issue Logging Template

Use this format for each failed check:

- **Area**: (e.g., Statusbar, Wizard Step 5, SSE Event)
- **Symptom**: (what fails)
- **Endpoint**: (route + method)
- **Payload**: (CBOR keys + values)
- **Expected**: (what should happen)
- **Observed**: (what happens)
- **Notes**: (screenshots/logs)
