# UI Linkage Guide (Frontend Behavior)

This document describes how the web UI should behave for the Power Distribution Board (floor-heating controller with integrated calibration).

It is intentionally frontend-first: it describes the UI state model, screens, overlays, and notifications the UI must present. It does not implement linkage yet, but it does define the CBOR transport expectations and the minimal payload keys the UI consumes so backend linkage can be added quickly.

---

## Scope and goals

### In scope
- UI behavior rules: what is shown, when controls are enabled/disabled, and how feedback is presented.
- Expected UI states for: setup, calibration, running, idle, and off.
- Expected notification/alert behavior (warnings/errors/toasts/logs/history).
- Mapping the guide to the current frontend structure under `FlutterApp/adminapp/assets/`.

### Out of scope (for now)
- Implementing the linkage code between the new UI and the backend (fetch/EventSource wiring).
- Full API documentation for every route (this document focuses on what the UI must show/control and the minimal payload contracts to wire it quickly).

---

## Transport: CBOR everywhere (HTTP + SSE)

The UI talks to the board using **CBOR payloads**.

### Content type and auth
- HTTP request and response payloads are CBOR (`Content-Type: application/cbor`, `Accept: application/cbor`).
- Production backend expects CBOR. Any JSON handling in frontend scripts should be treated as a development/mock fallback only.
- After login, the UI must send the session token on every authenticated request:
  - Header: `X-Session-Token: <token>`
  - (Fallback supported by backend) query param: `?token=<token>`
- Important SSE note: browser `EventSource` cannot send custom headers, so `/state_stream` and `/event_stream` must use `?token=<token>` (or a polyfill that supports headers).
- Session/IP note: the backend ties the session token to the client IP; SSE clients are rejected if the IP does not match the active session.

### Non-CBOR exceptions (intentional)
Some endpoints are not CBOR because they serve browser/static primitives:
- `GET /device_log` returns `text/plain` (debug log output)
- `GET /heartbeat` returns `text/plain` (`alive`)
- `GET /favicon.ico` returns `204`
- Static assets are served from:
  - `/css/`, `/js/`, `/icons/`, `/fonts/`

### Login payload (CBOR)
- `POST /connect` body CBOR map:
  - `username` (text)
  - `password` (text)
- `200 OK` response CBOR map:
  - `ok` (bool)
  - `role` (text: `admin|user`)
  - `token` (text)

### Device info payload (CBOR)
- `GET /device_info` response CBOR map (no auth; used by the login screen):
  - `deviceId` (text)
  - `sw` (text)
  - `hw` (text)

### Logout payload (CBOR)
- `POST /disconnect` body CBOR map:
  - `action` (text: `"disconnect"`)
- Response CBOR map:
  - `ok` (bool)

### Error payload (CBOR)
When a request fails, the backend responds with a CBOR map that contains:
- `error` (text, localized to the device UI language when available)
- optional `detail` (text)
- optional `state` (text; used when the error is state-related)

### Control requests (CBOR)
Most UI interactions that change device configuration or state are performed through:
- `POST /control` body CBOR map:
  - `action` (text: usually `set` or `get`)
  - `target` (text: identifies what to change/read)
  - optional `value` (bool/int/float/text depending on target)
  - optional `epoch` (uint; when provided, the device RTC can be synchronized)

Responses are CBOR maps:
- On success:
  - `{ status: "ok", applied: true }` for immediate changes
  - `{ status: "ok", queued: true }` for async queued changes
- On failure: error payload (see above)

Important UI rule: after `applied` or `queued`, the UI should refresh from `/load_controls` (for config) and `/monitor` (for telemetry) instead of assuming the value changed.

### SSE streams use base64(CBOR)
The board exposes EventSource streams:
- `GET /state_stream` (event name: `state`)
- `GET /event_stream` (event name: `event`)

Each SSE `event.data` is a **base64-encoded CBOR map**:
1) base64 decode `event.data` into bytes
2) CBOR decode bytes into an object/map

`/state_stream` CBOR keys:
- `state` (text: `Idle|Running|Shutdown|Error`)
- `seq` (uint)
- `sinceMs` (uint)

`/event_stream` CBOR keys:
- On connect: `kind="snapshot"` plus:
  - `unread`: `{ warn: uint, error: uint }`
  - optional `last_warning`: `{ reason: text, ms?: uint, epoch?: uint }`
  - optional `last_error`: `{ reason: text, ms?: uint, epoch?: uint }`
- On new notice: `kind="warning"` or `kind="error"` plus:
  - `reason` (text)
  - optional `ms` (uint) and/or `epoch` (uint)
  - `unread`: `{ warn: uint, error: uint }`

Note: `reason` strings are already localized server-side based on the device UI language (`/load_controls.uiLanguage`).

---

## Current frontend structure (what exists today)

The UI shell is defined in `FlutterApp/adminapp/assets/admin.html` and is composed at runtime by loading partial HTML snippets via `data-include`.

Note:
- The new UI asset set currently provides `admin.html` (and login pages) but no separate `user.html`. The user role can still reuse the same shell while hiding admin-only tabs/features based on the `role` returned by `POST /connect`.

### Main layout blocks
- Sidebar: `FlutterApp/adminapp/assets/admin/partials/layout/sidebar.html`
  - Tabs: Dashboard / User / Control / Admin / Device / Live
  - Quick actions: Mute, Power, Reset, Restart
- Statusbar: `FlutterApp/adminapp/assets/admin/partials/layout/statusbar.html`
  - Device indicator, connection mode (AP/Station), warnings/errors counters
  - Board temp, sink temp, DC voltage, DC current
  - Mode pill + Live shortcut button
  - Session toggle menu
- Tab panels: `FlutterApp/adminapp/assets/admin/partials/tabs/*.html`
  - `dashboard.html`, `user.html`, `control.html`, `admin.html`, `device.html`, `live.html`
- Overlays (modal/panel UI): `FlutterApp/adminapp/assets/admin/partials/overlays/*.html`
  - Live overlay, Warnings overlay, Errors overlay, History overlay, Log overlay, Calibration overlay
  - Global toast + confirm live under: `FlutterApp/adminapp/assets/admin/partials/layout/overlays.html`
- Setup Wizard: `FlutterApp/adminapp/assets/admin/partials/wizard/setup-wizard.html`
  - 10-step guided initial configuration, with required and skippable steps

---

## Terminology / UI mental model

- Outputs / Wires: up to 10 heating outputs (shown as L1-L5 and R1-R5 in the Live tab).
- NTC: temperature sensor used during setup and calibration. In wire calibration, the NTC is attached to a selected wire gate.
- Run session: normal heating operation with energy/time metrics.
- Calibration session: actions that tune sensors/models and produce parameters (wire calibration, floor calibration, sensor zeroing, etc.).
- Presence probe: determines which outputs are physically connected.

---

## Session, roles, and login (frontend behavior)

