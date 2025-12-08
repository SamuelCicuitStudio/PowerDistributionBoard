# Wire Thermal & Presence Model (Design)

This document describes a modular design for:
- Detecting whether each nichrome wire is physically present when its output is ON.
- Simulating per‑wire temperature from electrical power.
- Exposing that information to the main `Device` state machine and to the web UI.

The goal is to keep the heavy math and detection logic in **focused helper classes**, and let `Device` orchestrate them.

> NOTE: This is a design/architecture document. It describes how to structure the logic; it does **not** imply that all pieces are already implemented exactly this way in code.

---

## High‑Level Goals

- **Wire presence detection**
  - Decide for each output (1..10) whether a real wire/load is connected.
  - Use measured current vs expected current (from bus voltage and wire resistance).
  - Re‑evaluate presence both at startup (probe) and continuously while running.

- **Virtual temperature model**
  - Maintain a virtual temperature `T[i]` for each wire, based on:
    - Electrical power dissipated in the wire.
    - Its thermal capacity (mass, specific heat).
    - Cooling towards ambient.
  - Use virtual temperature to:
    - Enforce over‑temperature limits.
    - Inform presence logic (avoid marking “missing” when a wire is hot and current naturally drops).
    - Provide rich UI telemetry (per‑wire temps).

- **Modular responsibilities**
  - One class owns the **thermal integration** logic (no GPIO, no NVS).
  - One class owns the **presence detection** logic.
  - `HeaterManager` stays focused on GPIO control + static wire data.
  - `Device` wires everything together and applies safety/behavior.

---

## Key Components & Roles

### 1. HeaterManager (existing)

**Role**: Hardware driver and static wire metadata.

- Owns per‑wire configuration:
  - `WireInfo`: index, resistance (R), derived geometry (length, cross‑section, volume, mass).
  - `connected` flag (presence), `presenceCurrentA`.
  - `temperatureC` (virtual temperature slot).
- Drives GPIO pins for outputs:
  - `setOutput(index, bool)` – single‑channel on/off.
  - `setOutputMask(mask)` – bitmask on/off for all channels.
  - Keeps a rolling **output history** (mask + timestamp) for observers.
- Exposes read‑only helpers:
  - `getOutputState(i)` / `getOutputMask()`.
  - `getWireInfo(i)` – static physical properties.
  - `getWireEstimatedTemp(i)` / `setWireEstimatedTemp(i, T)` – virtual temp storage.
  - `getOutputHistorySince(lastSeq, ...)` – history for thermal/presence observers.

HeaterManager knows **how** to turn outputs on/off and **what** each wire looks like physically, but does **not** do thermal math or decide if a wire is “missing” by itself.

---

### 2. WireThermalModel (new, pure logic)

**Role**: Compute virtual temperatures for each wire from power dissipation.

This can be implemented as a new helper class (or logically separated module) owned by `Device`:

```cpp
class WireThermalModel {
public:
    void init(const HeaterManager& heater, float ambientC);

    // Main integration entry: called from a RTOS task.
    void integrate(const CurrentSensor::Sample* curBuf, size_t nCur,
                   const HeaterManager::OutputEvent* outBuf, size_t nOut,
                   float idleCurrentA, float ambientC, uint32_t nowMs);

    float getWireTemp(uint8_t index) const;
};
```

**Inputs**
- Per‑wire cold resistance `R0`, mass, and derived thermal capacity from `HeaterManager`.
- Continuous current samples from `CurrentSensor` (timestamp + currentA).
- Output mask history from `HeaterManager` (which wires were ON at what time).
- Ambient temperature (from DS18B20 sensors).

**Core logic**
- For each wire:
  - Maintain a state `{ R0, C_th, tau, T, lastUpdateMs, locked, cooldownReleaseMs }`.
  - Between timestamps, apply cooling towards ambient:
    - `T(t+dt) = T_inf + (T(t) − T_inf) * exp(−dt / tau)`
  - When there is net heater current and a wire is active:
    - Compute its instantaneous resistance `R(T)` using a linear Nichrome tempco.
    - Derive bus voltage from net current and total conductance.
    - Compute Joule power `P = V² / R(T)` per wire.
    - Integrate temperature increase:
      - `ΔT = (P * dt) / C_th`
  - Clamp and lock:
    - If `T >= WIRE_T_MAX_C` → lock wire, disable heating until cool.
    - If `T <= WIRE_T_REENABLE_C` and cooldown time passed → unlock.
