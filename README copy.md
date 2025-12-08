You’ve already implemented most of what you’re asking for; the remaining gaps are mainly about the exact **manual-mode rules** and a few **timeout/heartbeat** behaviors.

I’ll split this into three areas: **startup/shutdown safety**, the **com-hub/queue architecture**, and the **“random logout”** issue.

1. Startup / shutdown safety (are outputs truly OFF?)

From the code path, the system already enforces a safe baseline at boot and around the main loop:

At boot (`setup()`):

- `mainRelay->turnOff();` and `WIRE->disableAll();` in `main.cpp` (lines 49–79) shut down the power path and every heater output before anything else executes.
- `CpDischg` and the fans are set to a non-driving state (e.g., `discharger->setBypassRelayGate(false);`).

Loop baseline (`Device::loopTask()`):

- At the start of the loop task, you again do:
  `relayControl->turnOff();`
  `if (WIRE) WIRE->disableAll();`
  `if (indicator) indicator->clearAll();`
  `RGB->setOff();`
  in `src/system/DeviceLoop.cpp` (lines 246–257).
- You always enter idle or run from this “everything OFF” baseline.

Shutdown / deep sleep:

- `Device::prepareForDeepSleep()` (`DeviceCore.cpp:207+`) disables all outputs, fans, relay, caps bypass, LEDs, and sets the state to `Shutdown`.
- When the loop exits from `RUN`, you again call:
  `relayControl->turnOff();`
  `WIRE->disableAll();`
  `indicator->clearAll();`
  `RGB->setOff();`
  in `DeviceLoop.cpp` (lines 316–329).

So at power-up and on any loop exit, **all outputs and the relay are forced OFF**. Manual outputs are only enabled when either:

- The planner runs in `StartLoop()` (auto mode), or
- A Wi-Fi command issues `DevCmdType::SET_OUTPUT` (manual UI).

That part of the safety story is already solid.

2. The current com-hub / queue design vs. what you want

Your target:

> “Wi-Fi doesn’t change anything unless the device core accepts, applies it, and then tells Wi-Fi it’s done. Put a decoupled, queue-based com-hub between them.”

You already have that structure (with slightly different names):

Device core state machine

- `Device::loopTask()` and `Device::StartLoop()` run critical power logic on their own task (`DeviceLoopTask`) using:
  event group `gEvt` with `EVT_WAKE_REQ`, `EVT_RUN_REQ`, `EVT_STOP_REQ` (`Device.h:330+`),
  and states `DeviceState::Shutdown/Idle/Running/Error` (`Device.h` + `DeviceLoop.cpp`).
- All “hard” behaviors (wait for 12V, charge caps, over-current, 12V drop, thermal lockouts) live here.

Command/ack queues between Wi-Fi and Device

- In `Device.h` / `DeviceCore.cpp`:
  `DevCommand` and `DevCommandAck` (`Device.h` lines 207–222);
  `cmdQueue` and `ackQueue` (private members).
  `Device::submitCommand()` enqueues a command and assigns a unique ID (`DeviceCore.cpp` lines 53–77).
  `Device::commandTask()` (own task) pulls from `cmdQueue` and calls `handleCommand()` (`DeviceCore.cpp` lines 86–104).
  `handleCommand()` performs the change (write NVS, outputs, relay, fan, reset), then replies with an ack in `ackQueue` (`DeviceCore.cpp` lines 106–211).
  Some commands require `requiresSafe()` and are blocked while `Running`.

DeviceTransport as the com-hub façade

- `sendCommandAndWait(...)` (`DeviceTransport.cpp` lines 82–118) builds a `DevCommand`, calls `DEVICE->submitCommand(cmd)`, waits on `DEVICE->waitForCommandAck(...)`, and checks type+id.
- Returns true on success; logs + false on timeout/mismatch.

Wi-Fi Manager uses DeviceTransport