### Login flow
- `FlutterApp/adminapp/assets/login.html` is the entry page.
- On successful login, the UI transitions to `FlutterApp/adminapp/assets/admin.html`.
- Login failures must show a clear reason and provide a retry path.
- When served by the device HTTP server, the expected pages are:
  - `/login` or `/login.html` (login UI)
  - `/admin.html` (main UI shell)
  - `/user.html` (optional user UI shell; may reuse `admin.html` with role-based tab hiding)
  - `/login_failed.html` (optional fallback page)
- Backend login constraints the UI must surface clearly:
  - Single-session behavior: if another client is already connected, `POST /connect` fails with `error="Already connected"`.
  - User role gating: user login is blocked until setup is complete (`error="setup_required"`).

### Roles
- The UI must treat roles consistently:
  - `admin`: full access to all tabs and calibration tools.
  - `user`: restricted access (at minimum, should not access admin-level credential/wifi settings).
- The current role is displayed in the statusbar (session toggle).

### Session menu
- The statusbar session menu must provide:
  - a shortcut to "Set Credential" (navigates to Admin tab)
  - Logout (returns to login page)
- On session expiration/invalid session, the UI must:
  - clear local auth state (token/cookie/etc.),
  - redirect to `login.html`,
  - preserve the selected language (localStorage).

### Session keep-alive (important)
- The backend runs a heartbeat watchdog and may disconnect clients after ~tens of seconds of no keep-alive/activity (when the device is not busy running and setup is complete).
- To avoid unexpected disconnects:
  - either keep polling `GET /monitor` (which refreshes the keep-alive), or
  - call `GET /heartbeat` periodically (recommended ~5-6s) when monitor polling is paused (e.g., during heavy UI work).

---

## Time and RTC sync (frontend expectation)

- The dashboard displays a local clock and date.
- When connected to a device, the UI should ensure the device RTC is synchronized (at least once per session, and again if drift is detected).
- History/log timestamps should be consistent with the time basis used by the device (avoid mixing local time and device time without labeling).

---

## App bootstrap and initial sync (admin.html)

When `admin.html` loads, the UI must start by loading state/config from the device so all tabs and overlays reflect reality.

### Startup sequence (recommended)
1. Load includes (`data-include`) and initialize i18n.
2. Validate that a session token exists; if missing, redirect to `login.html`.
3. Subscribe to SSE (base64(CBOR)):
   - `EventSource("/state_stream?token=<token>")` to track `Idle|Running|Shutdown|Error` transitions.
   - `EventSource("/event_stream?token=<token>")` to receive warnings/errors and unread counters.
4. Fetch initial device data (CBOR) and populate UI:
   - `GET /load_controls` (persisted config + key flags like `setupDone`, `setupRunAllowed`).
   - `GET /setup_status` (setup progress + missing keys; used to open the wizard and point to incomplete steps).
   - `GET /monitor` (telemetry snapshot + session stats for statusbar/dashboard/live tab).
5. Start the telemetry loop using the Traffic Model section below (adaptive intervals by UI mode).

### Redirect and auth failure rules
- Any `401 not_authenticated` response must:
  - clear the saved token,
  - redirect to `login.html`,
  - keep the selected UI language (localStorage `ui.lang`).

### Loading and stale-data rules
- Until `/load_controls` and the first `/monitor` succeed:
  - show `--` placeholders in statusbar/dashboard/live,
  - disable Save buttons (and show "Connecting..." or "Loading...").
- If monitor data becomes stale (no successful `/monitor` refresh for a timeout window), show a visible "disconnected/stale" state in the statusbar indicator.

---

## Traffic model (explicit, implementation-ready)

This section defines the exact traffic behavior the UI must implement, based on the current frontend structure and backend constraints.

### Core principle: one shared telemetry loop
- There must be a single monitor fetch loop for the entire UI.
- All widgets (statusbar, dashboard, live tab, overlays) render from the same cached snapshot.
- No tab/overlay may start its own `/monitor` polling.

### Always-on channels
1) **State SSE** (`/state_stream?token=<token>`)
   - Always connected while authenticated.
   - Drives global state (`Idle|Running|Shutdown|Error`), mode pill, and power button state.
2) **Event SSE** (`/event_stream?token=<token>`)
   - Always connected while authenticated.
   - Drives warnings/errors counters and updates the warning/error overlays.

### Telemetry modes (exact rates and rules)
The UI must dynamically switch polling rates based on visibility and active UI context:

| Mode | When active | `/monitor` interval | Extra polling |
|------|-------------|---------------------|---------------|
| **Idle** | any tab, no live overlay, wizard closed | **1000 ms** | none |
| **Live** | Live tab or Live overlay open | **250 ms** (current firmware) | optional `/monitor_since` if enabled |
| **Wizard** | Setup wizard open | **1000 ms** | `/setup_status` every **500-1000 ms** |
| **Calibration** | Calibration overlay open | **1000 ms** | `/calib_status` every **500-1000 ms** and incremental `/calib_data` |
| **Hidden** | page/tab not visible | **paused** | `GET /heartbeat` every **6000 ms** |

Rules:
- If multiple modes apply, choose the **fastest** interval.
- If the page is hidden, **pause `/monitor`** and keep the session alive with `/heartbeat`.
- When leaving a faster mode (e.g. Live overlay closed), return to the Idle interval.

### Stale data rules
- If `/monitor` has not succeeded within **3 seconds**, mark the statusbar indicator as "stale".
- Continue to show the last known values (do not clear unless disconnected).
- On repeated failures, back off the polling interval to **2000 ms**, retry 3 times, then show a reconnect banner.

### Monitor cache behavior
- The last good `/monitor` payload is stored as `monitorCache`.
- Rendering logic always uses `monitorCache` and should never block the UI on a new request.
- Each successful `/monitor` refresh updates:
  - statusbar (voltage/current/temps + connection pills),
  - dashboard telemetry,
  - live tab wiring view,
  - live overlay chart (if open).

### Keep-alive handling
- If `/monitor` is paused or slowed (hidden mode), the UI **must** call `GET /heartbeat` every ~6000 ms.
- Any successful `/monitor`, `/control`, `/setup_status`, `/calib_*` call implicitly refreshes activity and can skip one heartbeat tick.

### Implementation outline (exact logic)
1) Maintain a global state object:
   - `isAuthenticated`, `isVisible`, `isLiveOpen`, `isWizardOpen`, `isCalibOpen`
   - `monitorIntervalMs`, `lastMonitorOkMs`, `monitorCache`
2) On any UI state change, compute the desired `monitorIntervalMs` using the table above.
3) If the interval changes, restart the single monitor timer with the new interval.
4) Each monitor tick:
   - call `GET /monitor` (CBOR),
   - on success: update cache + timestamp,
   - on failure: mark stale, back off after 3 consecutive failures.
5) Heartbeat timer:
   - runs only when `isVisible == false` **or** `monitorIntervalMs > 2000`,
   - calls `GET /heartbeat` every ~6000 ms.