- After each integration step:
  - Publish `T` back to `HeaterManager::setWireEstimatedTemp(i, T)` for telemetry.

**Outputs**
- Per‑wire virtual temperatures available via:
  - `WIRE->getWireEstimatedTemp(i)` for telemetry and safety decisions.
  - Optional: per‑wire instantaneous power if you want explicit power telemetry.

---

### 3. WirePresenceManager (new, pure logic)

**Role**: Decide if each wire is physically present / healthy, based on current vs expected behavior.

This can be a second helper class, also owned by `Device`:

```cpp
class WirePresenceManager {
public:
    void probeAll(HeaterManager& heater,
                  CurrentSensor& cs,
                  float busVoltage);

    void updatePresenceFromMask(HeaterManager& heater,
                                uint16_t mask,
                                float totalCurrentA,
                                float busVoltage);

    bool hasAnyConnected(const HeaterManager& heater) const;
};
```

**Initial probe (at startup / safe moment)**  
Similar to the existing `probeWirePresence()` in `HeaterManager`, but logically separated:

1. Ensure all outputs are OFF.
2. For each wire `i`:
   - Turn only that wire ON.
   - Measure average current `Imeas`.
   - Compute expected current `Iexp = Vbus / R_i`.
   - Compare ratio `Imeas / Iexp` against min/max thresholds.
   - Mark `connected = true/false` and store `presenceCurrentA`.
3. Restore previous output states.

**Continuous presence refinement (while running)**

Whenever a **mask** of outputs is active and there is measured total current `I_total`:

1. Compute expected total conductance:
   - Use only wires:
     - That are currently in the mask,
     - Marked `connected` already,
     - Whose virtual temperature is below a presence cutoff (e.g., `< 150°C`) so hot wires are not treated as missing.
2. Compute expected total current `Iexp = Vbus * G`.
3. Compare `ratio = I_total / Iexp` to a minimum valid ratio:
   - If ratio is too low, mark *cool* wires in the mask as `connected = false`.
   - Do not touch wires already considered missing or very hot.

This gives a **smart, temperature‑aware presence detection** that:
- Uses the thermal model to avoid false “missing” flags when wires are hot.
- Updates presence whenever the load combination and measured current disagree significantly with expectations.

---

## Extended Wire‑Control Classes (for Maximum Modularity)

The following helper classes further sharpen responsibilities around the wires. They are **conceptual building blocks**; in code they can live as separate classes, or be embedded inside existing modules (e.g. `HeaterManager`, `Device`), as long as their roles stay clearly separated.

### 4. WireGpioDriver (hardware‑only)

**Role**: The only class that knows GPIO pin numbers and actually touches `pinMode()` / `digitalWrite()`. It does **no** safety or planning.

```cpp
class WireGpioDriver {
public:
    void begin();                     // Configure all pins as outputs, all OFF.

    void applyMask(uint16_t mask);    // Write GPIOs to match mask (bit i -> wire i+1).

    uint16_t readMask() const;        // Return last applied mask (from cached state).
};
```

Responsibilities:
- Map logical wire indices 1..10 to physical pins.
- Ensure all outputs start in a safe OFF state.
- Provide a simple bit‑mask interface to higher‑level logic (`HeaterManager` / `WireActuator`).

It does **not**:
- Know about resistance, temperature, presence, or access flags.
- Decide which wires *should* be on; it only executes the mask it is given.

---

### 5. WireConfigStore (NVS‑backed configuration)

**Role**: Owns all *persistent* configuration for wires and related parameters. No GPIO, no runtime state.

Typical contents:
- Per‑wire resistance `R[i]` (R01..R10 NVS keys).
- Global target resistance `targetRes`.
- Global `wireOhmPerM` (geometry parameter).
- Per‑wire access flags (OUT01_ACCESS_KEY..OUT10_ACCESS_KEY).

Sketch:

```cpp
class WireConfigStore {
public:
    void loadFromNvs();   // Read all wire‑related keys from Preferences.
    void saveToNvs() const;

    float  getWireResistance(uint8_t index) const;
    void   setWireResistance(uint8_t index, float ohms);

    bool   getAccessFlag(uint8_t index) const;
    void   setAccessFlag(uint8_t index, bool allowed);

    float  getTargetResOhm() const;
    void   setTargetResOhm(float ohms);

    float  getWireOhmPerM() const;
    void   setWireOhmPerM(float ohmPerM);
};
```

