# Buzzer Map

The buzzer provides short audible cues that mirror key RGB and UI events. It also supports a latched alert reminder for warnings or critical faults.

## One-shot cues (event-driven)
- BIP - single short mid-tone beep (generic tap/acknowledge).
- SUCCESS - three ascending beeps (OK / completed).
- FAILED - two low beeps (generic failure / not currently used often).
- WIFI_CONNECTED - two mid-high beeps (joined AP/STA).
- WIFI_OFF - single mid-low beep (WiFi link lost or disabled).
- OVER_TEMPERATURE - four fast high beeps (over-temperature warning/limit reached).
- FAULT - five low urgent beeps (latched fault cue).
- STARTUP - three ascending beeps (power-on / boot sequence).
- READY - two short high beeps (system ready / safe to operate).
- SHUTDOWN - three descending beeps (system shutting down).
- CLIENT_CONNECTED - short two-tone chirp (web client/admin connected).
- CLIENT_DISCONNECTED - short two-tone descending chirp (web client/admin disconnected).

## Alert latch (warning/error reminders)
Latched alerts repeat a short reminder until cleared. This is separate from one-shot cues.
- `setAlert(WARNING)` - two short mid beeps, repeats every 10s.
- `setAlert(CRITICAL)` - three low beeps, repeats every 4s.
- `clearAlert()` - stop repeating reminder.
- Muted state blocks sound but keeps the alert state so it resumes when unmuted.

## Event mapping (typical)
- Boot / startup - `STARTUP`, then `READY` when the system reaches ready state.
- Enter RUN / successful actions - `SUCCESS` (pairs with green RUN overlays).
- WiFi join / leave - `WIFI_CONNECTED` when joining, `WIFI_OFF` when WiFi is turned off or all clients time out.
- Web client connect / disconnect - `CLIENT_CONNECTED` and `CLIENT_DISCONNECTED`.
- User taps / small actions - `BIP` used as a neutral acknowledgment.
- Over-temperature - `OVER_TEMPERATURE` on thermal warnings/trips.
- Faults / protection trips - `FAULT` for serious protection events (over-current, global thermal lock, config errors).
- System shutdown - `SHUTDOWN` when the system powers down or disables key functions.

The mute setting persists across reboots, and when muted the buzzer drops all queued modes while the RGB LED continues to convey state visually.