### Optional optimization (backend-ready, small change)
If `pushLiveSample()` is enabled in firmware:
- Use `/monitor_since?seq=<lastSeq>` at **250 ms** in Live mode.
- Keep full `/monitor` at **2000 ms** in Live mode (statusbar + non-live data).
- This reduces payload size and frees CPU on both sides.

---

## Global interaction rules (important)

### UI must reflect the device state (no "fake saved")
- When the user changes settings, the UI should:
  1) send the change request,
  2) show a pending state (disable the form / show spinner / show "Applying..."),
  3) update the UI only when the device confirms (ACK / refreshed snapshot).
- Avoid optimistic updates that can diverge from the real device configuration.

### Lock editing during critical operations
When the device is:
- Running (auto loop),
- Calibrating,
- Executing a test,
- Performing sensor zero actions,

...the UI must prevent parameter edits across all configuration tabs and wizard steps that would conflict. The user must clearly see why controls are locked and what to do (e.g., "Stop running to edit settings").

### Allowed outputs gate everything
- The allowed outputs list is not cosmetic:
  - Presence probing should focus on allowed outputs (or at minimum highlight allowed vs disallowed).
  - NTC linking and calibration actions must respect the allow list.
  - Live and manual control should emphasize allowed outputs; disallowed outputs should not be startable by normal operators.

### Linked parameters and reactive UI behavior (critical)
Many UI controls are not independent. When one parameter changes, the UI must immediately re-evaluate and update any other controls/behaviors that depend on it.

Rules:
- Do not wait for a page reload. Apply dependency updates as soon as the backend confirms the change (after an ACK + refreshed `/load_controls` and/or `/monitor`).
- While a change is pending, the UI may temporarily disable dependent controls to avoid conflicting actions; on failure it must revert to the previous behavior.
- Always use the latest server-confirmed state as the source of truth (no permanent optimistic updates).

Required dependency examples (must be implemented):
- Output access (`/load_controls.outputAccess.outputX`):
  - If an output becomes disallowed: disable its manual ON/OFF control in the Control tab and prevent it from being started by user-level actions.
  - If an output becomes allowed: enable its manual ON/OFF control and allow it to be selected in "allowed outputs" UI lists.
  - Presence/probing and any "select wire index/gate" choices must respect allowed outputs (disallow selecting gates for outputs that are not allowed).
- Setup gating (`/load_controls.setupRunAllowed`, `/setup_status.runAllowed`):
  - If RUN is not allowed: the sidebar Power button must not transition to RUN and the dashboard must show a persistent "setup incomplete" warning.
  - When RUN becomes allowed: enable the Power button RUN action and clear the persistent warning.
- NTC gate link (wizard "Link NTC", or device config `NTC_GATE_INDEX`):
  - The UI must prevent linking the NTC to a disallowed output gate and must show a wizard toast/instruction when blocked.
  - When the link changes: update any UI that shows "active wire / target wire / wire index" for calibration/test contexts and ensure charts/labels reflect the new gate.
- Calibration apply/save (wizard Save parameters, calibration overlay Stop & Save):
  - After the device saves calibration results, immediately refresh and update:
    - dashboard "Cal OK / Cal Pending" status
    - wizard step statuses/results fields (tau/k/C, done/running flags)
    - any gating that depends on "calibration complete" (e.g., setup readiness and warnings)
  - If the save fails: keep the UI in "pending/unsaved" state and show a clear error in the wizard toast or calibration overlay alert.

---

## Localization (English / French / Italian)

The UI already supports i18n through `data-i18n*` attributes and a language selector.

### Language selection
- Language selector exists in Setup Wizard step 1 (`data-language-select`).
- Language is persisted in the browser (localStorage key `ui.lang`).
- All visible labels must update instantly when language changes:
  - `data-i18n` text
  - placeholders (`data-i18n-placeholder`)
  - aria labels (`data-i18n-aria`)
  - titles (`data-i18n-title`)

Device-side language (backend)
- The device persists a UI language code in NVS (`UI_LANGUAGE_KEY` / `/load_controls.uiLanguage`), normalized to `en|fr|it`.
- The backend uses this value to localize error strings and event reasons in CBOR responses (e.g. `/last_event`, `/event_stream`).
- The UI can update the device language via `POST /control`:
  - `{ action:"set", target:"uiLanguage", value:"fr" }` (also accepts `target:"language"`)

### Dynamic messages (alerts, notifications, logs)
- UI should support dynamic feedback in a way that can be localized. Recommended format for dynamic events:
  - `level`: `ok | info | warn | err`
  - `key` + optional `vars` (preferred), with a `fallback` string if needed
  - `time` (already formatted) or a raw timestamp (then UI formats it)

This keeps the UI in control of wording per language, while still allowing fallback strings during early integration.

---

## Global feedback and notifications (what the UI must show)

### 1) Statusbar counters + "new warning/error" banner
- Statusbar shows warning and error counters at all times.
- When the count increases, show a short banner ("New warning" / "New error") via the statusbar alert area.

### 2) Warning/Error overlays (detail views)
- Warnings overlay and Errors overlay show:
  - Current (most recent) item: message + time
  - Last 10 list (history)
- Initial population should come from `GET /last_event` (CBOR):
  - `warnings[]` and `errors[]` arrays provide up to 10 items each (each item has `reason` + optional timestamps)
  - `unread` provides counts, and `last_error` / `last_stop` provide the most recent stop/error details
  - the UI may call `GET /last_event?mark_read=1` when the user opens the overlay (to mark history as read)
- Live updates should come from `/event_stream`:
  - use the initial `kind="snapshot"` message to seed unread counts + last entry if `/last_event` was not fetched yet
  - append new `kind="warning"` / `kind="error"` notices as they arrive
  - keep a rolling buffer of 10 per kind
- Overlays must be dismissible via:
  - close button
  - clicking the overlay background
  - `Esc`

### 3) Toasts (short confirmations)
- Use toasts for short, non-blocking confirmations:
  - Saved
  - Restart requested
  - Calibration started/stopped
  - Linked NTC to Gate X

### 4) Confirm bar (dangerous actions)
- Use confirm bar for irreversible/destructive actions:
  - reset configuration
  - clear calibration data
  - clear logs/history

### 5) Dashboard notifications list (summary cards)
- Dashboard includes a notifications list for persistent "what needs attention" items, e.g.:
  - Setup incomplete / required steps pending
  - Calibration pending / recommended
  - Missing output(s) / open circuit detected
  - Connectivity status changes (AP/Station)

These should be more descriptive than the statusbar counters and should remain visible until resolved.

---

## Statusbar (what it shows and how it is driven)

The statusbar is always visible and must remain consistent across all tabs and overlays.

### Values shown (and source)
- Device status indicator (`data-sb="indicator"`):
  - `ok` when authenticated and monitor data is fresh
  - `warn` when connected but degraded (stale monitor, calibration pending, etc.)
  - `err` when disconnected/unauthenticated or the device is in `Error` state
