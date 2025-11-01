# Power Distribution Board (Nichrome) — User Guide

Welcome! This board controls **10 nichrome-wire outputs**, an **inrush (bypass) MOSFET**, and an **input relay**, with a web interface for live control and configuration. It also features a **buzzer** and an **RGB status LED** that communicates system state.

> **Quick highlights**
>
> - One physical **yellow button** on the board for power/start/stop and Wi‑Fi actions.
> - Two web roles: **Admin** and **User** (with per‑output access control).
> - Web UI has **Dashboard**, **Manual**, **User Settings**, and **Admin Settings** tabs.
> - Wi‑Fi client mode (STA) with **auto‑fallback AP** if credentials fail.
> - **AP auto‑sleeps** after a short idle period. Triple‑tap the yellow button to bring Wi‑Fi back.

---

## Table of Contents

- [Hardware Overview](#hardware-overview)
- [Safety Notes](#safety-notes)
- [Power‑On & Wi‑Fi Behavior](#power-on--wi-fi-behavior)
- [First‑Time Setup](#first-time-setup)
- [Finding the Board’s IP](#finding-the-boards-ip)
- [Web Interface](#web-interface)

  - [Dashboard](#dashboard)
  - [Manual](#manual)
  - [User Settings](#user-settings)
  - [Admin Settings](#admin-settings)

- [Access Control (User vs Admin)](#access-control-user-vs-admin)
- [Buzzer & Mute](#buzzer--mute)
- [RGB Status LED Guide](#rgb-status-led-guide)
- [Typical Workflows](#typical-workflows)
- [FAQ / Troubleshooting](#faq--troubleshooting)

---

## Hardware Overview

- **Yellow Button**: single onboard push button for wake/run/stop and Wi‑Fi actions (see [Power‑On & Wi‑Fi Behavior](#power-on--wi-fi-behavior)).
- **10 Nichrome Outputs**: individually switchable channels for heating loads.
- **Inrush (Bypass) MOSFET**: engages after the capacitor reaches threshold.
- **Input Relay**: connects input power during the start sequence and disconnects on stop.
- **RGB Status LED**: communicates background state (OFF/IDLE/RUN/etc.) and overlays (Wi‑Fi events, web activity, relay toggles, warnings).
- **Buzzer**: short audible feedback for actions and state changes.

> **Electrical note:** Nichrome outputs are resistive heaters. Ensure proper gauge, thermal management, and **never run unattended** without safeguards.

---

## Safety Notes

- Nichrome elements get **hot**: mount on non‑flammable surfaces; provide ventilation.
- Verify wiring and fusing. Keep clearances between high‑current traces and low‑voltage logic.
- Do not exceed rated current per channel or total system limits.
- Use **proper PPE** and a **bench supply** during first tests.

---

## Power‑On & Wi‑Fi Behavior

1. **Boot → OFF** (LED off). System awaits a **Wake** action.
2. **Wake (Tap #1 on the yellow button or Web Start):**

   - Relay turns on, capacitor charges, then bypass MOSFET engages.
   - System goes to **IDLE** (ready) or directly to **RUN** if Start was already requested.

3. **Run (Tap #2)**: Starts the activation loop (per configured On/Off times).
4. **Stop (Tap #3)**: Clean shutdown → **OFF**.

### Wi‑Fi

- On power‑up the board tries saved **Wi‑Fi STA** credentials. If it can’t connect, it opens a soft **AP**.
- **AP SSID** appears; browse to **[http://192.168.4.1/login](http://192.168.4.1/login)**.
- **AP idle sleep:** If the web interface is not used for a while, Wi‑Fi powers down to save energy.
- **Bring Wi‑Fi back:** **Triple‑tap** the yellow button quickly to restart the Wi‑Fi connection/soft‑AP and access the web UI again.

---

## First‑Time Setup

1. **Power the board** and wait ~10–15 seconds.
2. If it can’t join your router, connect to the board’s **AP** from your phone/PC, open **[http://192.168.4.1/login](http://192.168.4.1/login)**.
3. **Default credentials** (change them after login):

   - **Admin**: `admin` / `admin123`
   - **User**: `user` / `user123`

4. Enter your **Wi‑Fi (STA)** credentials in the **Dashboard → Wi‑Fi** section or a dedicated Connect form (if present). After connecting, the board’s **IP will change** to the one assigned by your router.

> If Wi‑Fi is idle/sleeping, triple‑tap the yellow button to bring it back and reconnect.

---

## Finding the Board’s IP

- After the board joins your home/office Wi‑Fi, check your router’s **connected devices** page for the assigned IP.
- If unsure, you can:

  - Momentarily **disable your phone’s mobile data** and try **[http://pdis.local](http://pdis.local)** (if mDNS is enabled in your network), or
  - Use a network scanner app to find the device.

---

## Web Interface

The web tool has tabs and quick‑action buttons. Depending on whether you log in as **Admin** or **User**, you will see different capabilities.

### Dashboard

Primary status and quick controls:

- **Mode**: **Auto ↔ Manual** toggle for the main activation loop.
- **LT**: LED feedback switch (per‑channel LED indicators while running).
- **Ready / OFF indicators**: visual status.
- **Power button**:

  - **Start** (wake → run),
  - **Stop** (clean shutdown),
  - shows state (OFF/IDLE/RUN) and disables itself when not applicable.

- **Mute**: Mutes/unmutes the buzzer (saved to preferences).
- **System**: **Reboot** and **Factory Reset** (use with care).
- Live gauges (if present): input voltage/current, temperatures.

### Manual

Direct control when **Manual** mode is active:

- **Outputs 1–10**: On/Off per channel.
- **Input Relay**: On/Off.
- **Bypass MOSFET**: On/Off.
- **Fan**: duty slider.
- **Timing**: On‑Time / Off‑Time (ms or s), **Save** to preferences.
- **Electrical Params**: AC frequency, charge resistor, DC voltage, desired output voltage.

> Manual control is subject to role permissions and the **User Access** settings below.

### User Settings

- Change **User username/password**.
- **Per‑output access**: allow/deny the **User role** to control each output (1–10).
- Save applies immediately.

### Admin Settings

- Change **Admin username/password**.
- Use strong passwords and keep them safe.

---

## Access Control (User vs Admin)

- **Admin** can control all outputs and all system settings.
- **User** control is limited by **per‑output access flags** set in _User Settings_. If a channel is not granted, User clicks on it will be ignored.

---

## Buzzer & Mute

- The **Mute** button in the web UI toggles a persisted flag. When muted, beeps are suppressed except for critical events.
- You can unmute at any time from the web UI.

---

## RGB Status LED Guide

> The board is wired **RG‑only** (no blue). Colors below are described in RG terms.

### Background states (always-on ambience)

| State     | Effect                        | Meaning                    |
| --------- | ----------------------------- | -------------------------- |
| **OFF**   | LED off                       | System off / sleeping      |
| **WAIT**  | Amber breathe                 | Power‑up / getting ready   |
| **IDLE**  | Soft‑green slow heartbeat     | Standing by, ready         |
| **RUN**   | Bright‑green double heartbeat | Actively running           |
| **FAULT** | Fast red strobe               | Fault condition            |
| **MAINT** | Amber blink                   | Maintenance / special mode |

### Overlay events (short pulses layered on background)

- **Wi‑Fi AP**: yellow heartbeat pulse
- **Wi‑Fi STA**: quick green flash
- **Wi‑Fi lost**: amber blink
- **Web Admin active**: orange‑red breathe
- **Web User active**: green breathe
- **Relay on/off**: short yellow/amber flashes
- **Fan on/off**: short green/amber flashes
- **Output toggled (per‑channel)**: short green/amber flashes, repeated N times to indicate channel index
- **Power‑up sequence**: distinct flashes for _wait 12V_, _charging_, _threshold OK_, _bypass on_, _wait button_, and _start_
- **Low/Critical battery**: yellow/red attention blinks

> Overlays are prioritized; critical alerts can briefly preempt background effects.

---

## Typical Workflows

**Start a job**

1. Tap the yellow button once (or press **Start** in the web UI).
2. Wait for the LED sequence: relay on → charging → bypass on → IDLE.
3. Tap the yellow button again (or **Start** in the web UI) to enter **RUN**.

**Stop safely**

1. Tap the yellow button (Stop) or press **Stop** in the web UI.
2. The board cleanly disables outputs, turns relay off, disables bypass, and returns to **OFF**.

**Bring Wi‑Fi back if the page won’t load**

- Triple‑tap the yellow button quickly to re‑enable Wi‑Fi/AP and reconnect.

**Give a user access to only channels 1–3**

1. Log in as **Admin**.
2. Go to **User Settings** and enable access for outputs **1**, **2**, **3**.
3. Save. Now a **User** login can only toggle those channels.

---

## FAQ / Troubleshooting

**The webpage shows “Idle.” Does that mean the device is disconnected?**

- _Idle_ is a **real device state** (board is ready but not running). If the board disconnects, the web page will fail heartbeat checks and automatically redirect you to the AP login when it can’t reach the device.

**The power button in the web UI doesn’t match the hardware state.**

- The UI continuously syncs from the device and updates the power button and LED indicators accordingly. If it’s out of sync, refresh the page, or toggle **Start/Stop** once to resync.

**The AP vanished after a few minutes.**

- That’s expected: to save power, Wi‑Fi/AP auto‑sleeps when idle. Triple‑tap the yellow button to bring it back.

**I can’t control some outputs when logged in as User.**

- Those outputs are probably **not granted** to the User role. Ask an Admin to enable them under **User Settings**.

---

## Changelog

- v1.0 — Initial public release of the user guide.

---

## License

This documentation is provided as‑is under your project’s license.
