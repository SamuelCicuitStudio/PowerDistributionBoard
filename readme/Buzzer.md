# Buzzer Map

The buzzer provides short audible cues that mirror the RGB LED states: success, faults, Wi‑Fi events, and lifecycle transitions.

## Patterns (modes)
- **BIP** — single short mid‑tone beep (generic tap/acknowledge).
- **SUCCESS** — three ascending beeps (OK / completed).
- **FAILED** — two low beeps (generic failure / not currently used often).
- **WIFI_CONNECTED** — two mid‑high beeps (joined AP/STA).
- **WIFI_OFF** — single mid‑low beep (Wi‑Fi link lost or disabled).
- **OVER_TEMPERATURE** — four fast high beeps (over‑temperature warning/limit reached).
- **FAULT** — five low urgent beeps (latched fault; matches red FAULT strobe).
- **STARTUP** — three ascending beeps (power‑on / boot sequence).
- **READY** — two short high beeps (system ready / safe to operate).
- **SHUTDOWN** — three descending beeps (system shutting down).
- **CLIENT_CONNECTED** — short two‑tone chirp (web client/admin connected).
- **CLIENT_DISCONNECTED** — short two‑tone descending chirp (web client/admin disconnected).

## Event mapping
- **Boot / startup** — `STARTUP`, then `READY` when the system reaches ready state (pairs with BOOT/START backgrounds).
- **Enter RUN / successful actions** — `SUCCESS` (pairs with green RUN overlays).
- **Wi‑Fi join / leave** — `WIFI_CONNECTED` when joining, `WIFI_OFF` when Wi‑Fi is turned off or all clients time out (pairs with Wi‑Fi overlays).
- **Web client connect / disconnect** — `CLIENT_CONNECTED` and `CLIENT_DISCONNECTED` (pairs with WEB_ADMIN/WEB_USER overlays).
- **User taps / small actions** — `BIP` used as a neutral acknowledgment for many button or control changes.
- **Over‑temperature** — `OVER_TEMPERATURE` on thermal warnings/trips (pairs with TEMP_WARN / TEMP_CRIT overlays).
- **Faults / protection trips** — `FAULT` for serious protection events (over‑current, global thermal lock, config errors) alongside the red FAULT LED pattern.
- **System shutdown** — `SHUTDOWN` when the system powers down or disables key functions (pairs with OFF / MAINT states).

The mute setting persists across reboots, and when muted the buzzer simply drops all queued modes while the RGB LED continues to convey state visually.