- Connection mode pill (`data-sb="link-pill"`):
  - driven by `/monitor` keys `wifiSta`, `wifiConnected`, optional `wifiRssi`
  - shows AP vs Station mode and should update immediately on changes
- Warnings/errors counters (`data-sb="warn-count"`, `data-sb="err-count"`):
  - driven by `/event_stream` `unread` (preferred), with `/monitor.eventUnread` as fallback
  - clicking opens the correct overlay (`warnings.html` or `errors.html`)
- Temperatures:
  - board (`data-sb="board-temp"`) from `/monitor.boardTemp`
  - heatsink (`data-sb="sink-temp"`) from `/monitor.heatsinkTemp`
- Electrical telemetry:
  - DC voltage (`data-sb="dc-voltage"`) from `/monitor.capVoltage`
  - DC current (`data-sb="dc-current"`) from `/monitor.current` (already selected by firmware); `/monitor.currentAcs` is optional debug/secondary source
- Mode pill + Live shortcut:
  - Mode (`data-sb="mode-pill"` / `data-sb="mode-label"`) is derived from the current activity context:
    - `Running` when `/state_stream.state == "Running"`
    - `Idle` / `Off` when `/state_stream.state == "Idle|Shutdown"`
    - Calibration/Test modes should override the label when active (wire test, model calibration, floor calibration, etc.; derived from their status endpoints)
  - Live shortcut (`data-sb="live-btn"`) is visible when a non-idle mode is active and opens the Live overlay

### Statusbar alert banner ("New warning"/"New error")
- The short banner inside the content area (`data-sb="alert"`, `data-sb="alert-text"`) is used for:
  - "New warning" when warn unread increases
  - "New error" when error unread increases
  - short system notices (optional), but avoid using it for long explanations

### Session menu
- Session toggle shows the current role and opens the session menu overlay.
- Session actions:
  - "Set Credential" navigates to Admin tab (credential form)
  - "Logout" calls `POST /disconnect` and returns to `login.html`

---

## Sidebar quick actions (expected behavior)

- Mute:
  - toggles buzzer/attention cues (device-side), but must not hide warnings/errors counters.
  - must reflect state via `aria-pressed` and a visible "muted" state.
  - linkage: `POST /control` -> `{ action:"set", target:"buzzerMute", value:true|false }`
- Power button:
  - reflects the global device state:
    - `OFF` = device is in `Shutdown` (`/state_stream.state == "Shutdown"`)
    - `IDLE` = device is awake but not running (`/state_stream.state == "Idle"`)
    - `RUN` = device is running (`/state_stream.state == "Running"`)
  - must be blocked when the setup wizard is not complete and RUN is not allowed:
    - use `/load_controls.setupRunAllowed` to gate RUN
  - must be blocked during critical calibration steps when transitions are unsafe
  - linkage:
    - OFF -> `POST /control` `{ action:"set", target:"systemWake" }`
    - IDLE -> `POST /control` `{ action:"set", target:"systemStart" }` (only when RUN allowed)
    - RUN -> `POST /control` `{ action:"set", target:"systemShutdown" }`
- Reset:
  - requests a safe reset of system configuration/state (device-side reset flag + restart)
  - should always require confirmation (confirm bar)
  - linkage: `POST /control` `{ action:"set", target:"systemReset" }`
- Restart:
  - requests a device restart/reboot
  - should always require confirmation and must prepare the user for a brief disconnect/reconnect
  - linkage: `POST /control` `{ action:"set", target:"reboot" }`

Important distinction:
- Sidebar Reset/Restart are device actions (`/control` targets above).
- Setup Wizard reset is `POST /setup_reset` (admin-only) and resets wizard progress + calibration flags.

Tab buttons (Dashboard/User/Control/Admin/Device/Live) must always remain available for navigation, but editing inside tabs can be locked while running/calibrating.

---

## Tabs (expected behavior)

### Dashboard tab
- Purpose: high-level overview + "is it safe/ready to run?"
- Must show:
  - System readiness (ready/not ready)
  - Current mode (off/idle/running/calibration)
  - Calibration status (pending/ok)
  - Electrical telemetry: voltage, current, capacitance
  - Thermal telemetry: board temps + heatsink temp (and any extra channels)
  - LED feedback toggle (device-side feature)
- Linkage:
  - LED feedback toggle: `POST /control` `{ action:"set", target:"ledFeedback", value:true|false }`
- Must include quick entry points:
  - Open Setup Wizard
  - Open Calibration overlay
  - Open Log overlay

### User tab
- Account: change password (requires current password).
- Output Access: operator-facing allow list.
- Save/reset actions must respect the "no edits during critical operations" rule.
- Linkage:
  - Change username/password: `POST /control`:
    - `{ action:"set", target:"userCredentials", value:{ current, newId?, newPass? } }`
  - Output access flags: `POST /control`:
    - `{ action:"set", target:"AccessN", value:true|false }` where `N=1..10`

### Control tab (manual)
- Manual controls (fan speed slider, relay toggle).
- Manual output toggles list.
- Entering manual control should clearly indicate it overrides/aborts auto control (UI already warns about this).
- Linkage:
  - Fan speed: `POST /control` `{ action:"set", target:"fanSpeed", value:0..100 }`
  - Relay: `POST /control` `{ action:"set", target:"relay", value:true|false }`
  - Outputs: `POST /control` `{ action:"set", target:"outputN", value:true|false }` where `N=1..10`
- Manual output toggles must be reactive to linked settings:
  - If an output is not allowed (`outputAccess.outputX == false`), its toggle must be disabled and attempting to enable it must be blocked in the UI.
  - If an output is not physically present (`/monitor.wirePresent[X-1] == false`), its toggle must be disabled (or treated as "no wire") and show a clear status.

### Admin tab
- Admin credential changes (requires current admin password).
- Wi-Fi Station and AP configuration forms.
- Save actions must show ack/pending states and failure feedback.
- Linkage:
  - Admin credentials (and optional Station Wi-Fi update):
    - `POST /control`:
      - `{ action:"set", target:"adminCredentials", value:{ current, username?, password?, wifiSSID?, wifiPassword? } }`
  - Station Wi-Fi:
    - `POST /control` `{ action:"set", target:"wifiSSID", value:"..." }`
    - `POST /control` `{ action:"set", target:"wifiPassword", value:"..." }`
  - AP credentials (admin-only):
    - `POST /ap_config` `{ apSSID?, apPassword? }`

### Device tab
- Device configuration for sampling, safety limits, floor/nichrome parameters, and energy controls.
- Contains Save/Reset buttons (common components).
- All validation must be explicit (min/max, units, and required fields).
- Linkage:
  - Read initial values from `GET /load_controls`.
  - Apply edits via `POST /control` using the "Device config" targets listed in the backend contracts section.

### Live tab
- Purpose: wiring view + real-time status of each output + run session KPIs.
- Must show:
  - Output connection state legend (ON/OFF/No wire)
  - Per-output temperature (or "--" when not available)
  - AC input connected indicator + input relay indicator
  - Current session summary metrics: energy, duration, peak power, peak current
