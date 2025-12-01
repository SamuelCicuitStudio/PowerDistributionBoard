# RGB LED Map

Full-RGB status LED driven by dedicated LEDC channels (R/G/B). All colors below are 24-bit RGB.

## Patterns
- OFF — LEDs off.
- SOLID — steady color.
- BLINK — on/off with duty.
- BREATHE — smooth fade in/out.
- HEARTBEAT2 — double pulse per cycle.
- FLASH_ONCE — single pulse (short overlay cue).
- STROBE — fast on/off for critical/fault.

## Background states
- **BOOT / INIT / PAIRING** — `#FFAA00`  
  <img alt="#FFAA00" src="https://placehold.co/16x16/FFAA00/FFAA00.png" /> — Amber **breathe** while firmware is starting or pairing.

- **START** — `#00DC3C`  
  <img alt="#00DC3C" src="https://placehold.co/16x16/00DC3C/00DC3C.png" /> — Bright green **double heartbeat** during power-up sequence.

- **IDLE / READY** — `#3CC83C`  
  <img alt="#3CC83C" src="https://placehold.co/16x16/3CC83C/3CC83C.png" /> — Soft green **heartbeat** when the system is ready/safe.

- **RUN** — `#00FF78`  
  <img alt="#00FF78" src="https://placehold.co/16x16/00FF78/00FF78.png" /> — Bright green **double heartbeat** while actively delivering power.

- **WAIT (12V / button / ready)** — `#FFAA00`  
  <img alt="#FFAA00" src="https://placehold.co/16x16/FFAA00/FFAA00.png" /> — Amber **heartbeat** while waiting for supply or start condition.

- **MAINT** — `#008CFF`  
  <img alt="#008CFF" src="https://placehold.co/16x16/008CFF/008CFF.png" /> — Blue **breathe** in maintenance/service mode.

- **OFF / SLEEP** — `#000000`  
  <img alt="#000000" src="https://placehold.co/16x16/000000/000000.png" /> — LED **off**.

- **FAULT** — `#FF0000`  
  <img alt="#FF0000" src="https://placehold.co/16x16/FF0000/FF0000.png" /> — Red **strobe** (fast) for latched critical fault.

## Overlays (short cues that preempt backgrounds)

### Wi‑Fi & Web
- **Wi‑Fi STA joined** — `#00FF00`  
  <img alt="#00FF00" src="https://placehold.co/16x16/00FF00/00FF00.png" /> — Short green **flash** when joining the router.

- **Wi‑Fi AP active** — `#FFE628`  
  <img alt="#FFE628" src="https://placehold.co/16x16/FFE628/FFE628.png" /> — Short yellow **flash** when AP is active.

- **Wi‑Fi link lost** — `#FFAA00`  
  <img alt="#FFAA00" src="https://placehold.co/16x16/FFAA00/FFAA00.png" /> — Amber **flash** when Wi‑Fi link drops.

- **Network recovered** — `#00DCB4`  
  <img alt="#00DCB4" src="https://placehold.co/16x16/00DCB4/00DCB4.png" /> — Teal/green **flash** when connectivity returns.

- **Web admin active** — `#FF7828`  
  <img alt="#FF7828" src="https://placehold.co/16x16/FF7828/FF7828.png" /> — Orange‑red **flash** when an admin connects.

- **Web user active** — `#00DC8C`  
  <img alt="#00DC8C" src="https://placehold.co/16x16/00DC8C/00DC8C.png" /> — Teal/green **flash** when a user connects.

### Fan / Relay / Bypass / Discharge
- **Fan ON** — `#00C8FF`  
  <img alt="#00C8FF" src="https://placehold.co/16x16/00C8FF/00C8FF.png" /> — Cyan **flash** when a fan starts.

- **Fan OFF** — `#FFAA00`  
  <img alt="#FFAA00" src="https://placehold.co/16x16/FFAA00/FFAA00.png" /> — Amber **flash** when a fan stops.

- **Relay ON** — `#FFE628`  
  <img alt="#FFE628" src="https://placehold.co/16x16/FFE628/FFE628.png" /> — Short yellow **flash** when relay closes.

- **Relay OFF** — `#FFAA00`  
  <img alt="#FFAA00" src="https://placehold.co/16x16/FFAA00/FFAA00.png" /> — Short amber **flash** when relay opens.

- **Bypass ON** — `#00DC8C`  
  <img alt="#00DC8C" src="https://placehold.co/16x16/00DC8C/00DC8C.png" /> — Teal **flash** when bypass is enabled.

- **Discharge active** — `#FFD250`  
  <img alt="#FFD250" src="https://placehold.co/16x16/FFD250/FFD250.png" /> — Yellow **breathe** while capacitor discharge runs.

