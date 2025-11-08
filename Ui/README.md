Nice, this is getting sharp now 🔥
Let’s rewrite the model to match your **real constraints**:

- Capacitor is effectively “always being refilled” → **we cannot use ΔVcap / capacitor energy** reliably.
- We **can** trust:

  - ACS781 current
  - Which wires are ON/OFF and when
  - Each wire’s nominal resistance and geometry

- We have **two modes**:

  - Sequential (1 wire at a time)
  - Advanced (parallel groups tuned to target equivalent R)

We want a **purely current-driven, per-wire power model** with:

- R(T) (temperature-dependent resistance),
- Thermal inertia,
- Auto 150°C protection,
- Same logic working for both modes.

Here’s the updated spec, clean and exact, based only on what you truly have.

---

## 1. Inputs the model actually uses

### Measured / runtime

1. **Total current `I_meas(t)`** from ACS781

   - Reliable.
   - Logged with timestamps.

2. **Active wires mask `active_i(t)`**

   - From your control logic.
   - For each time slice you know exactly which wires are ON.

3. **Time**

   - `millis()` or equivalent → compute `dt` between samples.

4. **(Optional but recommended) Baseline current**

   - Measure `I_idle` when no heater wires are ON.
   - Use `I_net = max(I_meas - I_idle, 0)` for heating calculations.

### Config / calibration (per wire)

From your design + Nichrome data:

1. `R0_i` — cold resistance at reference `T0` (e.g. 20°C).
2. Wire geometry:

   - Length `L_i`
   - Diameter / gauge → cross-section, surface area.

3. Material:

   - Nichrome density ρ
   - Specific heat `c_p`

4. Derived:

   - Mass `m_i`
   - Thermal capacity `C_th_i = m_i · c_p`

5. Thermal behavior:

   - Effective thermal resistance `R_th_i` to ambient
     → time constant `τ_i = R_th_i · C_th_i`.

6. R(T) behavior:

   - Use Nichrome’s temp coefficient or better: a linear / piecewise fit:

     - `R_i(T) = R0_i · [1 + α · (T − T0)]`

   - (α tuned using tech sheet / your measurements.)

### Global

1. Initial ambient temperature `T_amb`

   - Read once at startup from DS18B20s.
   - All wires start with `T_i = T_amb`.

2. Temperature constraints:

   - `T_max = 150°C` (hard ceiling)
   - `T_reenable ≈ 140°C` (hysteresis)
   - Optional fixed cooldown time per wire after overheat.

---

## 2. Per-wire state (kept by the estimator)

For each wire `i`:

- `T_i`
  Current estimated temperature (this **is** `WireInfo.temperatureC`).

- `lastUpdateMs_i`
  For integrating over time.

- `isLocked_i`
  True when this wire hit 150°C and is temporarily disabled.

- `cooldownReleaseMs_i` (optional)
  Earliest time this wire can be re-enabled.

You can also keep a tiny history if you want smoothing, but not required.

---

## 3. Core idea: use only current + R(T) to get power per wire

Since Vcap is not trustworthy, we treat supply voltage as **implicit**, solved from:

- All active heater wires are in **parallel**.
- At a given instant:

  - Each active wire `i` has resistance `R_i(T_i)`.
  - Total conductance:

    - `G_tot = Σ (1 / R_i(T_i))` over active wires.

  - We know **total heater current** `I_net` from ACS (after baseline subtraction).

So:

1. **Estimate supply voltage across the wires**:

   - `V_est = I_net / G_tot` (if `G_tot > 0`)

2. **Per-wire current**:

   - For each active wire:

     - `I_i = V_est / R_i(T_i)`
       (equivalently `I_i = I_net · ( (1/R_i) / G_tot )` )

3. **Per-wire power**:

   - `P_i = I_i² · R_i(T_i)`
     or
   - `P_i = V_est² / R_i(T_i)`
     (both are equivalent)

4. **Check conservation**:

   - Σ P_i = `V_est · I_net` = total power into wires (minus whatever you filtered).

This uses only:

- Measured current,
- Known R(T),
- Active set.

Exactly what you have.

---

## 4. Update rule per time step (this is the model)

At each estimator tick (or for each logged current sample):

1. Compute `dt` from `lastUpdateMs_i` for each wire.
2. Determine `active` set from output states.