- Must provide actions to open overlays:
  - History overlay
  - Calibration overlay
  - Live overlay (focused chart)
  - Error overlay shortcut
  - Log overlay

#### Live tab: wiring/state mapping (from `/monitor`)
- Wire presence: `/monitor.wirePresent[0..9]`
  - `false` => show `data-state="nowire"` and temperature `--`
  - `true` => show `data-state="off"` unless energized
- Energized outputs: `/monitor.outputs.output1..output10`
  - `true` and present => show `data-state="on"`
- Temperature shown on each port: `/monitor.wireTemps[0..9]`
  - values are integers in degC; `-127` means "unknown/off"
  - if a wire is NTC-linked, firmware may overwrite that wire slot with the NTC temperature
- AC dot (mains present via 12V detect): `/monitor.ac`
  - backend source: `digitalRead(DETECT_12V_PIN)` (12V DC presence signal used as "AC input connected" indicator)
- Relay dot: `/monitor.relay` (on/off)

#### Live tab: session KPIs (from `/monitor.session`)
The live card must display the session values and keep them stable across refreshes:
- When `/monitor.session.valid == false`: show "No active session" and `--` in the 4 KPI slots.
- When `valid == true` and `running == true`:
  - energy from `energy_Wh` (Wh)
  - duration from `duration_s` (s)
  - peak power from `peakPower_W` (W)
  - peak current from `peakCurrent_A` (A)
  - update on every monitor refresh
- When `valid == true` and `running == false`:
  - show the last session values (same keys) until a new session starts

These same session stats are also used to populate the History overlay (run sessions only).

---

## Overlays (expected behavior)

### Live overlay
- Shows a focused live chart for the current active context:
  - Wire calibration: the active wire curve + setpoint
  - Floor calibration: floor temp + setpoint (and any relevant wire/model traces)
  - Normal run: tracked output temperature(s) + floor temperature target
  - Wire test: test target and active wire telemetry
- Only show/highlight the wires involved in the current context (do not highlight all 10 unless needed).
- Show header chips:
  - Setpoint
  - Target (e.g., "Wire 03", "Floor", "Profile")
  - Mode (running / wire calibration / floor calibration / test)

#### Live overlay: data and behavior (linkage contract)
- The Live overlay is opened from:
  - Statusbar Live shortcut (`data-sb="live-btn"`)
  - Live tab "Live Control" button (`data-live-control`)
- It must render the *current context* using the same live chart component used by the setup wizard.

Recommended default mapping from `/monitor` (fallback when no specific calibration/test status is active):
- Setpoint: `/monitor.wireTargetC` (shown in the header chip and as the setpoint line on the chart)
- Wire temperatures: `/monitor.wireTemps[]`
- Wires to highlight: derive from outputs that are currently energized:
  - build a CSV like `"1,3,7"` from `/monitor.outputs.outputX == true`
  - assign it to the chart root as `data-live-wires="1,3,7"`
- If no outputs are energized, highlight nothing or highlight the configured target wire index (depending on the active mode)

When a specific mode is active, mode-specific status endpoints should override the defaults:
- Wire test active -> highlight the active wire only and set Target to that wire
- Wire calibration active -> highlight the calibrated wire only and set Target accordingly
- Floor calibration active -> highlight the selected wire index (and optionally show all wires as context)

The Live overlay must never show all 10 wires by default unless the active mode truly involves all 10.

### Calibration overlay
- Advanced calibration console (separate from setup wizard):
  - Model calibration start + Stop and Save
  - Capacitance + current sensor zero action
  - Wire test with target temperature and live status
  - Floor calibration with recipe settings and results (tau, k, C)
  - Presence probe with per-wire status grid
  - Calibration log (refresh/clear)
  - Saved sessions/history load/refresh/clear tools
- Must show a clear "Calibration Status" card while any calibration is active.
- Must write important events into the calibration log (start/stop/save/error).

### Log overlay
- Displays device log output.
- Must support refresh and show an empty state ("No log data") cleanly.

### History overlay
- Lists run sessions only (not calibration, not manual set, not wire tests).
- Columns: time, duration, energy, peak power, peak current.

---

## Setup wizard (initial configuration UX)

The wizard is mandatory on first boot and whenever required setup steps are not complete.

Wizard progress is expected to persist across refresh/reconnect (device-side persistence); the UI must render the current device-reported progress and keep the dashboard in sync (persistent warning if incomplete).

### Step list (current UI)
1. Welcome (includes language selection)
2. Credentials (skippable)
3. Wi-Fi (skippable)
4. Allowed outputs (required)
5. Sensor zero + Presence check (required)
6. NTC parameters (skippable)
7. Device settings (required)
8. Wire calibration (skippable)
9. Floor calibration (skippable)
10. Finish (summary)

### Wizard-wide behavior rules
- The wizard must show:
  - current step number and title
  - a progress bar
  - required vs skippable markers
- Cancel must leave setup incomplete and should trigger a persistent dashboard warning.
- Skip must mark the step as skipped and show it in the Finish summary.

### Setup wizard notifications vs warnings/errors overlays (important)
The setup wizard is itself an overlay panel. The UI must route feedback to the correct surface:
- Wizard step feedback (validation errors, "saved", "action failed", "link NTC ok", etc.):
  - use the wizard toast inside the wizard (`data-setup-toast`) and/or inline step status fields
  - do not rely on the Calibration overlay UI for wizard messages
- System warnings/errors (device events):
  - always update the statusbar counters and store the entries for the Warnings/Errors overlays
  - the user must be able to open Warnings/Errors overlays from the statusbar even while the wizard is open

### Step 5: sensor zero + presence (important sequencing)
- UI must enforce (or at least strongly guide) the required sequence:
  1) Run capacitance measurement
  2) Run ACS zero
  3) Run presence probe
- If capacitance measurement requires at least one connected output for discharge, the UI must:
  - block the action when no wire is connected, and
  - explain the reason clearly.

### Step 8: wire calibration (wizard)
- The user attaches the single NTC to the selected wire gate.
- UI actions:
  - Link NTC to gate
  - Start calibration / Stop calibration
  - Save parameters / Discard
  - Optional: Start/Stop wire test using the saved model
- UI must show:
  - status (idle/running/stopped/saved)
  - active gate, linked state, progress
  - extracted parameters: R0, tau, K, C
  - live chart (setpoint + wire curve)

### Step 9: floor calibration (wizard)
- Uses NTC floor temperature and the model to estimate wire temperatures.
- UI inputs: target temp, duty, ambient/heat/cool durations, sample interval, wire index.
- UI must show:
  - stage, running/done, tau/K/C results
  - live chart for the active calibration context

---

## Data bindings (frontend-side selectors to wire later)

This section is a quick reference for where the UI expects live data and actions.

