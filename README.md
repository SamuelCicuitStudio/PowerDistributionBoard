# Power Distribution Board (Nichrome) — User Guide

This board controls **10 nichrome-wire outputs**, an **inrush (bypass) MOSFET**, and an **input relay**, with a built-in HTTP API for live control and configuration used by the Windows desktop apps. It also features a **buzzer** and an **RGB status LED** that communicates system state.

> **Quick highlights**
>
> - One physical **yellow button** on the board for **power/start/stop** and **Wi‑Fi wake** actions.
> - One **BOOT button (IO0)** for a **safe full reset** and as a **hardware wake source** from deep sleep.
> - Two app roles: **Admin** and **User** (with **per-output access control**).
> - Desktop apps have **Dashboard**, **Manual**, **User Settings**, and **Admin Settings** views.
> - Wi-Fi client mode (**STA**) with **auto-fallback AP** if credentials fail.
> - **Wi‑Fi auto-sleeps** after inactivity. **Triple-tap the yellow button** to wake Wi‑Fi again.
> - The whole board can enter **deep sleep** when Wi‑Fi is disabled and the device is not running; wake it with the **yellow button** or **BOOT** button.
> - Wi‑Fi may be sleeping. **Triple-press the yellow button**, reconnect to the board (use password **`1234567890`** in AP), and try again.

---

## Table of Contents

