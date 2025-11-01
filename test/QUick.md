# Quick Start — Nichrome Power Board (for everyday users)

This board turns electric heating wires (nichrome) **ON/OFF**. You control it with **one yellow button** or from a **phone/computer**.

> ⚠️ **Safety first:** The heating wires get **hot**. Keep them away from anything that can burn. Don’t touch them while running.

---

## 1) The basics you’ll use

- **Yellow button** (on the board): start/stop & wake Wi-Fi
- **RGB light** (little LED on the board): shows what the board is doing
- **Web app** (on your phone/computer): start/stop, settings

---

## 2) Use it without the phone (just the yellow button)

1. **Power on** → the LED is **OFF** (board is idle)
2. **Tap the yellow button once** → the board **prepares** (brief amber pulses), then LED becomes **soft-green pulsing** = **Ready**
3. **Tap the yellow button again** → LED changes to **bright green double-pulse** = **Running**
4. **Tap once while Running** → it **stops** and returns to **OFF**

> Think “**tap to go forward**”: OFF → (tap) → READY → (tap) → RUN → (tap) → OFF

---

## 3) Use it with your phone (web app)

**First time or when not on your Wi-Fi:**

1. On your phone, open Wi-Fi and connect to the board’s network.

   - If you don’t see it, **triple-press the yellow button** to wake Wi-Fi.

2. Open a browser and go to **[http://192.168.4.1/login](http://192.168.4.1/login)**
3. Log in (you can change these later):

   - **Admin:** `admin` / `admin123`
   - **User:** `user` / `user123`

4. Press **Start** to run, **Stop** to stop.

**After you save your home Wi-Fi in the app:**

- The board will join your router. Next time, open the browser to
  **http://(the IP shown by your router)/login**
- Tip: check your router’s app or “connected devices” list to find the IP.

**If the page won’t load:**

- Wi-Fi may be sleeping to save power. **Triple-press the yellow button**, reconnect, and try again.

---

## 4) LED cheat-sheet (how to read the little light)

- **OFF (dark)** → Board is stopped
- **Soft-green, slow pulse** → **Ready** (waiting to start)
- **Bright green, double pulse** → **Running**
- **Amber breathing / yellow heartbeat** → **Preparing** or **Wi-Fi AP is active**
- **Fast red strobe** → **Error** → Tap to stop; check wiring/load before trying again
- **Quick green flash** → A good event happened (e.g., start, Wi-Fi joined)

> The LED may make short flashes (yellow/amber/green) when the **relay** or **fan** toggles—this is normal.

---

## 5) Handy tips

- **Buzzer too noisy?** In the web app, press **Mute** (it remembers your choice).
- **User can’t turn some outputs on?** That’s on purpose—an **Admin** must grant access.
- **Page stuck or “idle”?** Reconnect to the right network, or **triple-press** the yellow button and reload.

---

## 6) Quick start recipes

- **Fast hardware start (no phone):** tap once (prepare), wait for soft-green, tap again (run).
- **Stop:** tap once while running.
- **Wake Wi-Fi:** **triple-press** the yellow button.
- **First login (board’s own Wi-Fi):** connect phone → go to **192.168.4.1/login** → use the default login → Start.