### Common
- Save/reset buttons: `data-action="save"` and `data-action="reset"`
- Toast: `data-toast` / `data-toast-text`
- Confirm: `data-confirm`, `data-confirm-yes`, `data-confirm-no`

### Sidebar
- Reset button: `data-sidebar-action="reset"`
- Power button: `.power-button` with state classes like `state-off`, `state-idle`, `state-ready`
- Mute button: `.round-button.mute` with `aria-pressed`

### Statusbar (`data-sb="..."`)
- `indicator`, `link-pill`, `warn-count`, `err-count`, `board-temp`, `sink-temp`, `dc-voltage`, `dc-current`
- `mode-pill` / `mode-label`, and `live-btn`
- Session: `session-toggle`, `cred-btn`, `logout-btn`

### Alerts overlays
- Warnings: `data-alert-overlay="warn"`
- Errors: `data-alert-overlay="err"`
- Current: `data-alert-current`, `data-alert-current-meta`
- History: `data-alert-list`

### Live overlay
- Context: `data-live-wires`, `data-live="setpoint|target|mode"`
- Chart: `admin/partials/components/live-chart.html` uses:
  - `data-live-setpoint`, `data-live-wire-temp="1..10"`
  - `.wire-line[data-wire]` and `.wire-dot[data-wire]`

### Live tab (wiring + KPIs)
- Port readouts: `.live-port[data-port="L1"]` ... `.live-port[data-port="R5"]`
  - Each port row also contains a sibling `.live-state-dot[data-state="on|off|nowire"]`
- AC / relay indicators: `.live-bottom-dot.live-ac` and `.live-bottom-dot.live-relay`
- Current session panel:
  - Subtitle: `.live-card-sub` (e.g., "No active session" / "Running" / "Last session")
  - KPI values: `.live-metric-val` (note: these currently have no stable `data-*` hooks; consider adding `data-live-kpi="energy|duration|peakPower|peakCurrent"` to make backend linkage robust)

### Setup wizard calibration bindings
- Inputs: `data-cal-input="..."` + `data-cal-scope="wizard"`
- Actions: `data-cal-action="..."` + `data-cal-scope="wizard"`
- Fields: `data-cal-field="..."` + `data-cal-scope="wizard"`

### Calibration overlay bindings
- Inputs: `data-cal-input="..."`
- Actions: `data-cal-action="..."`
- Fields: `data-cal-field="..."`
- Presence grid: `data-cal-grid="presenceProbeGrid"`

---

## Key backend payloads the UI must consume (CBOR)

This section lists the minimal data contracts needed to wire the UI quickly.

### `GET /monitor` (telemetry + session)
CBOR map keys used by the UI:
- Electrical:
  - `capVoltage` (float) -> statusbar DC voltage, dashboard voltage gauge
  - `current` (float) -> statusbar current, dashboard current gauge
  - `capacitanceF` (float) -> dashboard capacitance gauge (convert to mF for display)
- Temperatures:
  - `boardTemp` (float) and `heatsinkTemp` (float) -> statusbar + dashboard
  - `wireTemps` (array[int]) -> live tab ports and live chart traces (`-127` means missing)
  - `wirePresent` (array[bool]) -> live tab dot state
- Floor control (optional but recommended for dashboard/live):
  - `floor` (map):
    - `active` (bool)
    - `target_c` (float, optional)
    - `temp_c` (float, optional; UI should treat as NTC floor temp when present)
    - `wire_target_c` (float, optional)
    - `updated_ms` (uint, optional)
- Outputs/state:
  - `outputs` (map: `output1..output10` bool) -> live tab energized state + active wires in live overlay
  - `relay` (bool) and `ac` (bool) -> live tab bottom indicators
    - note: `ac` is derived from the 12V detect input (`DETECT_12V_PIN`), not a direct mains waveform measurement
  - `ready` (bool) / `off` (bool) -> power button state mapping (along with `/state_stream`)
- Unread warning/error counters:
  - `eventUnread` (map: `warn`, `error`) -> statusbar counters fallback
- Ambient wait (run gating):
  - `ambientWait` (map):
    - `active` (bool)
    - `since_ms` (uint, optional)
    - `tol_c` (float, optional)
    - `reason` (text, optional; localized)
  - When `active == true`, UI should block RUN and offer a "Confirm wires cool" action:
    - `POST /control` `{ action:"set", target:"confirmWiresCool" }`
- Connectivity (statusbar):
  - `wifiSta` (bool), `wifiConnected` (bool), `wifiRssi` (int, optional)
- Fan:
  - `fanSpeed` (uint)
- Session KPIs:
  - `session` map: `valid`, `running`, `energy_Wh`, `duration_s`, `peakPower_W`, `peakCurrent_A` -> live tab current session KPI panel
  - optional totals: `sessionTotals` map: `totalEnergy_Wh`, `totalSessions`, `totalSessionsOk`

### `GET /monitor_since?seq=<u32>` (optional live batch)
Incremental batch API for charting without pulling the full `/monitor` payload.
- Query:
  - `seq` (uint, optional) -> "since" sequence number
- Response CBOR keys:
  - `items` (array[map]) where each entry contains:
    - `seq` (uint)
    - `ts` (uint ms)
    - `capV` (float)
    - `i` (float)
    - `mask` (uint; outputs bitmask)
    - `relay` (bool), `ac` (bool), `fan` (uint)
    - `wireTemps` (array[int], length 10)
  - `seqStart` / `seqEnd` (uint, optional; present when at least one item returned)
Note: live buffer is enabled; UI can adopt this endpoint for high-rate charting, while keeping `/monitor` as the baseline snapshot.

### `GET /load_controls` (persisted settings + setup gating)
This response is used to populate all editable UI controls at load time and to gate user actions.
Minimum keys the UI must use:
- `setupDone` (bool), `setupRunAllowed` (bool), `setupCalibPending` (bool)
- `setupStage` (int), `setupSubstage` (int), `setupWireIndex` (int) -> resume wizard position and selected wire context
- `outputAccess` (map: `output1..output10` bool) -> user allow list + allowed outputs gating
- `uiLanguage` (text) -> optional: initialize UI language to match the device on first connect
- `fanSpeed` (uint), `buzzerMute` (bool), `ledFeedback` (bool)
- Core device configuration keys shown in tabs/wizard (sampling, current source, resistances, NTC params, floor material, etc.)

### `GET /setup_status` (wizard progress + missing requirements)
This response is used to drive the setup wizard UX and the persistent "setup incomplete" warnings.
Key fields:
- `setupDone` (bool)
- `stage` (int), `substage` (int), `wireIndex` (int)
- `configOk` (bool), `calibOk` (bool), `ready` (bool), `runAllowed` (bool), `calibPending` (bool)
- `missingConfig` (array[text]) and `missingCalib` (array[text]) to show what is blocking readiness

Additional calibration progress keys (used for wizard step status rendering):
- Per-wire (keys `"1"`..`"10"`):
  - `wireStage` (map[text->int])
  - `wireRunning` (map[text->bool])
  - `wireCalibrated` (map[text->bool])
