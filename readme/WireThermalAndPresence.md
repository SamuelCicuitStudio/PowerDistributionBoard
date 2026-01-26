# Wire Thermal and Presence (Implementation Spec)

This document defines the required behavior for wire thermal estimation and wire presence detection. It is a spec for implementation and calibration, not a description of current firmware behavior.

## Scope and goals
- Maintain per-wire virtual temperature for safety and UI.
- Detect wire disconnection immediately while any heating is active.
- Keep the logic modular: thermal math and presence detection are pure logic; Device orchestrates.

## Definitions
- `Vbus`: measured bus voltage during heating.
- `Rwire[i]`: cold resistance for wire i (from NVS).
- `mask`: active output bitmask.
- `Imeas`: measured total current (ACS or estimate).
- `Ileak`: baseline current through divider + charge resistor path when outputs are OFF.
- `Iexp`: expected load current based on `Vbus` and `Rwire[]`.
- `ratio`: `Imeas_wire / Iexp` used for presence checks.

## Current source policy
Presence logic is current-based only (no voltage-drop presence checks).
It uses the selected current source:
- `CURRENT_SOURCE_KEY = CURRENT_SRC_ACS`: use ACS current.
- `CURRENT_SOURCE_KEY = CURRENT_SRC_ESTIMATE`: use `Vbus` + resistances.

When using estimate mode, include the charge/discharge resistor path in the total current estimate. Presence logic must subtract `Ileak` before comparing against expected wire current.

## Presence calibration guide

### Preconditions
1) Per-wire resistance is set: `R01OHM_KEY` .. `R10OHM_KEY`.
2) Charge resistor value is set: `CHARGE_RESISTOR_KEY`.
3) Current source is selected (`CURRENT_SOURCE_KEY`).
4) Device is in a safe idle state (relay on, outputs off).

### NVS keys for presence calibration
- `CALPRS` (bool): presence calibration done.
- `PMINR` (float): minimum valid ratio for probe and runtime checks (default 0.50).
- `PWIN`  (int): averaging window in ms for probe samples.
- `PFAIL` (int): consecutive failures before marking missing.

### Step-by-step calibration
1) **Single-wire probe (per enabled wire)**
   - Turn on one output at a time using a short, low-energy pulse.
   - Record `Vbus` and `Imeas` over `PWIN`.
   - Compute expected wire current: `Iexp_wire = Vbus / Rwire[i]`.
   - Subtract leak: `Imeas_wire = Imeas - Ileak`.
   - Compute `ratio = Imeas_wire / Iexp_wire`.
   - If `ratio >= PMINR`, mark wire present.
   - If below, mark wire missing.

2) **Store thresholds**
   - Default values:
     - `PMINR = 0.50`
     - `PWIN  = 200`
     - `PFAIL = 3`
   - `PMINR` may be tuned per hardware if needed.

### Notes
- Do not auto-enable missing wires during RUN; require explicit probe or admin action.
- When ACS is used, ensure zero-current calibration is already done by the CurrentSensor driver.

## Runtime presence checks (continuous)

Presence checks must execute on every running/heating tick when outputs are active (normal RUN, wire test, model calibration).

### Algorithm
1) Read `Vbus`, `Imeas`, and `mask`.
2) Compute `Ileak` using `Vbus / (Rtop + Rbot + Rcharge)`.
3) Compute expected conductance using only:
   - Wires in `mask`
   - Wires marked present
   - Wires below the thermal cutoff (hot wires are excluded from presence checks)
4) Compute `Iexp = Vbus * Gtotal`.
5) Compute `Imeas_wire = Imeas - Ileak`.
6) If `Iexp` is valid:
   - `ratio = Imeas_wire / Iexp`
   - If `ratio < PMINR`, increment a failure counter.
   - If failures reach `PFAIL`, mark affected wires as missing.
   - If `ratio >= PMINR`, clear the failure counter.

### Reaction policy
- If a wire is marked missing while active:
  - Disable that output immediately.
  - Recompute `allowedOutputs`.
  - If no outputs remain, stop the run and post a warning or error.
- Presence changes must be posted to UI telemetry immediately.

### Recovery policy
- Do not auto-restore missing wires during RUN.
- Restore only via:
  - Explicit presence probe, or
  - Admin override action.

## Thermal model integration (summary)
- Thermal integration runs continuously, independent of presence checks.
- Presence checks may ignore wires above the thermal cutoff to avoid false negatives.
- Virtual wire temperatures are display and safety only; control target is the floor NTC.

## Changes required (implementation checklist)
1) **NVS keys**
   - Add presence keys to `src/system/ConfigNVS.hpp`.
   - Initialize defaults in `src/services/NVSManager.cpp`.
2) **New module**
   - Add `WirePresenceManager` class in `src/wire/`.
   - Pure logic only, no GPIO, no NVS writes (except optional calibration helper).
3) **State model**
   - Ensure `WireRuntimeState.present` exists and is used as source of truth.
4) **Sampling**
   - Provide a single current source selection and share it with presence logic.
5) **Device integration**
   - Call `updatePresenceFromMask()` on every running/heating tick when outputs are active.
   - Gate `checkAllowedOutputs()` by presence state.
6) **Calibration flow**
   - Add a presence-probe command (admin-only) to run the calibration steps.
   - Persist thresholds and `CALPRS`.
7) **Telemetry**
   - Expose `present` flags in `StatusSnapshot` and `/monitor`.
8) **UI**
   - Add a presence status panel and a "Run probe" action in admin mode.

## Step-by-step implementation plan
1) Add presence NVS keys + defaults.
2) Create `WirePresenceManager` with:
   - `probeAll(...)`
   - `updatePresenceFromMask(...)`
   - `hasAnyConnected(...)`
3) Wire it into `Device`:
   - During RUN/test/cal, call `updatePresenceFromMask()` every tick.
   - On probe, call `probeAll()` in a safe state.
4) Update `checkAllowedOutputs()` to respect `present`.
5) Add calibration endpoint and UI hook.
6) Add telemetry fields and UI display.
7) Validate with controlled disconnect tests (single wire, multi-wire).

## Acceptance criteria
- A wire disconnect during heating is detected within one control tick.
- The disconnected wire is disabled immediately.
- If no outputs remain, the run stops safely and a warning/error is posted.
- Presence status is visible in telemetry and updated in real time.