- [Hardware Overview](#hardware-overview)
- [Safety Notes](#safety-notes)
- [Physical Controls (Yellow Button)](#physical-controls-yellow-button)
- [Wi-Fi Behavior (AP / STA / Auto-Sleep)](#wi-fi-behavior-ap--sta--auto-sleep)
- [UI](#ui)
- [First-Time Setup](#first-time-setup)
- [Finding the Board’s IP](#finding-the-boards-ip)
- [Web Interface](#web-interface)
  - [Dashboard](#dashboard)
  - [Manual](#manual)
  - [User Settings](#user-settings)
  - [Admin Settings](#admin-settings)
- [Access Control (User vs Admin)](#access-control-user-vs-admin)
- [Buzzer & Mute](#buzzer--mute)
- [RGB Status LED Guide](#rgb-status-led-guide)
  - [Background States](#background-states)
  - [Overlay Events](#overlay-events)
  - [Pattern Primitives](#pattern-primitives)
- [Typical Workflows](#typical-workflows)
- [FAQ / Troubleshooting](#faq--troubleshooting)
- [Appendix: Defaults & Endpoints](#appendix-defaults--endpoints)
- [Changelog](#changelog)
- [License](#license)

---

## Hardware Overview

- **Yellow Button**  
  Single onboard push button used to **wake/start/stop** the system and to **wake Wi-Fi** (triple-press). See the detailed flows below.

- **BOOT Button (IO0)**  
  Second physical button typically near the USB connector. A **long hold** triggers a **full reset** via the firmware (safe shutdown + restart). It is also configured as a **deep‑sleep wake source** together with the yellow button.

- **10 Nichrome Outputs**  
  Individually switchable channels for heating loads (nichrome wires). You can enable/disable each output in **Manual** mode and enforce end-user access in **User Settings**.

- **Inrush (Bypass) MOSFET**  
  Engages after the capacitor bank reaches threshold to reduce series losses during operation.

- **Input Relay**  
  Brings input power to the charging stage during startup; opens on stop/shutdown.

- **RGB Status LED**  
  Shows **background states** (OFF / IDLE / RUN / FAULT / etc.) plus **short overlay hints** (Wi-Fi events, app activity, relay/fan toggles, warnings).

- **Buzzer**  
  Provides short audible feedback for important actions; can be **muted** from the desktop app (persists).

> **Electrical note:** Nichrome elements are **resistive heaters**. Use appropriate wire gauge and thermal management. **Never leave the system unattended** without safeguards.

---

## Safety Notes

- Nichrome elements get **hot**. Mount on **non-flammable** surfaces with ventilation.
- Verify **wiring and fusing**. Respect current limits per channel and overall supply.
- Keep separation between **high-current** paths and **logic** wiring.
- Use **PPE** and a **bench supply** during first power-up and tests.

---

## Physical Controls (Yellow Button)

The device boots **OFF** (LED dark; no outputs). The yellow button advances through the power sequence:

- **Tap #1 (from OFF)** → **Wake & Prepare**  
  The **input relay** closes; the board **charges the capacitor bank** up to the configured threshold. When threshold is reached, the **bypass MOSFET** engages and the system transitions to **IDLE** (ready).  
  LED shows charging and transition overlays (see LED section).

- **Tap #2 (from IDLE)** → **RUN**  
  Enters the main loop. Outputs follow configured **on/off timings** and any **access rules** in place.

- **Tap (from RUN)** → **OFF**  
  Requests a **clean stop**: outputs are disabled, the relay opens, bypass disengages, and LED returns to OFF.

- **Long-press (any state)** → **Restart**  
  Press-and-hold a few seconds to reboot the device (use only when necessary).

- **Triple-press (quick)** → **Wake Wi-Fi**  
  If the AP/STA has auto-slept due to inactivity, **press the yellow button three times quickly** to wake Wi-Fi back up so you can connect again.

> **Mental model:** A **quick tap** always means “**advance one step**”:  
> OFF → _(tap)_ → IDLE → _(tap)_ → RUN → _(tap)_ → OFF.

---

## Wi-Fi Behavior (AP / STA / Auto-Sleep)

The board includes an HTTP API server and supports two Wi-Fi modes:

1. **STA (Station / Client)** — connects to your router
2. **AP (Access Point)** — the board creates its own Wi-Fi network

### Boot Sequence

- On boot, the board **tries saved STA credentials**.
  - If it connects to your router, the API is reachable at the **router-assigned IP**.
  - If it **fails** to connect within its internal timeout, it **falls back to AP mode** so you can still reach the device.
- In **AP mode**, connect to the board’s Wi-Fi and open:  
  **http://192.168.4.1/**  
  **Default AP password:** `1234567890`

### AP Auto-Sleep (Power Saving)

- To save power, the device monitors app activity and **sleeps Wi-Fi** when idle. When this happens, the AP disappears and any open app session will stop responding.
- To wake Wi-Fi again, **triple-press the yellow button** and reconnect.

### Switching AP → STA

- After you save valid STA credentials and the device joins your network, the **IP address changes** to the one assigned by your router.
- **Important:** Your phone/computer must be on the **same network** as the board. Point the app to `http://<router-assigned-ip>/`.

---

## UI

This section previews the legacy web interface (no longer served by firmware). All screenshots are stored in the repository at **`ui/`**. Click any thumbnail to open the full-size image.

> **Notes**  
> • Paths are **case-sensitive** on GitHub: use `ui/` (lowercase).  
> • Filenames with spaces/accents are URL-encoded below. If you rename files, update links here.

<table>
<tr>
<td align="center" valign="top">
<a href="Ui/Capture%20d%E2%80%99%C3%A9cran%202025-11-01%20042258.png">
<img src="Ui/Capture%20d%E2%80%99%C3%A9cran%202025-11-01%20042258.png" alt="Capture d’écran 2025-11-01 042258" width="300"></a><br/><sub>Capture d’écran 2025-11-01 042258</sub>
</td>
<td align="center" valign="top">
<a href="Ui/Capture%20d%E2%80%99%C3%A9cran%202025-11-01%20042326.png">
<img src="Ui/Capture%20d%E2%80%99%C3%A9cran%202025-11-01%20042326.png" alt="Capture d’écran 2025-11-01 042326" width="300"></a><br/><sub>Capture d’écran 2025-11-01 042326</sub>
</td>
<td align="center" valign="top">
<a href="Ui/Capture%20d%E2%80%99%C3%A9cran%202025-11-01%20042345.png">
<img src="Ui/Capture%20d%E2%80%99%C3%A9cran%202025-11-01%20042345.png" alt="Capture d’écran 2025-11-01 042345" width="300"></a><br/><sub>Capture d’écran 2025-11-01 042345</sub>
</td>
</tr>

<tr>
<td align="center" valign="top">
<a href="Ui/Capture%20d%E2%80%99%C3%A9cran%202025-11-01%20042357.png">
<img src="Ui/Capture%20d%E2%80%99%C3%A9cran%202025-11-01%20042357.png" alt="Capture d’écran 2025-11-01 042357" width="300"></a><br/><sub>Capture d’écran 2025-11-01 042357</sub>
</td>
<td align="center" valign="top">
<a href="Ui/Capture%20d%E2%80%99%C3%A9cran%202025-11-01%20042411.png">
<img src="Ui/Capture%20d%E2%80%99%C3%A9cran%202025-11-01%20042411.png" alt="Capture d’écran 2025-11-01 042411" width="300"></a><br/><sub>Capture d’écran 2025-11-01 042411</sub>
</td>
<td align="center" valign="top">
<a href="Ui/Capture%20d%E2%80%99%C3%A9cran%202025-11-01%20042423.png">
<img src="Ui/Capture%20d%E2%80%99%C3%A9cran%202025-11-01%20042423.png" alt="Capture d’écran 2025-11-01 042423" width="300"></a><br/><sub>Capture d’écran 2025-11-01 042423</sub>
</td>
</tr>

<tr>
<td align="center" valign="top">
<a href="Ui/Capture%20d%E2%80%99%C3%A9cran%202025-11-01%20042436.png">
<img src="Ui/Capture%20d%E2%80%99%C3%A9cran%202025-11-01%20042436.png" alt="Capture d’écran 2025-11-01 042436" width="300"></a><br/><sub>Capture d’écran 2025-11-01 042436</sub>
</td>
<td align="center" valign="top">
<a href="Ui/Capture%20d%E2%80%99%C3%A9cran%202025-11-01%20042450.png">
<img src="Ui/Capture%20d%E2%80%99%C3%A9cran%202025-11-01%20042450.png" alt="Capture d’écran 2025-11-01 042450" width="300"></a><br/><sub>Capture d’écran 2025-11-01 042450</sub>
</td>
<td align="center" valign="top">
<a href="Ui/Capture%20d%E2%80%99%C3%A9cran%202025-11-01%20042508.png">
<img src="Ui/Capture%20d%E2%80%99%C3%A9cran%202025-11-01%20042508.png" alt="Capture d’écran 2025-11-01 042508" width="300"></a><br/><sub>Capture d’écran 2025-11-01 042508</sub>
</td>
</tr>

<tr>
<td align="center" valign="top">
<a href="Ui/Capture%20d%E2%80%99%C3%A9cran%202025-11-01%20042523.png">
<img src="Ui/Capture%20d%E2%80%99%C3%A9cran%202025-11-01%20042523.png" alt="Capture d’écran 2025-11-01 042523" width="300"></a><br/><sub>Capture d’écran 2025-11-01 042523</sub>
</td>
<td align="center" valign="top">
<a href="Ui/Capture%20d%E2%80%99%C3%A9cran%202025-11-01%20042536.png">
<img src="Ui/Capture%20d%E2%80%99%C3%A9cran%202025-11-01%20042536.png" alt="Capture d’écran 2025-11-01 042536" width="300"></a><br/><sub>Capture d’écran 2025-11-01 042536</sub>
</td>
<td>
</td>
</tr>
</table>

**Open the folder directly:** [`Ui/`](Ui/)

> **Add more screenshots:**
>
> 1. Drop `.png`/`.jpg` files into `Ui/`.
> 2. Copy one of the `<td>…</td>` blocks above and change the filename.
> 3. Commit the README change. GitHub will render the new image automatically.

---

## First-Time Setup

1. **Power the board** and wait ~10–15 seconds for initialization.
2. If STA join fails, connect to the board’s **AP** from your phone/PC and open:  
   **http://192.168.4.1/** (AP **password `1234567890`**)
3. **Default credentials** (change these immediately):
   - **Admin**: `admin` / `admin123`
   - **User**: `user` / `user123`
4. From the desktop app, enter your **Wi-Fi (STA)** credentials. Once connected, the board’s **IP will be the router-assigned IP** (see next section to find it).
5. If the AP disappears while you’re working, it’s likely idle auto-sleep. **Triple-press** the yellow button and retry.

---

## Finding the Board’s IP

After the board joins your router:

- Check your router’s **Connected Devices** page for the board’s **IP address**.
- Optionally try **mDNS** (if supported on your network): `http://pdis.local`
- A local **network scanner** app can also discover the device.

> If you can’t reach the board and it was previously in AP mode, your app may still be pointing to `192.168.4.1`. Move to your normal Wi-Fi network and use the **router-assigned IP** instead.

---

## Web Interface

You’ll see different capabilities depending on your role (**Admin** or **User**). The UI is divided into tabs.

### Dashboard

**Purpose:** Quick status and high-level actions.

- **Mode: Auto ↔ Manual**  
  Switch the main loop mode.
- **LT (LED Feedback)**  
  Toggle per-channel LED feedback while running.
- **Ready / OFF indicators**  
  Visual status mirrored from the device.
- **Power Button**
  - **Start** (OFF/IDLE → RUN)
  - **Stop** (RUN → OFF)
  - The button text/state mirrors the device; it disables itself when an action is not valid.
- **Mute (Buzzer)**  
  Toggles a **persistent** mute flag for the buzzer.
- **System**  
  **Reboot** and **Factory Reset** controls (use carefully).
- **Live Gauges (if present)**  
  Input voltage/current, temperatures, etc.

### Manual

**Purpose:** Direct, per-device control (when Manual mode is active).

- **Outputs 1–10** — On/Off per channel
- **Input Relay** — On/Off
- **Bypass MOSFET** — On/Off
- **Fan** — Duty slider
- **Timing** — On-Time / Off-Time; **Save** to preferences
- **Electrical Parameters** — AC frequency, charge resistor, DC voltage, desired output voltage

> **Note:** Manual actions for **User** are limited by the **Access** flags set by **Admin** (see below).

### User Settings

- Change **User username/password**.
- **Per-output access**: grant or deny the **User role** control over each output (1–10).
- **Save** applies immediately.

### Admin Settings

- Change **Admin username/password**.
- Use strong passwords and store them safely.

---

## Access Control (User vs Admin)

- **Admin** can **control everything**: all outputs, relay, bypass, fan, timings, electrical parameters, credentials.
- **User** control is limited by **per-output access flags** set in **User Settings**. If an output is **not granted**, clicking it in the UI will have **no effect**.

---

## Buzzer & Mute

- The **Mute** button flips a **persisted** flag. When muted, buzzer beeps are suppressed except for critical cases.
- You can unmute at any time from the desktop app.

---

## RGB Status LED Guide

> Hardware note: the LED is **RG-only** (no blue die is wired). All colors below are **Red + Green** mixes. Chips show the **exact hex** the firmware uses.

### Palette (quick reference)

- **OFF** — `#000000`  
  <img alt="#000000" src="https://placehold.co/16x16/000000/000000.png" />
- **Green (OK)** — `#00FF00`  
  <img alt="#00FF00" src="https://placehold.co/16x16/00FF00/00FF00.png" />
- **Soft Green (Idle)** — `#00B400`  
  <img alt="#00B400" src="https://placehold.co/16x16/00B400/00B400.png" />
- **Bright Green (Run)** — `#00DC00`  
  <img alt="#00DC00" src="https://placehold.co/16x16/00DC00/00DC00.png" />
- **Amber** — `#FF7800`  
  <img alt="#FF7800" src="https://placehold.co/16x16/FF7800/FF7800.png" />
- **Yellow** — `#FFC800`  
  <img alt="#FFC800" src="https://placehold.co/16x16/FFC800/FFC800.png" />
- **Red (Fault/Crit)** — `#FF0000`  
  <img alt="#FF0000" src="https://placehold.co/16x16/FF0000/FF0000.png" />
- **Soft White (RG)** — `#787800`  
  <img alt="#787800" src="https://placehold.co/16x16/787800/787800.png" />
- **Greenish (Bypass)** — `#00B43C`  
  <img alt="#00B43C" src="https://placehold.co/16x16/00B43C/00B43C.png" />
- **Admin Pulse (Orange-Red)** — `#C83C00`  
  <img alt="#C83C00" src="https://placehold.co/16x16/C83C00/C83C00.png" />
- **User Pulse (Green Pulse)** — `#3CC800`  
  <img alt="#3CC800" src="https://placehold.co/16x16/3CC800/3CC800.png" />
- **Net Recover (Green OK)** — `#00DC00`  
  <img alt="#00DC00" src="https://placehold.co/16x16/00DC00/00DC00.png" />

### Background States

These run continuously when no overlay is active:

| State     | Hex       | Chip                                                                     | Pattern & Tempo                                | Meaning                       |
| --------- | --------- | ------------------------------------------------------------------------ | ---------------------------------------------- | ----------------------------- |
| **OFF**   | `#000000` | <img alt="#000000" src="https://placehold.co/16x16/000000/000000.png" /> | —                                              | Device is off / sleeping      |
| **WAIT**  | `#FF7800` | <img alt="#FF7800" src="https://placehold.co/16x16/FF7800/FF7800.png" /> | **Breathe**, ~1200 ms period                   | Getting ready / early startup |
| **IDLE**  | `#00B400` | <img alt="#00B400" src="https://placehold.co/16x16/00B400/00B400.png" /> | **Double heartbeat**, ~2000 ms period          | Standing by, ready            |
| **RUN**   | `#00DC00` | <img alt="#00DC00" src="https://placehold.co/16x16/00DC00/00DC00.png" /> | **Double heartbeat**, ~1400 ms period          | Actively running              |
| **FAULT** | `#FF0000` | <img alt="#FF0000" src="https://placehold.co/16x16/FF0000/FF0000.png" /> | **Fast strobe**, ~50 ms on / 75 ms off (~8 Hz) | Fault condition (investigate) |
| **MAINT** | `#FF7800` | <img alt="#FF7800" src="https://placehold.co/16x16/FF7800/FF7800.png" /> | **Blink**, ~900 ms period                      | Maintenance / special mode    |

**Heartbeat shape.** Two short beats per period (beat, gap, beat, rest).

### Overlay Events

Short, higher-priority hints that temporarily preempt the background. (Durations/tempos are fixed for consistency.)

#### Power / Power Path

- **Wake flash** — `#787800`  
  <img alt="#787800" src="https://placehold.co/16x16/787800/787800.png" /> — Soft white, **flash once** when a tap is registered
- **Relay ON** — `#FFC800`  
  <img alt="#FFC800" src="https://placehold.co/16x16/FFC800/FFC800.png" /> — Short **yellow flash**
- **Relay OFF** — `#FF7800`  
  <img alt="#FF7800" src="https://placehold.co/16x16/FF7800/FF7800.png" /> — Short **amber flash**
- **Charging** — `#FFA000`  
  <img alt="#FFA000" src="https://placehold.co/16x16/FFA000/FFA000.png" /> — Amber **breathe** (≤ 1/s) while capacitor charges
- **Threshold OK** — `#00DC00`  
  <img alt="#00DC00" src="https://placehold.co/16x16/00DC00/00DC00.png" /> — Short **green flash**
- **Bypass ON** — `#00B43C`  
  <img alt="#00B43C" src="https://placehold.co/16x16/00B43C/00B43C.png" /> — Short **greenish flash**
- **Wait Button** — `#787800`  
  <img alt="#787800" src="https://placehold.co/16x16/787800/787800.png" /> — **Yellow heartbeat** while awaiting start
- **Start (enter RUN)** — `#00C800`  
  <img alt="#00C800" src="https://placehold.co/16x16/00C800/00C800.png" /> — **Flash once** on transition to RUN

#### Wi-Fi / Web

- **Wi-Fi AP up** — `#FFC800`  
  <img alt="#FFC800" src="https://placehold.co/16x16/FFC800/FFC800.png" /> — **Double heartbeat** (~1.5 s) for a few seconds
- **Wi-Fi STA joined** — `#00FF00`  
  <img alt="#00FF00" src="https://placehold.co/16x16/00FF00/00FF00.png" /> — **Quick green flash**
- **Wi-Fi lost** — `#FF7800`  
  <img alt="#FF7800" src="https://placehold.co/16x16/FF7800/FF7800.png" /> — **Amber blink**
- **Web Admin active** — `#C83C00`  
  <img alt="#C83C00" src="https://placehold.co/16x16/C83C00/C83C00.png" /> — **Orange-red breathe** (short window)
- **Web User active** — `#3CC800`  
  <img alt="#3CC800" src="https://placehold.co/16x16/3CC800/3CC800.png" /> — **Green breathe** (short window)
- **Network recovered** — `#00DC00`  
  <img alt="#00DC00" src="https://placehold.co/16x16/00DC00/00DC00.png" /> — **Green flash** on reconnect

#### Fan / Relay / Outputs

- **Fan ON** — `#00FF00`  
  <img alt="#00FF00" src="https://placehold.co/16x16/00FF00/00FF00.png" /> — Quick **green flash**
- **Fan OFF** — `#FF7800`  
  <img alt="#FF7800" src="https://placehold.co/16x16/FF7800/FF7800.png" /> — Quick **amber flash**
- **Relay ON** — `#FFC800`  
  <img alt="#FFC800" src="https://placehold.co/16x16/FFC800/FFC800.png" /> — Quick **yellow flash**
- **Relay OFF** — `#FF7800`  
  <img alt="#FF7800" src="https://placehold.co/16x16/FF7800/FF7800.png" /> — Quick **amber flash**
- **Output toggled (ON)** — `#00FF00`  
  <img alt="#00FF00" src="https://placehold.co/16x16/00FF00/00FF00.png" /> — Short **green flash** (may repeat **N** times = channel index)
- **Output toggled (OFF)** — `#FF7800`  
  <img alt="#FF7800" src="https://placehold.co/16x16/FF7800/FF7800.png" /> — Short **amber flash** (may repeat **N** times)

#### Limits / Battery

- **Temperature Warn** — `#FFC800`  
  <img alt="#FFC800" src="https://placehold.co/16x16/FFC800/FFC800.png" /> — **Yellow blink** (warning tempo)
- **Temperature Critical** — `#FF0000`  
  <img alt="#FF0000" src="https://placehold.co/16x16/FF0000/FF0000.png" /> — **Red rapid blink**
- **Current Warn** — `#FFC800`  
  <img alt="#FFC800" src="https://placehold.co/16x16/FFC800/FFC800.png" /> — **Yellow blink** (warning tempo)
- **Current Trip** — `#FF0000`  
  <img alt="#FF0000" src="https://placehold.co/16x16/FF0000/FF0000.png" /> — **Red rapid blink**
- **Low Battery** — `#FFC800`  
  <img alt="#FFC800" src="https://placehold.co/16x16/FFC800/FFC800.png" /> — **Yellow attention blinks**
- **Critical Battery** — `#FF0000`  
  <img alt="#FF0000" src="https://placehold.co/16x16/FF0000/FF0000.png" /> — **Red rapid blinks**

**Priority model:** _Background_ < _Action_ < _Alert_ < _Critical_. Equal/higher priority overlays preempt; background resumes automatically when they end.

### Pattern Primitives

- **FLASH_ONCE** — Solid ON for _onMs_, then off.
- **BLINK** — Equal ON/OFF slices within a period.
- **BREATHE** — Smooth ramp up/down (soft glow).
- **HEARTBEAT2** — Two short beats per period (beat, gap, beat, rest).
- **STROBE** — Rapid on/off via explicit on/off timings (used by **FAULT**).

---

## Typical Workflows

### Start a job (hardware-only, no web)

1. Ensure wiring and loads are safe; power the board (LED off).
2. **Tap the yellow button once** to **Wake & Prepare**.  
   Watch the LED: relay ON → charging pulses → threshold OK → bypass ON → **IDLE**.
3. **Tap the yellow button again** to enter **RUN** (main loop).
4. To stop, **tap once** while running. The board cleanly disables outputs, opens the relay, and returns to **OFF**.

### Start a job (from the desktop app)

1. Connect to **AP** (`http://192.168.4.1/`, AP **password `1234567890`**) or the **router IP** (STA).
2. Log in (**Admin** or **User**).
3. On the **Dashboard**, click **Start** (or **Stop** when running).
4. If the AP disappears while idle, **triple-press** the yellow button and reconnect.

### Give a user access to only channels 1–3

1. Log in as **Admin**.
2. Open **User Settings** and enable outputs **1**, **2**, **3**.
3. Save. User accounts can now toggle only those channels.

---

## FAQ / Troubleshooting

**The app says “Idle”. Is the device disconnected?**  
No. **Idle** is a **real device state** (board is ready, not running). Disconnection looks different: your app won’t update, buttons seem unresponsive, or the AP has vanished (auto-sleep).

**The AP vanished after a few minutes.**  
Expected: **auto-sleep** saves power when the app is idle. **Triple-press** the yellow button to wake Wi-Fi (AP/STA), reconnect the app.

**I switched to STA but can’t reach the device.**  
You’re likely still on `192.168.4.1` (AP). After joining your router, the board API is reachable at the **router-assigned IP**. Find it in your router’s device list and point the app to `http://<that-ip>/`.

**The power button in the UI doesn’t match the hardware state.**  
The UI mirrors the device. If it looks out of sync, restart the app or **toggle Start/Stop** once—status will re-sync.

**I’m logged in as User but some outputs won’t toggle.**  
They’re likely **not granted** to the User role. Ask an **Admin** to enable those outputs in **User Settings**.

**The buzzer is noisy.**  
Use **Mute** on the Dashboard. The **mute flag persists** until you unmute.

**Wi-Fi keeps sleeping while I’m on the page.**  
Keep at least one tab **active**, or interact periodically. If it sleeps, **triple-press** the yellow button and reconnect.

**I forgot the credentials.**  
Use **Admin Settings** (if you still have access) to reset passwords. If fully locked out, perform a **factory reset** (from the System section) or follow your project’s hardware recovery procedure.

---

## Appendix: Defaults & Endpoints

**Default logins** (change after first use):

- **Admin**: `admin` / `admin123`
- **User**: `user` / `user123`

**AP base URL:** `http://192.168.4.1/` (AP **password `1234567890`**)  
**STA base URL:** `http://<router-assigned-ip>/`

**Common endpoints (for reference):**

> All authenticated endpoints require `X-Session-Token` header or `?token=` query param.


- `GET /login` — legacy (404 JSON; no HTML)
- `POST /connect` — Authenticate; returns `{ok, role, token}`
- `POST /disconnect` — Logout (token required)
- `GET /monitor` — Live status snapshot (token required)
- `GET /load_controls` — Control panel snapshot (token required)
- `POST /control` — Command API (token required)
- `GET /heartbeat` — Keeps session alive; prevents Wi-Fi auto-sleep (token required)

> **Port:** HTTP **80** (default). Use the Windows desktop apps.

---

## Changelog

- v1.0 — Initial public release of the user guide.

---

## License

This documentation is provided as-is under your project’s license.