- Floor:
  - `floorStage` (int)
  - `floorRunning` (bool)
  - `floorCalibrated` (bool)
- Sensor/presence:
  - `capCalibrated` (bool)
  - `presenceCalibrated` (bool)

### `GET /last_event` (seed warnings/errors + mark read)
Used to seed overlays after login/reconnect.
- Optional query:
  - `mark_read=1` (bool-ish) -> mark warning/error history as read
- Response CBOR keys:
  - `state` (text: `Idle|Running|Shutdown|Error`)
  - `last_error` (map): `reason` (text, localized) + optional `ms` (uint), `epoch` (uint)
  - `last_stop` (map): `reason` (text, localized) + optional `ms` (uint), `epoch` (uint)
  - `unread` (map): `warn` (uint), `error` (uint)
  - `warnings` (array[map]) up to 10 items: `reason` + optional `ms` / `epoch`
  - `errors` (array[map]) up to 10 items: `reason` + optional `ms` / `epoch`

### `POST /setup_update` (persist wizard progress; admin-only)
Use this to persist the wizard position so the UI can resume after refresh.
- Request body CBOR keys (all optional, only provided fields are updated):
  - `setup_done` (bool)
  - `stage` (int)
  - `substage` (int)
  - `wire_index` (int)
- Response CBOR keys:
  - `ok` (bool)
  - `setupDone` (bool), `stage` (int), `substage` (int), `wireIndex` (int)
  - `configOk` (bool), `calibOk` (bool), `calibPending` (bool)

### `POST /setup_reset` (reset setup + calibration flags; admin-only)
Use this for the "Reset setup" / "factory-ish reset" confirm action.
- Request body CBOR keys (all optional):
  - `clear_models` (bool) -> implies `clear_wire_params` + `clear_floor_params`
  - `clear_wire_params` (bool)
  - `clear_floor_params` (bool)
- Response: `{ ok: true }`
- May fail with `error="calibration_busy"` if any calibration/test is active.

### `POST /control` target `calibrate` (caps + current sensor zero)
This triggers the device-side "manual calibration sequence" used by the setup wizard Step 5.
- Request: `{ action:"set", target:"calibrate" }`
- Response: `{ status:"ok", queued:true }`
- Effect (device-side sequence):
  - Discharge to a safe baseline
  - Charge caps to threshold
  - Calibrate capacitance (`capacitanceF` updates)
  - Calibrate current sensor zero (if present)
  - Recharge and return to a safe state
- UI must treat this as async: disable the button and poll `/setup_status.capCalibrated` and/or `/load_controls.capacitanceF` until completion.
- If capacitance measurement requires at least one connected wire for discharge, the UI must block starting this action when no wire is connected.

### `POST /control` targets used by the UI (quick reference)
The UI should only use these targets when authenticated; many return `{status:"ok", queued:true}` and require a refresh from `/monitor` and `/load_controls`.

- Outputs (manual control):
  - `target:"outputN"` where `N=1..10`, `value:true|false`
- Allowed outputs (access flags):
  - `target:"AccessN"` where `N=1..10`, `value:true|false`
  - Note: `/load_controls.outputAccess` uses keys `output1..output10`, but the setter targets are `Access1..Access10`.
- Power (global state):
  - `target:"systemWake"` (wake device)
  - `target:"systemStart"` (request RUN)
  - `target:"systemShutdown"` (request stop/shutdown)
- Restart/reset:
  - `target:"reboot"` (restart after short delay)
  - `target:"systemReset"` (reset flag + restart)
- Run gating:
  - `target:"confirmWiresCool"` (used when `/monitor.ambientWait.active==true`)
- UX toggles:
  - `target:"buzzerMute"`, `value:true|false`
  - `target:"ledFeedback"`, `value:true|false`
  - `target:"fanSpeed"`, `value:0..100`
- Credentials (these use `value` as a nested map):
  - Admin: `target:"adminCredentials"`, `value:{ current, username?, password?, wifiSSID?, wifiPassword? }`
  - User: `target:"userCredentials"`, `value:{ current, newId?, newPass? }`
- Station Wi-Fi (applies immediately; may restart):
  - `target:"wifiSSID"`, `value:"..."`
  - `target:"wifiPassword"`, `value:"..."`
- Device config (selected targets; mostly `{status:"ok", applied:true}`):
  - Electrical/safety:
    - `target:"acFrequency"`, `value:int` (queued)
    - `target:"chargeResistor"`, `value:float` (queued)
    - `target:"currLimit"`, `value:float` (queued)
    - `target:"currentSource"`, `value:int|text` (applied; text containing `"acs"` selects ACS)
    - `target:"tempWarnC"`, `value:float` (applied)
    - `target:"tempTripC"`, `value:float` (applied)
  - Floor:
    - `target:"floorThicknessMm"`, `value:float` (applied)
    - `target:"floorMaterial"`, `value:text|int` (applied; text like `wood|epoxy|concrete|slate|marble|granite`)
    - `target:"floorMaxC"`, `value:float` (applied)
    - `target:"floorSwitchMarginC"`, `value:float` (applied)
    - `target:"floorTau"`, `value:double` (applied)
    - `target:"floorK"`, `value:double` (applied)
    - `target:"floorC"`, `value:double` (applied)
    - `target:"floorCalibrated"`, `value:bool` (applied)
    - `target:"nichromeFinalTempC"`, `value:float` (applied)
  - Wire model:
    - `target:"wireResN"`, `value:float` (queued; `N=1..10`)
    - `target:"wireOhmPerM"`, `value:float` (queued)
    - `target:"wireGauge"`, `value:int` (queued)
    - `target:"wireTauN"`, `value:double` (applied; `N=1..10`)
    - `target:"wireKN"`, `value:double` (applied; `N=1..10`)
    - `target:"wireCN"`, `value:double` (applied; `N=1..10`)
    - `target:"wireCalibratedN"`, `value:bool` (applied; `N=1..10`)
  - NTC:
    - `target:"ntcGateIndex"`, `value:int` (applied; 1..10)
    - `target:"ntcModel"`, `value:int|text` (applied; text containing `stein`/`sh` selects Steinhart-Hart, else Beta)
    - `target:"ntcBeta"`, `value:float` (applied)
    - `target:"ntcT0C"`, `value:float` (applied)
    - `target:"ntcR0"`, `value:float` (applied)
    - `target:"ntcFixedRes"`, `value:float` (applied)
    - `target:"ntcShA"|"ntcShB"|"ntcShC"`, `value:float` (applied)
    - `target:"ntcMinC"|"ntcMaxC"`, `value:float` (applied)
    - `target:"ntcSamples"`, `value:int` (applied)
    - `target:"ntcPressMv"|"ntcReleaseMv"|"ntcDebounceMs"`, `value:float|int` (applied)
    - `target:"ntcCalTargetC"`, `value:float` (applied)
    - `target:"ntcCalSampleMs"`, `value:int` (applied)
    - `target:"ntcCalTimeoutMs"`, `value:int` (applied)
    - `target:"ntcCalibrated"`, `value:bool` (applied)
  - Presence flags:
    - `target:"presenceMinDropV"`, `value:float` (applied)
    - `target:"presenceCalibrated"`, `value:bool` (applied)