- **Discharge done** — `#00DC78`  
  <img alt="#00DC78" src="https://placehold.co/16x16/00DC78/00DC78.png" /> — Green **flash** when discharge completes.

### Power path & protections
- **12V lost** — `#FF5014`  
  <img alt="#FF5014" src="https://placehold.co/16x16/FF5014/FF5014.png" /> — Orange‑red **strobe** when 12V supply drops while active.

- **DC bus low** — `#FFAA00`  
  <img alt="#FFAA00" src="https://placehold.co/16x16/FFAA00/FFAA00.png" /> — Amber **blink** when DC bus is below threshold.

- **Over‑current trip** — `#FF0000`  
  <img alt="#FF0000" src="https://placehold.co/16x16/FF0000/FF0000.png" /> — Red **strobe burst** on over‑current protection.

- **Temp warn** — `#FFE628`  
  <img alt="#FFE628" src="https://placehold.co/16x16/FFE628/FFE628.png" /> — Yellow **blink** for temperature warning.

- **Temp critical** — `#FF0000`  
  <img alt="#FF0000" src="https://placehold.co/16x16/FF0000/FF0000.png" /> — Red **strobe** for critical over‑temperature.

- **Thermal channel lock** — `#FFB428`  
  <img alt="#FFB428" src="https://placehold.co/16x16/FFB428/FFB428.png" /> — Yellow **blink** when some channels are thermally locked.

- **Thermal global lock** — `#FF6414`  
  <img alt="#FF6414" src="https://placehold.co/16x16/FF6414/FF6414.png" /> — Orange **strobe** when all channels are thermally locked.

- **Sensor missing** — `#50A0FF`  
  <img alt="#50A0FF" src="https://placehold.co/16x16/50A0FF/50A0FF.png" /> — Blue **blink** when a sensor is missing/invalid.

- **Config error** — `#FF3CB4`  
  <img alt="#FF3CB4" src="https://placehold.co/16x16/FF3CB4/FF3CB4.png" /> — Magenta **strobe** on configuration/NVS error.

- **Bypass forced OFF** — `#FF9628`  
  <img alt="#FF9628" src="https://placehold.co/16x16/FF9628/FF9628.png" /> — Amber **flash** when bypass is forced off for safety.

### Power‑up sequence
- **Wait 12V** — `#FFAA00`  
  <img alt="#FFAA00" src="https://placehold.co/16x16/FFAA00/FFAA00.png" /> — Amber **breathe** while waiting for 12V.

- **Charging** — `#FFC83C`  
  <img alt="#FFC83C" src="https://placehold.co/16x16/FFC83C/FFC83C.png" /> — Yellow‑amber **breathe** while capacitor is charging.

- **Threshold OK** — `#00DC50`  
  <img alt="#00DC50" src="https://placehold.co/16x16/00DC50/00DC50.png" /> — Green **flash** when charge threshold is reached.

- **Bypass ON (power‑up)** — `#00DC8C`  
  <img alt="#00DC8C" src="https://placehold.co/16x16/00DC8C/00DC8C.png" /> — Teal **flash** when bypass engages in the sequence.

- **Wait button** — `#DCB450`  
  <img alt="#DCB450" src="https://placehold.co/16x16/DCB450/DCB450.png" /> — Amber **heartbeat** while waiting for user start.

- **Start (enter RUN)** — `#00DC78`  
  <img alt="#00DC78" src="https://placehold.co/16x16/00DC78/00DC78.png" /> — Green **flash** when entering RUN state.

### Outputs
- **Channel ON** — `#00FF78`  
  <img alt="#00FF78" src="https://placehold.co/16x16/00FF78/00FF78.png" /> — Green/teal **pulse** when an output is enabled.

- **Channel OFF** — `#FFAA00`  
  <img alt="#FFAA00" src="https://placehold.co/16x16/FFAA00/FFAA00.png" /> — Amber **pulse** when an output is disabled.

### General
- **Wake flash** — `#DCDCDC`  
  <img alt="#DCDCDC" src="https://placehold.co/16x16/DCDCDC/DCDCDC.png" /> — Soft white **flash once** on wake/tap.

- **Reset trigger** — `#DCDCDC`  
  <img alt="#DCDCDC" src="https://placehold.co/16x16/DCDCDC/DCDCDC.png" /> — Soft white **flash** when reset is requested.

- **Low battery** — `#FFE628`  
  <img alt="#FFE628" src="https://placehold.co/16x16/FFE628/FFE628.png" /> — Yellow **blink** while battery is low.

- **Critical battery** — `#FF0000`  
  <img alt="#FF0000" src="https://placehold.co/16x16/FF0000/FF0000.png" /> — Red **strobe burst** when battery is critically low.

Color semantics: green/teal = good/active, yellow/amber = pending/caution, red = fault/critical, blue/magenta = configuration/sensor issues, white = neutral system cue.