- In `WiFiManager::handleControl(const ControlCmd& c)` (`WiFiManager.cpp:889+`), config and manual ops call `DEVTRAN->setXxx() / requestRun() / requestStop()`, which internally use `sendCommandAndWait()` or event bits.
- `/control` decodes JSON → `ControlCmd` → `handleControl(c)` (`WiFiManager.cpp` lines 520–577).

End-to-end flow from the admin page:

- `admin.js` → `sendControlCommand("set","output3",true)`.
- `/control` builds `ControlCmd{ CTRL_OUTPUT_BOOL, i1=3, b1=true }` → `handleControl(c)`.
- `handleControl()` → `DEVTRAN->setOutput(3, true, true)`.
- `DeviceTransport::setOutput()` → build `DevCommand{SET_OUTPUT, i1=3, b1=true}` → `sendCommandAndWait()`.
- `Device::commandTask()` receives it, does `WIRE->setOutput(3, true)`, updates indicators in `handleCommand`.
- `handleCommand()` pushes `ack(success=true)` into `ackQueue`.
- `sendCommandAndWait()` returns true; `/control` responds `{"status":"ok","applied":true}`.
- Admin JS leaves the UI toggle ON; otherwise it reverts.

This is exactly a **decoupled, request+ack, queue-based com-hub** where the device core “accepts, applies, and acknowledges.”

Where it diverges from your intent:

- **Manual vs. auto is currently UI-only**
  `toggleMode()` sends `set:mode` (admin JS). On the server, `"mode"` maps to `CTRL_MODE_IDLE` and just calls `DEVTRAN->requestIdle()` (`WiFiManager.cpp:552–559, 1009–1015`), which sets `EVT_STOP_REQ` (`DeviceTransport.cpp` 38–52).
  There’s no persistent manual-mode flag in `Device` or `StatusSnapshot`, and nothing core-side that says “if manual is on, the auto loop must stay off.” The board only receives an IDLE request.

- **Output safety vs. state**
  `DevCmdType::SET_OUTPUT` is **not** behind `requiresSafe()` (`DeviceCore.cpp:115–139`).
  The command task will flip outputs even in `Running` (UI tries to prevent it via `ensureManualTakeover()`, but a direct `/control` call or a UI bug could still change outputs mid-run).

- **Wi-Fi control queue not consistently used for HTTP**
  `WiFiManager` has `_ctrlQueue` and `controlTaskLoop()` (serialize `ControlCmd`) but `/control` calls `handleControl(c)` directly. The queue is used by other internal pieces (fan, RGB) via their `sendCmd` helpers, not by the HTTP handler.

3. Why the “random” disconnects/logouts happen

Two separate mechanisms can dump you back to `/login`:

Heartbeat keep-alive

- Front-end starts a tight heartbeat: `startHeartbeat(1500)` (admin/user JS).
  Every 1.5s: `fetch("/heartbeat")`.
  If the fetch fails or the response isn’t literally `"alive"`, the JS immediately:
  `window.location.href = "http://powerboard.local/login";`
- Backend `/heartbeat` (`WiFiManager.cpp` 248–264):

  - If `!isAuthenticated(request)` or `wifiStatus == NotConnected`: send 403 JSON, `request->redirect("/login")`, and **don’t** set `keepAlive`.
  - If authed: update `lastActivityMillis`, set `keepAlive = true`, return `"alive"`.

- A separate RTOS task `WiFiManager::heartbeat()` (every 6s, `WiFiManager.cpp` 815–870) checks:
  `isUserConnected()`, `isAdminConnected()`, and `keepAlive`.
  If no user/admin → stop heartbeat, mark Wi-Fi off.
  If `keepAlive == false` at the check: log “Heartbeat timeout – disconnecting”, call `onDisconnected()` → `wifiStatus = NotConnected`, play Wi-Fi-off sound, post overlay.
