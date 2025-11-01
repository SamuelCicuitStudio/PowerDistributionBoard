# Quick Start — Nichrome Power Board (for everyday users)

This board turns electric heating wires (nichrome) **ON/OFF**. You can use the **yellow button** on the board or a **phone/computer**.

> ⚠️ **Safety:** The heating wires get **hot**. Keep them away from anything that can burn. Don’t touch while running.

---

## 1) What you’ll use

- **Yellow button** — start/stop & wake Wi-Fi
- **RGB light** — shows what the board is doing
- **Web app** — start/stop and basic settings

---

## 2) Use it without the phone (just the yellow button)

1. **Power on** → LED is **OFF** (stopped)
2. **Tap once** → board **prepares** (amber pulses), then **soft-green** = **Ready**
3. **Tap again** → **bright green double-pulse** = **Running**
4. **Tap once while Running** → **stops** and returns to **OFF**

> Think “**tap to go forward**”: OFF → (tap) → READY → (tap) → RUN → (tap) → OFF

---

## 3) Use it with your phone (web app)

**First time or when not on your home Wi-Fi (AP mode):**

1. On your phone, open Wi-Fi and connect to the board’s network.

   - If you don’t see it, **triple-press the yellow button** to wake Wi-Fi.
   - **AP Wi-Fi password (default): `1234567890`**

2. Open a browser and go to **[http://192.168.4.1/login](http://192.168.4.1/login)**
3. Log in (change later if you want):

   - **Admin:** `admin` / `admin123`
   - **User:** `user` / `user123`

4. Press **Start** to run, **Stop** to stop.

**After saving your home Wi-Fi (STA mode):**

- The board joins your router. Next time, open:
  **http://(router-assigned-IP)/login**
- Find that IP in your router’s “connected devices” list.

**If the page won’t load:**

- Wi-Fi may be sleeping. **Triple-press the yellow button**, reconnect to the board (use password **`1234567890`** in AP), and try again.

---

## 4) LED cheat-sheet

- **OFF (dark)** → Stopped
- **Soft-green, slow pulse** → **Ready**
- **Bright green, double pulse** → **Running**
- **Amber breathing / yellow heartbeat** → Preparing or **AP is active**
- **Fast red strobe** → **Error** → Tap to stop; check wiring/load before retry
- **Quick green flash** → Good event (start/joined Wi-Fi)

## RGB Status LED Guide

> Hardware note: the LED is **RG-only** (no blue). All colors use **Red** + **Green** mixes.

### Background States

These run continuously when no overlay is active:

| State     | Color (RG)   | Pattern & Tempo                                | Meaning                       |
| --------- | ------------ | ---------------------------------------------- | ----------------------------- |
| **OFF**   | LED off      | —                                              | Device is off / sleeping      |
| **WAIT**  | Amber        | **Breathe**, ~1200 ms period                   | Getting ready / early startup |
| **IDLE**  | Soft green   | **Double heartbeat**, ~2000 ms period          | Standing by, ready            |
| **RUN**   | Bright green | **Double heartbeat**, ~1400 ms period          | Actively running              |
| **FAULT** | Red          | **Fast strobe**, ~50 ms on / 75 ms off (~8 Hz) | Fault condition (investigate) |
| **MAINT** | Amber        | **Blink**, ~900 ms period                      | Maintenance / special mode    |

> Short yellow/amber/green flashes when the **relay** or **fan** toggles are normal.

---

## 5) Handy tips

- **Mute buzzer** in the web app (remembers your choice).
- **User can’t toggle some outputs?** An **Admin** must grant access.
- **Page looks stuck or idle?** Reconnect to the right network, or **triple-press** the yellow button and reload.

---

## 6) Quick recipes

- **Fast hardware start (no phone):** tap once (prepare) → wait soft-green → tap again (run).
- **Stop:** tap once while running.
- **Wake Wi-Fi:** **triple-press** the yellow button.
- **First login (AP):** connect to board Wi-Fi → **password `1234567890`** → go to **192.168.4.1/login** → log in → Start.
