# WiFiManager Implementation Guide

This document explains how the firmware’s WiFiManager works so future development can extend or debug it confidently.

## Responsibilities
- Bring up Wi‑Fi in STA (preferred) or fallback AP mode.
- Host the HTTP server and API endpoints (no static assets served).
- Manage authentication (single admin, single user).
- Stream device state (SSE) and serve telemetry snapshots.
- Serialize control commands and forward them to Device via DeviceTransport.
- Handle inactivity/heartbeat, disconnection, and Wi‑Fi restart.

## Startup Flow
1. `WiFiManager::begin()` is called during system init.
2. Creates mutex and control queue + worker task.
3. Attempts STA connect (`StartWifiSTA()`); on timeout falls back to AP (`StartWifiAP()`).
4. Registers HTTP routes, starts server, starts inactivity timer.
5. Starts snapshot task (pulls StatusSnapshot) and SSE state stream task.

## STA / AP Behavior
- **STA**: hostname `powerboard` (mDNS http service). Connects with `WIFI_STA_SSID/PASS`. On success registers routes and starts server.
- **AP**: SSID/password from NVS (`DEVICE_WIFI_HOTSPOT_NAME_KEY`/`DEVICE_AP_AUTH_PASS_KEY`). Sets static AP IP, starts mDNS, registers routes, starts server.
- AP limits max clients to 1.
- `restartWiFiAP()` cleanly stops Wi‑Fi and re-enters AP mode.

## Routes (see `WiFiManager::registerRoutes_`)
- `/login`             – legacy route; returns 404 JSON (no HTML served).
- `/device_info`       – JSON deviceId/sw/hw from NVS.
- `/connect`           – POST username/password; returns `{ok, role, token}`.
- `/disconnect`        – POST action=disconnect; clears session; returns `{ok:true}`.
- `/heartbeat`         – keeps session alive; updates inactivity timer (requires token).
- `/state_stream`      – SSE push of `{state, seq, sinceMs}` snapshots.
- `/monitor`           – telemetry snapshot (caps, temps, outputs, session stats).
- `/load_controls`     – persisted control/config values.
- `/control`           – control commands (set/get) processed by worker task.
- `/session_history`, `/History.json` – power tracker history.
- No static assets are served from SPIFFS.

## Authentication / Session
- Single active session: `wifiStatus` is NotConnected | UserConnected | AdminConnected.
- `/connect` validates against NVS creds (ADMIN_ID/PASS, USER_ID/PASS) and returns a session `token`.
- Clients must include the token on authenticated requests using `X-Session-Token` header or `?token=` query param.
- Session is bound to the client IP; other IPs are rejected even if they know the token.
- `/heartbeat` and `/disconnect` respect auth; unauthenticated hits are rejected.

## Heartbeat and Inactivity
- `heartbeat()` spawns/refreshes a periodic check; `/heartbeat` updates `lastActivityMillis` and sets `keepAlive` (token required).
- `startInactivityTimer()` watches idle time; on expiry it disconnects clients and can drop Wi‑Fi/AP per existing logic.

## State & Telemetry
- `snapshotTask` pulls `StatusSnapshot` (cap voltage, current, temps, outputs, fan, session stats).
- `stateStreamTask` reads DeviceTransport state events and pushes SSE. Frontend uses SSE for zero‑lag power/off indicators and falls back to polling if needed.

## Control Path (queued)
- `/control` deserializes JSON to `ControlCmd` (type + args) and enqueues to `_ctrlQueue`.
- `controlTaskLoop` dequeues and calls `handleControl()`.
- `handleControl` switches on `CtrlType` (relay, outputs, run/stop, fan speed, buzzer mute, resistances, voltage/frequency, access flags, reset, etc.) and **always goes through `DeviceTransport`**. ACK is returned to HTTP as `{status:"ok","applied":true}` or `{error:"apply_failed"}`.
- All direct Device access was removed; WiFiManager never touches hardware/NVS directly.

## SSE and Polling Fallback
- SSE endpoint: `stateSse` at `/state_stream`. On connect, latest state is sent immediately; subsequent events stream as JSON.
- Frontend falls back to periodic `/control` get/status when SSE closes.

## Wi‑Fi Status Flags
- `WifiState`, `prev_WifiState`, and `wifiStatus` are guarded by `_mutex`.
- `isWifiOn()` returns Wi‑Fi availability for other modules.

## Integration Points
- **DeviceTransport**: single bridge for state snapshots and commands.
- **SwitchManager**: physical buttons call transport via WiFiManager or directly as configured.
- **RGB/Buzzer**: overlays and sounds on auth/connect/disconnect outcomes.

## Adding New Endpoints or Controls
1. Define route in `registerRoutes_()`.
2. If it changes device state/config, add a new `CtrlType` and map to a `DeviceTransport` command (with ACK).
3. Update frontend to consume the new endpoint or control type.
4. Keep `/control` responses consistent (`ok`/`error`) for UI rollback.

## Testing Checklist
- STA connect → mDNS reachable at `http://powerboard.local/`.
- AP fallback works and API is reachable.
- Admin/user login succeeds and returns a token; single session enforced.
- SSE streams state to the active session IP.
- Each control action logs `[WiFi] Handling control type:X` and `[WiFi] Control result type:X ok=Y`.
- Heartbeat keeps session alive; inactivity timer disconnects as expected.