- Result: if any **6-second** window passes where `/heartbeat` didn’t set `keepAlive` (tab backgrounded, timer throttled, brief Wi-Fi dip, slow response), the task flips to `NotConnected`. The next `/heartbeat` is treated as unauthenticated and redirected to `/login`; your frontend sees not-alive/fail and immediately navigates to `/login`.
  That exactly matches the “random” kicks.

Wi-Fi/AP inactivity timer

- `inactivityTask()` (`WiFiManager.cpp` 732–754) runs every 5s:
  if Wi-Fi is ON and `millis() - lastActivityMillis > INACTIVITY_TIMEOUT_MS` (3 minutes, `Config.h:211`), it disables the AP and stops Wi-Fi.
- Normally `/heartbeat` and monitor polling keep `lastActivityMillis` fresh; if heartbeat stalls and you don’t interact for >3 minutes, the AP shuts down and you’re out.
- In practice the first mechanism (strict 6s heartbeat vs. 1.5s JS timer) is the usual culprit.

4. Bringing the design in line with your goals (conceptually)

Without altering the run logic, here’s how to make it match your intent:

- **Make manual/auto a device-owned mode, not a UI idea**

  - Add `bool manualMode` (or an enum) in `Device`; include it in `Device::StateSnapshot` / `StatusSnapshot`.
  - Introduce `DevCmdType::SET_MODE` handled only in `Device::handleCommand()`:

    - `SET_MODE(manual=true)`: if `Running`, signal STOP via `gEvt` and wait for `Idle/Shutdown`; force `WIRE->disableAll()` and `indicator->clearAll()` before ack; set `manualMode = true`.
    - `SET_MODE(manual=false)`: ensure outputs are OFF; clear `manualMode`, leave state in `Idle` ready for `requestRun()`.

  - Map UI “mode” to this new command instead of `CTRL_MODE_IDLE`, so the device core performs the safe transition and ack’s it. The UI waits for ack / watches state.

- **Gate outputs/relay by state in the core**

  - In `handleCommand()`, enforce for `SET_OUTPUT` / `SET_RELAY`:
    allowed only if `getState()==Idle && manualMode==true` (manual control),
    or when `StartLoop()` owns the outputs (auto).
  - Config writes already go through `requiresSafe()`—keep that.

- **Use the Wi-Fi control queue for HTTP**

  - Make `/control` enqueue into `_ctrlQueue` via `sendCmd(c)` and let `controlTaskLoop()` call `handleControl(c)`.
  - Device side is already serialized by `Device::commandTask()`.

- **Tighten UI ↔ core status coupling**

  - You already have SSE (`/state_stream`) and `/monitor` polling (~400 ms).
  - Once `manualMode` (and maybe per-output ownership) are added to `StatusSnapshot`, surface them in `/monitor` and `/load_controls`.
  - UI then reflects **device-reported** manual mode and disables/enables controls accordingly.

- **Make logouts less trigger-happy**

  - Loosen heartbeat semantics: treat a missed beat as a soft warning first (don’t immediately set `NotConnected`), or use `/monitor` activity as proof of life.
  - In the frontend, don’t redirect to `/login` on the first failure; show a “reconnecting…” overlay and only bail after several consecutive misses.
  - Keep in mind background-tab timer throttling makes a strict 6s watchdog vs. 1.5s JS tick inherently fragile.

Bottom line

- The **decoupled, queue-based com-hub with command+ack** is already in place (`DevCommand`/`DevCommandAck` + `DeviceTransport::sendCommandAndWait()`).
- Startup/shutdown already force the relay and all outputs **OFF**.
- What’s missing:

  1. a **device-owned manual/auto mode** and **core-enforced gating** for outputs/relay,
  2. routing `/control` through the Wi-Fi control queue, and
  3. a more forgiving, session-aware heartbeat/inactivity policy to avoid “random” logouts when a beat is missed.

If helpful, I can sketch the exact new fields/commands (names, where to expose them in `/monitor` and `/load_controls`, and minimal UI tweaks) next—no changes to your run loop required.