### `POST /presence_probe` (presence probe; admin-only)
Used by setup wizard Step 5 (presence) and by the Calibration overlay presence grid.
- Request body CBOR keys (optional):
  - `presenceMinDropV` (float) -> overrides/stores `presenceMinDropV` (clamped to 5..100)
- Response CBOR keys:
  - `status` (text: `"ok"`)
  - `calibrated` (bool: `true`)
  - `wirePresent` (array[bool], length 10)

### Wire test endpoints (`/wire_test_*`)
Used by the Calibration overlay and optionally by wizard Step 8 after model parameters are saved.

#### `GET /wire_test_status`
Response CBOR keys:
- `running` (bool)
- `target_c` (float, optional)
- `active_wire` (uint, optional)
- `ntc_temp_c` (float, optional)
- `active_temp_c` (float, optional)
- `packet_ms` (uint), `frame_ms` (uint), `updated_ms` (uint)
- `mode` (text: `"energy"`)
- `purpose` (text: `wire_test|model_cal|ntc_cal|floor_cal|none`)

#### `POST /wire_test_start`
- Request body: `{ target_c: float }`
- Response: `{ status:"ok", running:true }`

#### `POST /wire_test_stop`
- Response: `{ status:"ok", running:false }`

### Session history endpoints (History overlay)
#### `GET /session_history`
Response CBOR keys:
- `history` (array[map]) where each row includes:
  - `start_ms` (uint)
  - `duration_s` (uint)
  - `energy_Wh` (float)
  - `peakPower_W` (float)
  - `peakCurrent_A` (float)

#### `GET /History.cbor`
CBOR file-style response with the same `history[]` shape as `/session_history`.

### Device log endpoints (Log overlay)
- `GET /device_log` -> `text/plain`
- `POST /device_log_clear` -> CBOR `{ ok: true }`

### Access Point config (`POST /ap_config`; admin-only)
Updates AP credentials; may trigger a restart when values change.
- Request body CBOR keys:
  - `apSSID` (text, optional)
  - `apPassword` (text, optional)
- Response: `{ status:"ok", applied:true }`

### NTC calibration endpoints (`/ntc_*`)
Used by the Calibration overlay (and optionally wizard steps if exposed).

#### `POST /ntc_calibrate` (multi-point / Steinhart-Hart fit)
- Request body CBOR keys (optional):
  - `target_c` (float) -> defaults to heatsink reference when omitted/invalid
  - `sample_ms` (uint) -> defaults from config, clamped to 50..5000
  - `timeout_ms` (uint) -> defaults from config, clamped to 1000..3600000
- Response: `{ status:"ok", running:true }`

#### `GET /ntc_cal_status`
Response CBOR keys:
- `running` (bool), `done` (bool)
- `error` (text, optional)
- `start_ms` (uint), `elapsed_ms` (uint)
- `target_c` (float, optional), `heatsink_c` (float, optional)
- `ntc_ohm` (float, optional)
- `sample_ms` (uint), `samples` (uint)
- Fit results (optional): `sh_a` (float), `sh_b` (float), `sh_c` (float)
- `wire_index` (uint, optional)

#### `POST /ntc_cal_stop`
- Response: `{ status:"ok", running:false }`

#### `POST /ntc_beta_calibrate` (single-point beta calibration)
- Request body CBOR keys (any of these may be provided; backend selects a reference temperature):
  - `ref_temp_c` (float) / `ref_c` (float) / `ref_alias_c` (float)
  - `temp_c` (float) / `target_c` (float)
- Response: `{ status:"ok", applied:true }`

### Calibration recorder endpoints (`/calib_*`)
Used by the Calibration overlay for model calibration and floor calibration sessions, and to load/export calibration datasets.

#### `GET /calib_status`
Response CBOR keys:
- `running` (bool)
- `mode` (text: `ntc|model|floor|none`)
- `count` (uint), `capacity` (uint)
- `interval_ms` (uint)
- `start_ms` (uint), `start_epoch` (uint, optional)
- `saved` (bool), `saved_ms` (uint), `saved_epoch` (uint, optional)
- `target_c` (float, optional)
- `wire_index` (uint, optional)

#### `POST /calib_start`
Request body CBOR keys:
- Common:
  - `mode` (text: `ntc|model|floor`) (required)
  - `interval_ms` (uint, optional)
  - `max_samples` (uint, optional)
  - `target_c` (float, optional)
  - `wire_index` (uint, optional)
  - `epoch` (uint, optional) -> updates device RTC when provided
- Duty (used by model/floor energy runs; optional):
  - `duty` (float 0..1) or `duty_pct` (float 0..100)
- Floor-only recipe (optional; defaults are applied when omitted):
  - `ambient_ms` (uint)
  - `heat_ms` (uint)
  - `cool_ms` (uint)
  - `timeout_ms` (uint)

Response: `{ status:"ok", running:true }`

#### `POST /calib_stop`
- Request body optional: `{ epoch: uint }`
- Response: `{ status:"ok", running:false, saved: bool }`

#### `POST /calib_clear`
- Response: `{ status:"ok", cleared:true, file_removed: bool, history_removed: uint }`

#### `GET /calib_data?offset=<u16>&count=<u16>`
Paged sample fetch (max `count` is 200).
- Response CBOR keys:
  - `meta` (map) includes all `calib_status` fields plus:
    - `offset` (uint)
    - `limit` (uint)
  - `samples` (array[map]) rows include:
    - `t_ms` (uint), `v` (float), `i` (float)
    - `temp_c` (float), `room_c` (float)
    - `ntc_v` (float), `ntc_ohm` (float), `ntc_adc` (int), `ntc_ok` (bool)
    - `pressed` (bool)

#### `GET /calib_file`
Downloads the latest calibration recorder file (CBOR payload) if it exists.

#### `GET /calib_history_list`
Response CBOR keys:
- `items` (array[map]) with:
  - `name` (text) (path/name to request via `/calib_history_file`)
  - `start_epoch` (uint, optional)

#### `GET /calib_history_file?name=<text>`
Downloads a calibration history file (CBOR payload) by name.

---

## Admin-mode backend helper fields (recommended additions)

The backend already exposes enough to implement admin mode, but these additions would simplify the UI logic:
- Add `role` to `/monitor` and/or `/load_controls` so the UI can recover role after refresh without relying on localStorage.
- Add `dc12v` (bool) alongside `/monitor.ac` to avoid confusion (keep `ac` for backward compatibility).
- Add `capabilities` (array[text]) to `/device_info` or `/load_controls` (e.g. `["calib","ntc_cal","presence_probe"]`) to allow feature gating by firmware build.