Responsibilities:
- Abstract all NVS access for wire parameters.
- Validate and clamp configuration values (e.g. minimum R, positive ohms).
- Provide a clean API for higher layers (planners, safety) to query config.

It does **not**:
- Track temperatures, presence, or masks (that is runtime state).
- Drive GPIOs or make enable/disable decisions.

---

### 6. WireStateModel (runtime wire state only)

**Role**: In‑RAM model of the **current** state of each wire, independent of hardware pins or NVS.

Example per‑wire runtime structure:

```cpp
struct WireRuntimeState {
    bool  present;          // result from WirePresenceManager.
    bool  overTemp;         // latched over‑temperature condition.
    bool  locked;           // gated by thermal model / safety.
    bool  allowedByAccess;  // from config policy.

    float tempC;            // latest virtual temperature.
    float lastPowerW;       // last computed power (optional).
    uint32_t lastUpdateMs;  // last time temp/power were updated.
};

class WireStateModel {
public:
    WireRuntimeState&       wire(uint8_t index);
    const WireRuntimeState& wire(uint8_t index) const;

    uint16_t getLastMask() const;
    void     setLastMask(uint16_t m);
};
```

Responsibilities:
- Hold everything that can change at runtime for wires:
  - Presence flags (`present`).
  - Thermal state (`tempC`, `overTemp`, `locked`).
  - Policy‑related flags (`allowedByAccess`).
  - Last applied mask, timestamps, last power, etc.
- Be the **single source of truth** for wire status used by planners, safety, and telemetry.

It does **not**:
- Touch hardware or NVS.
- Implement any algorithms; it just stores and exposes data.

---

### 7. WirePlanner (resistance / strategy planner)

**Role**: Decide *which* wires should be active to meet a target resistance or strategy, given config + current wire state.

The planner is pure logic: no GPIO, no NVS writes, no temperature updates.

```cpp
class WirePlanner {
public:
    // Return a candidate mask that best meets targetRes using wires
    // that are currently available in WireStateModel (present + not locked).
    uint16_t chooseMask(const WireConfigStore& cfg,
                        const WireStateModel&  state,
                        float targetResOhm) const;
};
```

Responsibilities:
- Explore combinations of available wires (1..10) to approximate `targetRes`.
- Respect availability from `WireStateModel` (present, not locked, etc.).
- Prefer solutions with fewer active wires when possible (less heat / loss).

It does **not**:
- Apply the mask to hardware (that is the actuator’s job).
- Decide whether the mask is safe in the current device state; it assumes it is choosing from *potentially* safe wires and defers final gating to the safety layer.

---

### 8. WireSafetyPolicy (gating / safety rules)

**Role**: Take a requested mask (from planner or manual control) and **filter** it according to all safety and policy rules.

```cpp
class WireSafetyPolicy {
public:
    uint16_t filterMask(uint16_t requestedMask,
                        const WireConfigStore& cfg,
                        const WireStateModel&  state,
                        DeviceState            devState) const;
};
```

Responsibilities:
- Enforce:
  - Device‑level state (e.g. only allow heating in `Running`, maybe in Manual mode).
  - Access flags (user‑disabled outputs).
  - Presence flags (only `present == true` wires).
  - Thermal limits (`overTemp`, `locked` wires must be excluded).
  - Any per‑wire or global power limits.
- Return the **effective** mask actually allowed to reach the hardware.

It does **not**:
- Talk to GPIOs or NVS.
- Choose an “optimal” combination; it only restricts what was requested.

---

### 9. WireActuator (requested → filtered → hardware)

**Role**: Glue the **requested** mask, safety policy, and GPIO driver together. It is the only class that translates planner/manual intent into calls to `WireGpioDriver`.

```cpp
class WireActuator {
public:
    WireActuator(WireGpioDriver& driver,
                 WireSafetyPolicy& policy,
                 WireStateModel& state);

    // Apply a new requested mask; returns the mask actually applied.
    uint16_t applyRequestedMask(uint16_t requestedMask,
                                const WireConfigStore& cfg,
                                DeviceState devState);
};
```

Responsibilities:
- Receive a requested mask (from `WirePlanner`, manual control, or Device logic).
- Ask `WireSafetyPolicy` to filter it down to a safe effective mask.
- Call `WireGpioDriver::applyMask(effectiveMask)`.
- Update `WireStateModel`’s `lastMask` and output history (if not delegated to `HeaterManager`).

