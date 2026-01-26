# Frontend <-> Backend Linkage Implementation Plan (Fixed Scope, Additive)

This document converts `readme/uilinkage.md` into a practical, staged implementation plan.
The goal is to link the new frontend to the existing backend without breaking current behavior,
using an additive layer that can be enabled progressively. This plan has a **fixed scope**
that must be completed before any optimization work begins.

---

## Fixed Goals (must be done)
- Implement UI <-> backend linkage exactly as documented in `readme/uilinkage.md`.
- Use an additive path: new modules/services first, then incremental wiring.
- Avoid destructive changes; keep old UI logic working until each stage is verified.
- Make the integration reversible (feature flag / fallback to mock data).
- On first start, only the setup wizard is populated; keep all other UI linkage disconnected until setup is completed at least once.
- No “optional” features until all stages are complete.

---

## Principles (non-negotiable)
1) Single source of truth: backend CBOR snapshots + SSE events.
2) One shared telemetry loop for `/monitor` (no per-tab polling).
3) Additive layer first: introduce transport/cache modules before touching UI behavior.
4) Fail safe: on error, keep last known values and show stale state.
5) No breaking UI changes: only wire existing `data-*` hooks and update text/values.

---

## Stage 0 - Preparation (no behavior changes)

### Backend (no breaking changes)
- Confirm all routes and payload keys in `readme/uilinkage.md` are accurate.
- Required additions (do not change existing contracts):
  - Enable `pushLiveSample()` so `/monitor_since` returns data for high‑rate charts.
  - Keep `/monitor` as the canonical snapshot source.

### Frontend (additive scaffolding)
- Create a Linkage Layer (new module/service), but do not wire UI yet:
  - `CborClient` (CBOR fetch + error translation + auth token)
  - `SseClient` (base64(CBOR) decode + reconnection)
  - `TelemetryCache` (single `/monitor` loop + cache + stale tracking)
  - `AuthSession` (token + role + `/connect` + `/disconnect`)
  - `Heartbeat` (keep-alive when monitor loop is paused)

Deliverable: scaffolding exists, but UI still uses current behavior or mocks.

---

## Stage 1 - Transport layer (wired but hidden)

### Frontend
- Implement `CborClient`:
  - Uses `Content-Type: application/cbor`, `Accept: application/cbor`.
  - Sends `X-Session-Token` header for all authenticated requests.
  - On `401 not_authenticated`: clear token and redirect to `/login.html`.
  - Decodes CBOR and returns `{ok,data}` or `{error,detail}`.

- Implement `SseClient`:
  - Uses `EventSource("/state_stream?token=<token>")`.
  - Uses `EventSource("/event_stream?token=<token>")`.
  - Base64 decode then CBOR decode for each message.
  - Reconnect on failure (exponential backoff).

- Implement `TelemetryCache` (but keep UI disconnected from it):
  - Follows the Traffic Model in `readme/uilinkage.md`.
  - Keeps `monitorCache`, `lastMonitorOkMs`, `isStale`.

Deliverable: transport layer is functional and can log CBOR payloads to console.

---

## Stage 2 - Auth + Bootstrap wiring

### Frontend
- Wire login to `POST /connect`:
  - Store `{token, role}`.
  - Redirect to `/admin.html` (user role can reuse this shell).
  - Display login errors from `error` field.
- Wire logout to `POST /disconnect`.
- On admin.html load:
  - Fetch `/load_controls` and `/setup_status` once to decide setup-only vs full mode.
  - If setup is incomplete (setupDone/runAllowed/ready + stage checks), run setup-only mode:
    - Do not start SSE clients or the `TelemetryCache` loop.
    - Do not initialize statusbar/sidebar/tabs/overlays linkage.
    - Populate only the setup wizard UI.
  - Once setup is complete at least once, start SSE clients and `TelemetryCache` and proceed with Stage 3+ wiring.

Deliverable: login, logout, and initial data load work end-to-end.

---

## Stage 3 - Statusbar + Sidebar (core UI)

### Statusbar (always on)
Bind from `monitorCache` + SSE:
- Voltage/current/temps from `/monitor` (DC voltage = `cspVoltage`, current = `currentAcs` only; temps = 2x board + 1x heatsink, ignore NTC).
- Warnings/errors counters from `/event_stream` (fallback `/monitor.eventUnread`).
- Mode pill from `/state_stream` plus active calibration/test.
- Stale indicator when cache is stale.