### For each wire `i`:

#### 4.1 Heating term (only if active and not locked)

If wire `i` is ON and `!isLocked_i`:

- Use `P_i` computed above.
- Convert power → energy over `dt`:

  - `E_i = P_i · dt`

- Temperature rise from that energy:

  - `ΔT_heat_i = E_i / C_th_i`

- (This inherently uses its mass and Nichrome properties → thermal inertia.)

#### 4.2 Cooling term (always)

Regardless of ON/OFF state:

- Cool toward `T_amb` using thermal RC:

  - `ΔT_cool_i = − (T_i − T_amb) · (dt / τ_i)`

This gives:

> `T_i_new = T_i_old + ΔT_heat_i + ΔT_cool_i`

with **inertia**:

- Big τ → slow changes,
- Small τ → fast response,
- Hotter above ambient → faster cooling.

#### 4.3 Safety & lockout

After updating:

1. If `T_i_new >= T_max (150°C)`:

   - Set `T_i_new = 150°C`
   - `isLocked_i = true`
   - Set `cooldownReleaseMs_i = now + cooldown_ms` (if you use one)

2. While `isLocked_i`:

   - Treat wire as **not eligible** for activation (both modes must respect this).
   - Only apply the **cooling term** each step.
   - When:

     - `T_i < T_reenable` (e.g. 140°C)
     - AND `now >= cooldownReleaseMs_i`

   - Then:

     - `isLocked_i = false` → wire can be used again.

3. Write `T_i_new` into `WireInfo.temperatureC`.

---

## 5. How this works in each mode

### 5.1 Sequential mode (1→10)

- At any time, only one wire is ON:

  - Active set S = {k}
  - `G_tot = 1 / R_k(T_k)`
  - `V_est = I_net · R_k(T_k)`
  - `P_k = I_net² · R_k(T_k)`

- Only that wire gets heating. Others cool.
- Temperature estimate is very accurate because there’s no ambiguity:

  - All current is that wire (minus baseline).

- Control logic:

  - Always pick the **coolest non-locked** wire next.
  - That naturally equalizes them near the 150°C band.

### 5.2 Advanced mode (parallel groups)

- Multiple wires ON in parallel; your planner chooses them to hit target R_eq.

- Estimator:

  1. Active set S = all `i` with ON and not locked.
  2. Compute `G_tot = Σ 1/R_i(T_i)` using current temps.
  3. Use `I_net` to derive `V_est`.
  4. For each active wire:

     - `I_i` and `P_i` computed as above.
     - `ΔT_heat_i` based on its own `C_th_i`.

- Effects:

  - Colder wires (lower R(T)) naturally draw more current → heat faster.
  - Hotter wires (higher R(T)) draw less → self-balancing.
  - Scheduler can:

    - Prefer cooler wires in the next group.
    - Skip any locked ones.

  - Result: group driving plus the physics model pushes all wires toward the same temperature band without exceeding 150°C.

---

## 6. How to use the current log / CurrentManager

To make it robust:

- Have `CurrentManager` maintain a small ring buffer of:

  - `timestamp`
  - `I_meas`
  - `activeMask` (which wires were ON)

- The estimator task:

  - Consumes these entries in order,
  - For each step:

    - Computes `dt` between entries,
    - Runs the per-wire update as described.

This decouples:

- Fast switching / PWM / mode logic
  from
- Smooth thermal estimation.

And it works the same in both modes.

---

## 7. End result

With this updated model:

- You **do not rely** on capacitor discharge shape.
- You **only rely** on:

  - Measured current (trusted),
  - Which wires are ON,
  - Nichrome physics (R(T), mass, surface),
  - Thermal RC inertia,
  - 150°C clamp + lockout.

You get:

- A physically grounded **virtual temperature** for each of the 10 wires,
- Valid in sequential and advanced group mode,
- Good enough to:

  - Protect hardware,
  - Steer scheduling to keep every wire hovering _just under_ 150°C.

If you’d like, I can now turn _this_ exact spec into a clear set of functions / RTOS task flow that slots right into `HeaterManager` + `Device` without hand-waving.
help me imp^lement the fix here, the idle current can you recalculate it before starting the loop instead knowing that all the output are off and input relay activated ac topping off the capacitors ? for the sequential make sure to top of the coolest from all?