It does **not**:
- Calculate target resistance or combinations (planner’s job).
- Maintain temperatures or presence; it just applies the decision safely.

In the current codebase, much of this behavior lives inside `HeaterManager::setOutput()` / `setOutputMask()` and `Device`’s state machine; the modular design simply makes those responsibilities explicit and easier to evolve.

---

### 10. WireTelemetryAdapter (wire → StatusSnapshot / JSON)

**Role**: Collect all wire‑level information from the various models and pack it into the telemetry structures used by `DeviceTransport` and `WiFiManager`.

```cpp
class WireTelemetryAdapter {
public:
    void fillSnapshot(StatusSnapshot& out,
                      const WireConfigStore& cfg,
                      const WireStateModel&  state) const;

    // Optional convenience: write detailed wire data to a JSON object for /monitor.
    void writeMonitorJson(JsonObject& root,
                          const StatusSnapshot& snap) const;
};
```

Responsibilities:
- Map internal wire state onto external telemetry:
  - `wireTemps[i]` from `WireStateModel` / `WireThermalModel`.
  - `outputs[i]` from `WireStateModel` / actuator.
  - Optional `wirePower[i]`, `totalPower_W`, presence flags, fault flags.
- Keep `StatusSnapshot` small but expressive for the UI.

It does **not**:
- Perform any control decisions or hardware access.
- Own the source of truth; it only reads from other models and formats data.

---

## Device Integration

`Device` remains the orchestrator and real safety owner. It wires the helpers together like this:

- At initialization / RUN entry:
  - Ask `HeaterManager` for wire geometry and resistances.
  - Initialize `WireThermalModel` with those parameters and ambient.
  - Start continuous current sampling (`CurrentSensor::startContinuous(...)`).
  - Start a FreeRTOS task that periodically calls:
    - `WireThermalModel::integrate(...)`
    - `WirePresenceManager::updatePresenceFromMask(...)` when appropriate.
  - Run an initial presence probe at a safe time (relay on, known bus voltage).

- In the state machine:
  - `checkAllowedOutputs()` considers:
    - Config access flags (user/admin permissions).
    - Thermal lockout from the thermal model (hot wires).
    - Presence flags from the presence manager (`connected`).
  - Commands like `SET_OUTPUT` and auto target‑resistance planners:
    - Ignore wires that are either over‑temp or marked not connected.
    - Re‑plan if presence changes.

- On faults:
  - Over‑temp: use virtual temps and physical temps to force Error → relay OFF, outputs OFF.
  - Presence anomalies: optionally transition to Error or re‑plan outputs.

---

## Telemetry & Web UI

Once thermal and presence logic are modularized and wired into `Device`, the telemetry path becomes:

1. `DeviceTransport::getTelemetry(StatusSnapshot& out)`:
   - Reads:
     - `out.wireTemps[i] = WIRE->getWireEstimatedTemp(i+1);`
     - `out.outputs[i]   = WIRE->getOutputState(i+1);`
     - (optionally) `out.wirePower[i]`, `out.totalPower_W`.
2. `WiFiManager` snapshot task:
   - Caches `StatusSnapshot` and exposes it via `/monitor`.
3. Frontend (`admin.js`):
   - Polls `/monitor` and:
     - Paints per‑wire temps and presence on the live SVG overlay.
     - Shows session totals and live power to the user.

This means when you manually switch outputs ON/OFF from the admin page, the thermal model and presence manager keep running in the background and the UI always sees up‑to‑date temperatures and presence for each wire.

---

## Benefits of This Modular Design

- **Clear separation of concerns**
  - `HeaterManager`: GPIO + static wire config.
  - `WireThermalModel`: math and thermal integration, no hardware.
  - `WirePresenceManager`: presence decisions, no GPIO.
  - `Device`: safety state machine and high‑level behavior.

- **Easier testing**
  - Thermal and presence classes can be unit‑tested with synthetic histories (no real hardware).
  - You can replay captured current + mask logs to validate behavior.

- **Smarter, safer behavior**
  - Presence detection that adapts to temperature and configuration, not just a fixed threshold.
  - Per‑wire virtual temperatures for both safety and UI insight.

- **Extensibility**
  - Adding new thermal rules or presence heuristics stays local to the helper classes.
  - Telemetry extensions (e.g., per‑wire power, cumulative Wh) can be added to `StatusSnapshot` without touching hardware drivers.