### Sidebar
Wire actions to `/control`:
- Mute -> `buzzerMute`
- Power -> `systemWake`, `systemStart`, `systemShutdown`
- Reset -> `systemReset`
- Restart -> `reboot`

Deliverable: statusbar + sidebar actions reflect real device state.

---

## Stage 4 - Tabs (Dashboard/User/Control/Admin/Device)

### Dashboard
- Read-only telemetry from `monitorCache`.
- Electrical: `cspVoltage` + `currentAcs` for gauges; temps show 2x board + 1x heatsink (leave the last tile off).
- LED feedback toggle -> `/control` `ledFeedback`.

### User tab
- Credentials -> `/control` `userCredentials`.
- Output access flags -> `/control` `AccessN`.

### Control tab
- Fan speed -> `/control` `fanSpeed`.
- Relay -> `/control` `relay`.
- Outputs -> `/control` `outputN`.
- Respect gating from `outputAccess` + `wirePresent`.

### Admin tab
- Admin credentials -> `/control` `adminCredentials`.
- Station Wi-Fi -> `/control` `wifiSSID` / `wifiPassword`.
- AP Wi-Fi -> `POST /ap_config`.

### Device tab
- Read from `/load_controls`.
- Write via `/control` targets listed in `uilinkage.md`.

Deliverable: all tab edits round-trip to the backend.

---

## Stage 5 - Overlays (Warnings/Errors/History/Log)

### Warnings/Errors overlays
- Initial seed from `GET /last_event`.
- Live updates from `/event_stream`.
- Mark read on open: `GET /last_event?mark_read=1`.

### History overlay
- Load on open from `/session_history`.

### Log overlay
- Load on open from `/device_log`.
- Clear via `/device_log_clear`.

Deliverable: overlays are fully functional and synchronized with backend state.

---

## Stage 6 - Setup Wizard (progress + gating)

### Core
- Poll `/setup_status` every 500-1000 ms while wizard open.
- Persist wizard progress with `POST /setup_update`.
- Reset wizard with `POST /setup_reset` (admin-only).

### Step 5: sensor zero + presence
- Run `POST /control` target `calibrate` for cap + ACS zero.
- Run `POST /presence_probe` with optional `presenceMinDropV`.
- Respect "wire required for discharge" rule.

### Step 8/9: wire + floor calibration
- Use `/calib_*` (status + data) and `/wire_test_*` where applicable.
- Update wizard status from `/setup_status` results.

Deliverable: wizard reflects backend progress and gating states.

---

## Stage 7 - Calibration overlay (advanced)

- Use `/calib_status`, `/calib_start`, `/calib_stop`, `/calib_clear`.
- Stream samples via `/calib_data` (incremental offset).
- Wire test: `/wire_test_start/stop/status`.
- NTC calibration: `/ntc_calibrate`, `/ntc_cal_status`, `/ntc_cal_stop`, `/ntc_beta_calibrate`.

Deliverable: calibration overlay fully functional.

---

## Optimization Phase (after Stage 0–7 are complete)

Only after all stages are complete and verified, evaluate performance tuning:
- Consider using `/monitor_since` for high‑rate charts in Live mode.
- Consider a tiny statusbar endpoint if `/monitor` proves too heavy.
- Consider a config-change event to avoid unnecessary `/load_controls` refresh.

---

## Validation Checklist (per stage)

- Stage 2: login works, SSE connects, `/monitor` returns values.
- Stage 3: statusbar updates within 1s; power/mute/reset actions affect device state.
- Stage 4: saving settings updates backend; UI refreshes from `/load_controls`.
- Stage 5: warnings/errors show from `/last_event` + live SSE.
- Stage 6: wizard progress persists across refresh; setup gating enforced.
- Stage 7: calibration data streams and saves as expected.

---

## Rollback Plan

Each stage is additive and can be disabled by a single flag (e.g., `linkage.enabled`):
- If errors occur, disable linkage and revert to mock/offline UI.
- Keep old UI behavior intact until each stage is verified.

---

## References
- `readme/uilinkage.md` (source of truth for behavior + payload contracts)
- `readme/TransportLayer.md` (lower-level transport notes)
- `readme/SetupWizardGuide.md` (wizard UX expectations)
