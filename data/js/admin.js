(function () {
  if (window.__ADMIN_JS_LOADED__) {
    console.warn("admin.js already loaded; skipping duplicate execution.");
    return;
  }
  window.__ADMIN_JS_LOADED__ = true;

  ("use strict");

  // ========================================================
  // ===============        TAB SWITCHING        ============
  // ========================================================
  const tabs = document.querySelectorAll(".tab");
  const contents = document.querySelectorAll(".content");

  function switchTab(index) {
    tabs.forEach((tab, i) => {
      const isActive = i === index;
      tab.classList.toggle("active", isActive);
      if (contents[i]) {
        contents[i].classList.toggle("active", isActive);
      }
    });
  }

  // ========================================================
  // ===============   GLOBAL HELPERS / STATE   =============
  // ========================================================

  const powerEl = () => document.getElementById("powerButton");
  const powerText = () => document.getElementById("powerLabel");

  let lastState = "Shutdown"; // Device state from backend
  let pendingConfirm = null; // For confirm modal
  let lastLoadedControls = null; // Snapshot from /load_controls
  let isMuted = false; // Buzzer mute cached state
  let stateStream = null; // EventSource for zero-lag state
  let statePollTimer = null; // Fallback polling timer
  let monitorPollTimer = null; // /monitor polling timer (UI snapshot)
  let calibrationBusy = false; // prevent overlapping manual calibrations

  // ========================================================
  // ===============          TOOLTIPS          =============
  // ========================================================

  const TOOLTIP_BY_ID = {
    // Dashboard / global controls
    modeToggle:
      "Auto vs Manual. Manual gives direct relay/output control; Auto runs the heating loop.",
    ltToggle:
      "LT (LED feedback). When enabled, output LEDs mirror output states during the loop.",
    muteBtn: "Mute/unmute buzzer sounds.",
    powerButton: "Start/stop the device loop (RUN / OFF).",
    forceCalibrationBtn:
      "Force a full calibration sequence (runs before starting the loop).",
    disconnectBtn:
      "Disconnect from the device UI (logs out and redirects to login).",
    sessionHistoryBtn: "Open session history (energy/duration/peaks).",
    readyLed: "Ready status indicator LED.",
    offLed: "OFF status indicator LED.",

    // Dashboard gauges
    voltageValue: "Capacitor bank voltage estimate (V) from the ADC input.",
    adcRawValue: "Raw ADC reading used for the voltage estimate.",
    currentValue: "Measured load current (A).",
    temp1Value: "Board temperature sensor 01 (degC).",
    temp2Value: "Board temperature sensor 02 (degC).",
    temp3Value: "Heatsink temperature (degC).",
    capacitanceValue: "Detected/assumed capacitor bank capacitance (mF).",

    // Manual control
    fanSlider: "Manual fan speed (%).",
    relayToggle: "Manual input relay control (manual mode).",

    // Device settings (Sampling & Power)
    acFrequency:
      "Sampling rate for current/telemetry (Hz). Higher = faster updates (50..500).",
    chargeResistor:
      "Bleed/charge resistor value between capacitor bank negative and system GND (Ohms).",
    currLimit:
      "Over-current trip threshold (A). Trips if current stays above this for a short time window.",

    // Thermal safety
    tempWarnC:
      "Temperature warning threshold (degC). Shows a warning overlay before shutdown.",
    tempTripC:
      "Temperature trip threshold (degC). Triggers shutdown and Error when exceeded.",

    // Thermal model
    idleCurrentA:
      "Baseline current when heaters are OFF (A). Used by virtual temperature estimation.",
    wireTauSec:
      "Virtual wire thermal inertia tau (seconds). Higher = slower virtual temperature response.",

    // Loop timing
    onTime: "Pulse ON duration (ms). Used in manual timing mode.",
    offTime: "Pulse OFF duration (ms). Used in manual timing mode.",
    loopModeSelect:
      "Loop mode. Advanced = grouped outputs; Sequential = one output at a time.",
    timingModeSelect:
      "Timing UI mode. Normal uses Hot/Medium/Gentle presets; Advanced exposes manual Ton/Toff.",
    timingProfileSelect:
      "Preset heat profile (Hot/Medium/Gentle). Updates Ton/Toff automatically.",
    timingResolved: "Read-only preview of the currently resolved Ton/Toff.",

    // Cooling & nichrome
    coolingAirToggle: "Cooling profile. ON = air/fast, OFF = buried/slow.",
    coolBuriedScale:
      "Cooling scale for buried profile. Higher = stronger cooling in the model.",
    coolKCold: "Base cooling gain (kCold) for the thermal model.",
    coolDropMaxC:
      "Maximum cooling drop per integration step (degC). Limits sudden cooling.",
    wireOhmPerM: "Nichrome resistivity (Ohms per meter).",
    rTarget: "Target resistance used by the selector/planner (Ohms).",

    // Admin settings
    adminCurrentPassword: "Current admin password (required to change settings).",
    adminUsername: "New admin username.",
    adminPassword: "New admin password.",
    wifiSSID: "WiFi station SSID to connect to.",
    wifiPassword: "WiFi station password.",

    // User settings
    userCurrentPassword: "Current user password (required to change user settings).",
    userNewPassword: "New user password to set.",
    userDeviceId: "Change the displayed device ID/label.",
  };

  function applyTooltips(root = document) {
    // 1) Explicit per-id tooltips (overwrite if present)
    for (const [id, tip] of Object.entries(TOOLTIP_BY_ID)) {
      const el = root.getElementById ? root.getElementById(id) : null;
      if (el) el.title = tip;
      const lbl = root.querySelector
        ? root.querySelector('label[for="' + id + '"]')
        : null;
      if (lbl) lbl.title = tip;
    }

    // 2) Common icon / container helpers (non-id)
    const userIcon = root.querySelector ? root.querySelector(".user-icon") : null;
    if (
      userIcon &&
      (!userIcon.title || String(userIcon.title).trim().length === 0)
    ) {
      userIcon.title = "User menu";
    }

    // 3) Gauges: provide hover help on the card/value/label.
    const gaugeTipByName = {
      Voltage: "Capacitor bank voltage estimate (V) from the ADC input.",
      Current: "Measured load current (A).",
      "Board 01": "Board temperature sensor 01 (degC).",
      "Board 02": "Board temperature sensor 02 (degC).",
      Heatsink: "Heatsink temperature (degC).",
      Capacitance: "Detected/assumed capacitor bank capacitance (mF).",
    };

    const gaugeCards = root.querySelectorAll
      ? root.querySelectorAll(".gauge-card")
      : [];

    for (const card of gaugeCards) {
      const labels = card.querySelectorAll
        ? card.querySelectorAll(".gauge-label")
        : [];
      const mainLabel = labels && labels.length ? labels[0] : null;
      const name =
        mainLabel && mainLabel.textContent ? mainLabel.textContent.trim() : "";
      if (!name) continue;

      let tip = gaugeTipByName[name] || "";
      if (!tip) {
        const m = /^Temp\s*(\d+)$/i.exec(name);
        if (m) tip = "Temperature channel " + m[1] + " (degC).";
        else tip = name + " (live reading).";
      }

      if (!card.title || String(card.title).trim().length === 0) {
        card.title = tip;
      }
      if (
        mainLabel &&
        (!mainLabel.title || String(mainLabel.title).trim().length === 0)
      ) {
        mainLabel.title = tip;
      }
      const value = card.querySelector ? card.querySelector(".gauge-value") : null;
      if (value && (!value.title || String(value.title).trim().length === 0)) {
        value.title = tip;
      }
      for (const lbl of labels) {
        if (lbl && (!lbl.title || String(lbl.title).trim().length === 0)) {
          lbl.title = tip;
        }
      }
    }

    // 4) Fallback: any interactive element without a tooltip gets one from its label/placeholder/text.
    const candidates = root.querySelectorAll
      ? root.querySelectorAll("input, select, textarea, button, [onclick]")
      : [];

    for (const el of candidates) {
      if (!el || typeof el !== "object") continue;
      if (el.title && String(el.title).trim().length) continue;

      const id = el.id || "";
      let tip = "";

      if (id) {
        const lbl = root.querySelector('label[for="' + id + '"]');
        if (lbl && lbl.textContent) tip = lbl.textContent.trim();
      }

      if (!tip && el.getAttribute) {
        const aria = el.getAttribute("aria-label");
        if (aria) tip = String(aria).trim();
      }

      if (!tip && el.getAttribute) {
        const ph = el.getAttribute("placeholder");
        if (ph) tip = String(ph).trim();
      }

      if (!tip && el.closest) {
        const item = el.closest(".manual-item");
        if (item) {
          const span = item.querySelector ? item.querySelector("span") : null;
          if (span && span.textContent) tip = span.textContent.trim();
        }
      }

      if (!tip && el.querySelector) {
        const img = el.querySelector("img[alt]");
        if (img) {
          const alt = img.getAttribute ? img.getAttribute("alt") : "";
          if (alt) tip = String(alt).trim();
        }
      }

      if (!tip && el.textContent) {
        const txt = String(el.textContent).trim();
        if (txt) tip = txt;
      }

      if (tip) el.title = tip;
    }
  }

  function isManualMode() {
    const t = document.getElementById("modeToggle");
    return !!(t && t.checked);
  }

  function sleep(ms) {
    return new Promise((resolve) => setTimeout(resolve, ms));
  }

  function approxEqual(a, b, eps = 0.05) {
    const na = Number(a);
    const nb = Number(b);
    if (!Number.isFinite(na) || !Number.isFinite(nb)) return false;
    return Math.abs(na - nb) <= eps;
  }

  function setModeDot(isManual) {
    const dot = document.querySelector(".status-dot");
    if (!dot) return;
    const color = isManual ? "#ffa500" : "#00ff80";
    dot.title = isManual ? "Manual Mode" : "Auto Mode";
    dot.style.backgroundColor = color;
    dot.style.boxShadow = "0 0 6px " + color;
  }

  function setField(id, val) {
    const el = document.getElementById(id);
    if (!el) return;
    if (val === undefined || val === null || Number.isNaN(val)) {
      el.value = "";
    } else {
      el.value = val;
    }
  }

  function getFloat(id) {
    const el = document.getElementById(id);
    if (!el) return undefined;
    const v = parseFloat(el.value);
    return Number.isFinite(v) ? v : undefined;
  }

  function getInt(id) {
    const el = document.getElementById(id);
    if (!el) return undefined;
    const v = parseInt(el.value, 10);
    return Number.isFinite(v) ? v : undefined;
  }

  // ========================================================
  // ===============        CONFIRM MODAL        ============
  // ========================================================

  function openConfirm(kind) {
    pendingConfirm = kind || null;

    const modal = document.getElementById("confirmModal");
    const title = document.getElementById("confirmTitle");
    const message = document.getElementById("confirmMessage");
    const okBtn = document.getElementById("confirmOkBtn");
    const cancelBtn = document.getElementById("confirmCancelBtn");

    if (!modal || !title || !message || !okBtn) return;

    // Always show cancel for confirmations
    if (cancelBtn) cancelBtn.style.display = "";

    okBtn.classList.remove("danger", "warning", "success");

    if (kind === "reset") {
      title.textContent = "Confirm Reset";
      message.textContent = "This will reset the device (soft reset). Proceed?";
      okBtn.textContent = "Yes, Reset";
      okBtn.classList.add("danger");
    } else if (kind === "reboot") {
      title.textContent = "Confirm Reboot";
      message.textContent = "The device will reboot. Continue?";
      okBtn.textContent = "Yes, Reboot";
      okBtn.classList.add("danger");
    } else {
      title.textContent = "Confirm Action";
      message.textContent = "Are you sure?";
      okBtn.textContent = "Confirm";
      okBtn.classList.add("warning");
    }

    modal.style.display = "flex";
  }

  function closeConfirm() {
    const modal = document.getElementById("confirmModal");
    if (modal) modal.style.display = "none";
    pendingConfirm = null;
  }

  function bindConfirmModal() {
    const modal = document.getElementById("confirmModal");
    const okBtn = document.getElementById("confirmOkBtn");
    const cancelBtn = document.getElementById("confirmCancelBtn");

    if (!modal || !okBtn) return;

    if (cancelBtn) {
      cancelBtn.addEventListener("click", () => {
        pendingConfirm = null;
        closeConfirm();
      });
    }

    okBtn.addEventListener("click", () => {
      if (!pendingConfirm) {
        closeConfirm();
        return;
      }

      if (pendingConfirm === "reset") {
        resetSystem();
      } else if (pendingConfirm === "reboot") {
        rebootSystem();
      }

      pendingConfirm = null;
      closeConfirm();
    });

    // Click outside to close
    modal.addEventListener("click", (e) => {
      if (e.target === modal) {
        pendingConfirm = null;
        closeConfirm();
      }
    });

    // ESC key closes
    document.addEventListener("keydown", (e) => {
      if (e.key === "Escape") {
        pendingConfirm = null;
        closeConfirm();
      }
    });
  }

  // Generic alert that reuses confirm modal with no pendingConfirm
  function openAlert(title, message, variant = "warning") {
    pendingConfirm = null;

    const modal = document.getElementById("confirmModal");
    const titleEl = document.getElementById("confirmTitle");
    const messageEl = document.getElementById("confirmMessage");
    const okBtn = document.getElementById("confirmOkBtn");
    const cancelBtn = document.getElementById("confirmCancelBtn");

    if (!modal || !titleEl || !messageEl || !okBtn) return;

    // Hide cancel for alerts
    if (cancelBtn) cancelBtn.style.display = "none";

    titleEl.textContent = title || "Notice";
    messageEl.textContent = message || "";
    okBtn.textContent = "OK";

    okBtn.classList.remove("danger", "warning", "success");
    if (variant === "danger") okBtn.classList.add("danger");
    else if (variant === "success") okBtn.classList.add("success");
    else okBtn.classList.add("warning");

    modal.style.display = "flex";
  }

  // ========================================================
  // ===============     MANUAL TAKEOVER LOGIC    ===========
  // ========================================================

  /**
   * Ensure manual mode is asserted and auto loop is not Running.
   * Call this before sending ANY manual-changing command.
   */
  async function ensureManualTakeover(source) {
    // 1) Flip UI + backend to manual if needed
    if (!isManualMode()) {
      const t = document.getElementById("modeToggle");
      if (t) t.checked = true;
      setModeDot(true);
      await sendControlCommand("set", "mode", true);
    }

    // 2) If auto loop was Running, try abort; fallback to shutdown
    if (lastState === "Running") {
      const resp = await sendControlCommand("set", "abortAuto", true);
      if (!resp || resp.error) {
        await shutdownSystem();
      }
      setPowerUI("Idle");
    }

    console.log("Manual takeover by:", source || "unknown");
  }

  // ========================================================
  // ===============        POWER BUTTON UI      ============
  // ========================================================

  function setPowerUI(state, extras = {}) {
    const btn = powerEl();
    const labelEl = powerText();
    if (!btn || !labelEl) {
      lastState = state || lastState;
      return;
    }

    btn.classList.remove(
      "state-off",
      "state-idle",
      "state-ready",
      "state-error"
    );

    let label = (state || "Shutdown").toUpperCase();
    let cls = "state-off";

    if (state === "Shutdown") {
      label = "OFF";
      cls = "state-off";
    } else if (state === "Idle") {
      label = "IDLE";
      cls = "state-idle";
    } else if (state === "Running") {
      label = extras.ready === true ? "READY" : "RUN";
      cls = "state-ready";
    } else if (state === "Error") {
      label = "ERROR";
      cls = "state-error";
    }

    btn.classList.add(cls);
    labelEl.textContent = label;
    lastState = state;
  }

  function onPowerClick() {
    if (lastState === "Shutdown") {
      startSystem();
    } else {
      shutdownSystem();
    }
  }

  async function pollDeviceState() {
    try {
      const res = await fetch("/control", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ action: "get", target: "status" }),
      });
      if (!res.ok) return;
      const data = await res.json();
      if (data && data.state) {
        setPowerUI(data.state);
      }
    } catch (err) {
      console.warn("Status poll failed:", err);
    }
  }

  function startStatePolling() {
    if (statePollTimer) return;
    pollDeviceState();
    statePollTimer = setInterval(pollDeviceState, 2000);
  }

  function stopStatePolling() {
    if (statePollTimer) {
      clearInterval(statePollTimer);
      statePollTimer = null;
    }
  }

  function startStateStream() {
    if (stateStream) return;
    try {
      stateStream = new EventSource("/state_stream");
      let gotStateEvent = false;

      const handleStateEvent = (ev) => {
        try {
          const data = JSON.parse(ev.data || "{}");
          if (data.state) {
            setPowerUI(data.state);
            if (!gotStateEvent) {
              stopStatePolling();
              gotStateEvent = true;
            }
          }
        } catch (e) {
          console.warn("State stream parse error:", e);
        }
      };

      // Server emits event name "state"
      stateStream.addEventListener("state", handleStateEvent);
      // Fallback if server ever sends unnamed messages
      stateStream.onmessage = handleStateEvent;

      stateStream.onerror = () => {
        if (stateStream) {
          stateStream.close();
          stateStream = null;
        }
        startStatePolling();
      };
    } catch (err) {
      console.warn("State stream failed to start:", err);
      startStatePolling();
    }
  }

  function initPowerButton() {
    const btn = powerEl();
    if (btn) {
      btn.addEventListener("click", onPowerClick);
    }
    // Initial state via SSE (with polling fallback)
    startStateStream();
    startStatePolling(); // will be stopped when SSE opens
  }

  // ========================================================
  // ===============  USER / ADMIN MENU & TABS  =============
  // ========================================================

  function toggleUserMenu() {
    const menu = document.getElementById("userMenu");
    if (!menu) return;
    menu.style.display = menu.style.display === "block" ? "none" : "block";
  }

  document.addEventListener("click", (e) => {
    const menu = document.getElementById("userMenu");
    const icon = document.querySelector(".user-icon");
    if (!menu || !icon) return;
    if (!icon.contains(e.target) && !menu.contains(e.target)) {
      menu.style.display = "none";
    }
  });

  // ========================================================
  // ===============  OUTPUT RENDERING HELPERS  =============
  // ========================================================

  function renderAllOutputs(containerId, isControlMode) {
    const container = document.getElementById(containerId);
    if (!container) return;

    container.innerHTML = "";

    for (let i = 1; i <= 10; i++) {
      const block = document.createElement("div");
      block.className = "manual-item";
      if (!isControlMode) block.classList.add("access-style");

      if (isControlMode) {
        block.title = "Manual control: toggle Output " + i;
        block.innerHTML = `
          <span>Output ${i}</span>
          <label class="switch">
            <input type="checkbox" title="Toggle Output ${i} (manual mode)" aria-label="Toggle Output ${i}" onchange="handleOutputToggle(${i}, this)">
            <span class="slider"></span>
          </label>
          <div class="led" id="manualLed${i}" title="Output ${i} state indicator"></div>
        `;
      } else {
        block.title = "Authorize Output " + i + " for loop/calibration";
        block.innerHTML = `
          <span>Allow Output ${i}</span>
          <label class="switch">
            <input type="checkbox" id="accessToggle${i}" title="Authorize Output ${i} for loop/calibration" aria-label="Authorize Output ${i}" onchange="updateOutputAccess(${i}, this.checked)">
            <span class="slider"></span>
          </label>
        `;
      }

      container.appendChild(block);
    }

    // Ensure tooltips exist for newly created toggles.
    applyTooltips(document);
  }

  function enableDragScroll(containerId) {
    const container = document.getElementById(containerId);
    if (!container) return;

    let isDown = false;
    let startX = 0;
    let scrollLeft = 0;

    container.style.scrollBehavior = "smooth";

    container.addEventListener("mousedown", (e) => {
      isDown = true;
      container.classList.add("dragging");
      startX = e.pageX - container.offsetLeft;
      scrollLeft = container.scrollLeft;
    });

    ["mouseleave", "mouseup"].forEach((evt) => {
      container.addEventListener(evt, () => {
        isDown = false;
        container.classList.remove("dragging");
      });
    });

    container.addEventListener("mousemove", (e) => {
      if (!isDown) return;
      e.preventDefault();
      const x = e.pageX - container.offsetLeft;
      const walk = (x - startX) * 2;
      container.scrollLeft = scrollLeft - walk;
    });

    container.addEventListener(
      "wheel",
      (e) => {
        if (e.deltaY !== 0) {
          e.preventDefault();
          container.scrollLeft += e.deltaY * 0.3;
        }
      },
      { passive: false }
    );
  }

  // ========================================================
  // ===============      MODE / LT TOGGLES      ============
  // ========================================================

  async function toggleMode() {
    const isManual = isManualMode();
    setModeDot(isManual);

    await sendControlCommand("set", "mode", isManual);

    if (isManual && lastState === "Running") {
      const r = await sendControlCommand("set", "abortAuto", true);
      if (!r || r.error) await shutdownSystem();
      setPowerUI("Idle");
    }
  }

  function toggleLT() {
    const ltToggle = document.getElementById("ltToggle");
    const isOn = !!(ltToggle && ltToggle.checked);
    sendControlCommand("set", "ledFeedback", isOn);
  }

  // ========================================================
  // ===============    MANUAL OUTPUT CONTROL    ============
  // ========================================================

  async function handleOutputToggle(index, checkbox) {
    await ensureManualTakeover("output" + index);
    const isOn = !!checkbox.checked;
    const led = checkbox.parentElement.nextElementSibling;
    if (led) led.classList.toggle("active", isOn);
    const resp = await sendControlCommand("set", "output" + index, isOn);
    if (resp && resp.error) {
      if (led) led.classList.toggle("active", !isOn);
      checkbox.checked = !isOn;
    }
  }

  function updateOutputAccess(index, newState) {
    sendControlCommand("set", "Access" + index, !!newState);
  }

  function toggleOutput(index, state) {
    sendControlCommand("set", "output" + index, !!state);
  }

  // ========================================================
  // ===============     SYSTEM CONTROL CMDS     ============
  // ========================================================

  async function startSystem() {
    if (isManualMode()) {
      openAlert(
        "Manual mode is ON",
        "Switch to Auto before starting the cycle.",
        "warning"
      );
      return;
    }
    await sendControlCommand("set", "systemStart", true);
  }

  async function shutdownSystem() {
    await sendControlCommand("set", "systemShutdown", true);
  }

  async function forceCalibration() {
    if (calibrationBusy) {
      openAlert(
        "Calibration",
        "A calibration is already in progress.",
        "warning"
      );
      return;
    }

    calibrationBusy = true;
    try {
      const res = await sendControlCommand("set", "calibrate", true);
      if (res && !res.error) {
        openAlert("Calibration", "Calibration started.", "success");
      } else {
        openAlert(
          "Calibration",
          (res && res.error) || "Failed to start calibration",
          "danger"
        );
      }
    } catch (err) {
      console.error("Calibration request failed:", err);
      openAlert("Calibration", "Request failed.", "danger");
    } finally {
      calibrationBusy = false;
    }
  }

  async function resetSystem() {
    await sendControlCommand("set", "systemReset", true);
  }

  async function rebootSystem() {
    await sendControlCommand("set", "reboot", true);
  }

  // ========================================================
  // ===============       HEARTBEAT / LOGIN     ============
  // ========================================================

  function startHeartbeat(intervalMs = 1500) {
    // Heartbeat disabled: session stays active while page is open.
  }

  function disconnectDevice() {
    fetch("/disconnect", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ action: "disconnect" }),
      redirect: "follow",
    })
      .then((response) => {
        if (response.redirected) {
          window.location.href = "http://powerboard.local/login";
        } else {
          return response.json().then((data) => {
            openAlert("Disconnect", data.error || "Unexpected response");
          });
        }
      })
      .catch((err) => {
        console.error("Disconnect failed:", err);
        window.location.href = "http://powerboard.local/login";
      });
  }

  // ========================================================
  // ===============      CONTROL ENDPOINT     ==============
  // ========================================================

  /**
   * Core helper for /control endpoint.
   * @param {string} action
   * @param {string} target
   * @param {any} [value]
   * @returns {Promise<any>}
   */
  async function sendControlCommand(action, target, value) {
    const payload = { action, target };
    if (value !== undefined) payload.value = value;

    try {
      const res = await fetch("/control", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(payload),
      });

      let data = {};
      try {
        data = await res.json();
      } catch {
        data = {};
      }

      if (!res.ok) {
        console.warn("Control error:", res.status, data.error || data);
        return { error: data.error || "HTTP " + res.status };
      }

      const applied = data.applied === true || data.status === "ok";
      if (applied) {
        console.log(`[ack] ${action} '${target}' -> applied`);
        return { ok: true, ...data };
      }

      if (data.state) {
        console.log("[state] State:", data.state);
        return { ok: true, state: data.state };
      }

      const errMsg = data.error || "unknown_error";
      console.warn("[ack-fail]", errMsg);
      return { error: errMsg };
    } catch (err) {
      console.error("Control request failed:", err);
      return { error: String(err) };
    }
  }

  function setMuteUI(muted) {
    const btn = document.getElementById("muteBtn");
    const icon = document.getElementById("muteIcon");
    if (!btn || !icon) return;

    btn.classList.toggle("muted", !!muted);
    icon.src = muted ? "icons/mute-2-256.png" : "icons/volume-up-4-256.png";
    icon.alt = muted ? "Muted" : "Sound";
  }

  async function toggleMute() {
    isMuted = !isMuted;
    setMuteUI(isMuted);
    await sendControlCommand("set", "buzzerMute", isMuted);
  }

  // ========================================================
  // ===============  DEVICE + NICHROME SETTINGS ============
  // ========================================================

  const TIMING_PRESETS = {
    sequential: {
      hot: { onMs: 10, offMs: 2 },
      medium: { onMs: 10, offMs: 15 },
      gentle: { onMs: 10, offMs: 75 },
    },
    advanced: {
      hot: { onMs: 10, offMs: 15 },
      medium: { onMs: 10, offMs: 45 },
      gentle: { onMs: 10, offMs: 120 },
    },
  };

  function getLoopMode() {
    const loopModeSelect = document.getElementById("loopModeSelect");
    return (loopModeSelect && loopModeSelect.value) || "advanced";
  }

  function getTimingMode() {
    const timingModeSelect = document.getElementById("timingModeSelect");
    return (timingModeSelect && timingModeSelect.value) || "preset";
  }

  function inferTimingProfile(loopMode, onMs, offMs) {
    const on = Number(onMs);
    const off = Number(offMs);
    if (!Number.isFinite(on) || !Number.isFinite(off)) return null;
    if (on !== 10) return null;

    if (loopMode === "sequential") {
      if (off <= 5) return "hot";
      if (off <= 20) return "medium";
      return "gentle";
    }

    // advanced
    if (off <= 20) return "hot";
    if (off <= 60) return "medium";
    return "gentle";
  }

  function setTimingVisibility() {
    const mode = getTimingMode();
    const manual = mode === "manual";

    const onField = document.getElementById("onTimeField");
    const offField = document.getElementById("offTimeField");
    const profileField = document.getElementById("timingProfileField");
    const resolvedField = document.getElementById("timingResolvedField");

    if (onField) onField.style.display = manual ? "" : "none";
    if (offField) offField.style.display = manual ? "" : "none";
    if (profileField) profileField.style.display = manual ? "none" : "";
    if (resolvedField) resolvedField.style.display = manual ? "none" : "";
  }

  function renderTimingResolved() {
    const resolved = document.getElementById("timingResolved");
    if (!resolved) return;

    const onMs = getInt("onTime");
    const offMs = getInt("offTime");
    if (onMs === undefined || offMs === undefined) {
      resolved.value = "--";
      return;
    }

    resolved.value = "Ton " + onMs + " ms / Toff " + offMs + " ms";
  }

  function applyTimingPreset() {
    const loopMode = getLoopMode();
    const profileSelect = document.getElementById("timingProfileSelect");
    const profile = (profileSelect && profileSelect.value) || "medium";

    const preset =
      (TIMING_PRESETS[loopMode] && TIMING_PRESETS[loopMode][profile]) || null;
    if (!preset) return;

    setField("onTime", preset.onMs);
    setField("offTime", preset.offMs);
    renderTimingResolved();
  }

  function syncTimingUiFromCurrentFields() {
    const timingModeSelect = document.getElementById("timingModeSelect");
    const profileSelect = document.getElementById("timingProfileSelect");
    if (!timingModeSelect || !profileSelect) return;

    const loopMode = getLoopMode();
    const onMs = getInt("onTime");
    const offMs = getInt("offTime");
    const inferred = inferTimingProfile(loopMode, onMs, offMs);

    if (inferred) {
      timingModeSelect.value = "preset";
      profileSelect.value = inferred;
    } else {
      timingModeSelect.value = "manual";
    }

    setTimingVisibility();
    renderTimingResolved();
  }

  function bindTimingUi() {
    const timingModeSelect = document.getElementById("timingModeSelect");
    const profileSelect = document.getElementById("timingProfileSelect");
    const loopModeSelect = document.getElementById("loopModeSelect");

    if (timingModeSelect) {
      timingModeSelect.addEventListener("change", () => {
        setTimingVisibility();
        if (getTimingMode() === "preset") {
          applyTimingPreset();
        } else {
          renderTimingResolved();
        }
      });
    }

    if (profileSelect) {
      profileSelect.addEventListener("change", () => {
        if (getTimingMode() === "preset") {
          applyTimingPreset();
        }
      });
    }

    if (loopModeSelect) {
      loopModeSelect.addEventListener("change", () => {
        if (getTimingMode() === "preset") {
          applyTimingPreset();
        } else {
          renderTimingResolved();
        }
      });
    }

    // Initial visibility (default: preset)
    setTimingVisibility();
    renderTimingResolved();
  }

  async function waitUntilApplied(expected, timeoutMs = 2000, stepMs = 120) {
    const deadline = Date.now() + timeoutMs;

    while (Date.now() < deadline) {
      try {
        const res = await fetch("/load_controls", { cache: "no-store" });
        if (!res.ok) break;
        const data = await res.json();

        let ok = true;

        if (expected.targetRes != null) {
          if (!approxEqual(data.targetRes, expected.targetRes, 0.1)) {
            ok = false;
          }
        }

        if (expected.wireRes) {
          const wr = data.wireRes || {};
          for (const [idx, val] of Object.entries(expected.wireRes)) {
            if (!approxEqual(wr[String(idx)], val, 0.05)) {
              ok = false;
              break;
            }
          }
        }

        if (ok) {
          await loadControls();
          return true;
        }
      } catch (err) {
        console.warn("waitUntilApplied error:", err);
        break;
      }

      await sleep(stepMs);
    }

    await loadControls();
    return false;
  }

  async function saveDeviceAndNichrome() {
    const cmds = [];
    const expected = { wireRes: {}, targetRes: null };
    const cur = lastLoadedControls || {};

    // Core device settings
    const acFreq = getInt("acFrequency");
    if (acFreq !== undefined && acFreq !== cur.acFrequency) {
      cmds.push(["set", "acFrequency", acFreq]);
    }

    const chargeR = getFloat("chargeResistor");
    if (
      chargeR !== undefined &&
      !approxEqual(chargeR, cur.chargeResistor, 0.05)
    ) {
      cmds.push(["set", "chargeResistor", chargeR]);
    }

    const currLimit = getFloat("currLimit");
    if (currLimit !== undefined && !approxEqual(currLimit, cur.currLimit, 0.05)) {
      cmds.push(["set", "currLimit", currLimit]);
    }

    const tempWarnC = getFloat("tempWarnC");
    if (
      tempWarnC !== undefined &&
      !approxEqual(tempWarnC, cur.tempWarnC, 0.05)
    ) {
      cmds.push(["set", "tempWarnC", tempWarnC]);
    }

    const tempTripC = getFloat("tempTripC");
    if (
      tempTripC !== undefined &&
      !approxEqual(tempTripC, cur.tempTripC, 0.05)
    ) {
      cmds.push(["set", "tempTripC", tempTripC]);
    }

    const idleCurrentA = getFloat("idleCurrentA");
    if (
      idleCurrentA !== undefined &&
      !approxEqual(idleCurrentA, cur.idleCurrentA, 0.01)
    ) {
      cmds.push(["set", "idleCurrentA", idleCurrentA]);
    }

    const wireTauSec = getFloat("wireTauSec");
    if (
      wireTauSec !== undefined &&
      !approxEqual(wireTauSec, cur.wireTauSec, 0.01)
    ) {
      cmds.push(["set", "wireTauSec", wireTauSec]);
    }

    const onTime = getInt("onTime");
    if (onTime !== undefined && onTime !== cur.onTime) {
      cmds.push(["set", "onTime", onTime]);
    }

    const offTime = getInt("offTime");
    if (offTime !== undefined && offTime !== cur.offTime) {
      cmds.push(["set", "offTime", offTime]);
    }

    const coolingToggle = document.getElementById("coolingAirToggle");
    if (coolingToggle) {
      const val = !!coolingToggle.checked;
      if (val !== cur.coolingAir) {
        cmds.push(["set", "coolingAir", val]);
      }
    }

    const coolBuried = getFloat("coolBuriedScale");
    if (
      coolBuried !== undefined &&
      !approxEqual(coolBuried, cur.coolBuriedScale, 0.001)
    ) {
      cmds.push(["set", "coolBuriedScale", coolBuried]);
    }

    const coolK = getFloat("coolKCold");
    if (coolK !== undefined && !approxEqual(coolK, cur.coolKCold, 0.0001)) {
      cmds.push(["set", "coolKCold", coolK]);
    }

    const coolDrop = getFloat("coolDropMaxC");
    if (
      coolDrop !== undefined &&
      !approxEqual(coolDrop, cur.coolDropMaxC, 0.001)
    ) {
      cmds.push(["set", "coolDropMaxC", coolDrop]);
    }

    const loopModeSelect = document.getElementById("loopModeSelect");
    if (loopModeSelect) {
      const modeVal = loopModeSelect.value || "advanced";
      if (modeVal !== cur.loopMode) {
        cmds.push(["set", "loopMode", modeVal]);
      }
    }

    const timingModeSelect = document.getElementById("timingModeSelect");
    if (timingModeSelect) {
      const val = timingModeSelect.value || "preset";
      if (val !== cur.timingMode) {
        cmds.push(["set", "timingMode", val]);
      }
    }

    const timingProfileSelect = document.getElementById("timingProfileSelect");
    if (timingProfileSelect) {
      const val = timingProfileSelect.value || "medium";
      if (val !== cur.timingProfile) {
        cmds.push(["set", "timingProfile", val]);
      }
    }

    const wireGauge = getInt("wireGauge");
    if (wireGauge !== undefined && wireGauge !== cur.wireGauge) {
      cmds.push(["set", "wireGauge", wireGauge]);
    }

    // Wire resistances R01..R10 -> wireRes1..wireRes10
    const curWr = cur.wireRes || {};
    for (let i = 1; i <= 10; i++) {
      const id = "r" + String(i).padStart(2, "0") + "ohm";
      const val = getFloat(id);
      const curVal = curWr[String(i)];
      if (val !== undefined && !approxEqual(val, curVal, 0.05)) {
        cmds.push(["set", "wireRes" + i, val]);
        expected.wireRes[String(i)] = val;
      }
    }

    // Target resistance
    const tgt = getFloat("rTarget");
    if (tgt !== undefined && !approxEqual(tgt, cur.targetRes, 0.05)) {
      cmds.push(["set", "targetRes", tgt]);
      expected.targetRes = tgt;
    }

    const wireOhmPerM = getFloat("wireOhmPerM");
    if (
      wireOhmPerM !== undefined &&
      !approxEqual(wireOhmPerM, cur.wireOhmPerM, 0.001)
    ) {
      cmds.push(["set", "wireOhmPerM", wireOhmPerM]);
    }

    if (!cmds.length) {
      return;
    }

    // Send sequentially to preserve ordering
    for (const [a, t, v] of cmds) {
      await sendControlCommand(a, t, v);
    }

    await waitUntilApplied(expected, 2000, 120);
  }

  function resetDeviceAndNichrome() {
    if (!lastLoadedControls) {
      loadControls();
      return;
    }

    const data = lastLoadedControls;

    setField("acFrequency", data.acFrequency);
    setField("chargeResistor", data.chargeResistor);
    setField("currLimit", data.currLimit);
    setField("tempWarnC", data.tempWarnC);
    setField("tempTripC", data.tempTripC);
    setField("idleCurrentA", data.idleCurrentA);
    setField("wireTauSec", data.wireTauSec);
    setField("onTime", data.onTime);
    setField("offTime", data.offTime);
    const coolingToggle = document.getElementById("coolingAirToggle");
    if (coolingToggle) coolingToggle.checked = !!data.coolingAir;
    const loopModeSelect = document.getElementById("loopModeSelect");
    if (loopModeSelect && data.loopMode) {
      loopModeSelect.value = data.loopMode;
    }
    const timingModeSelect = document.getElementById("timingModeSelect");
    if (timingModeSelect && data.timingMode) {
      timingModeSelect.value = data.timingMode;
    }
    const timingProfileSelect = document.getElementById("timingProfileSelect");
    if (timingProfileSelect && data.timingProfile) {
      timingProfileSelect.value = data.timingProfile;
    }
    if (getTimingMode() === "preset") {
      applyTimingPreset();
    } else {
      setTimingVisibility();
      renderTimingResolved();
    }
    syncTimingUiFromCurrentFields();
    if (data.wireGauge !== undefined) {
      setField("wireGauge", data.wireGauge);
    }
    if (data.coolBuriedScale !== undefined) {
      setField("coolBuriedScale", data.coolBuriedScale);
    }
    if (data.coolKCold !== undefined) {
      setField("coolKCold", data.coolKCold);
    }
    if (data.coolDropMaxC !== undefined) {
      setField("coolDropMaxC", data.coolDropMaxC);
    }
    if (data.wireOhmPerM !== undefined) {
      setField("wireOhmPerM", data.wireOhmPerM);
    }

    const wr = data.wireRes || {};
    for (let i = 1; i <= 10; i++) {
      const key = String(i);
      setField("r" + key.padStart(2, "0") + "ohm", wr[key]);
    }

    setField("rTarget", data.targetRes);
  }

  // ========================================================
  // ===============        LOAD CONTROLS        ============
  // ========================================================

  async function loadControls() {
    try {
      const res = await fetch("/load_controls", { cache: "no-store" });
      if (!res.ok) throw new Error("HTTP " + res.status);
      const data = await res.json();

      console.log("Fetched /load_controls:", data);

      lastLoadedControls = data;

      // LT toggle
      const ltToggle = document.getElementById("ltToggle");
      if (ltToggle) ltToggle.checked = !!data.ledFeedback;

      // Manual/Auto mode initial sync
      const modeToggle = document.getElementById("modeToggle");
      if (modeToggle && data.manualMode !== undefined) {
        modeToggle.checked = !!data.manualMode;
      }
      setModeDot(isManualMode());

      // Buzzer mute initial sync (accept bool, 0/1, "true"/"false")
      if (data.buzzerMute !== undefined) {
        const muted =
          data.buzzerMute === true ||
          data.buzzerMute === 1 ||
          data.buzzerMute === "1" ||
          data.buzzerMute === "true";
        isMuted = muted;
        setMuteUI(isMuted);
        console.log(
          "[Mute] State from /load_controls:",
          data.buzzerMute,
          "=> isMuted =",
          isMuted
        );
      }

      // Ready / Off LEDs
      const readyLed = document.getElementById("readyLed");
      const offLed = document.getElementById("offLed");
      if (readyLed)
        readyLed.style.backgroundColor = data.ready ? "limegreen" : "gray";
      if (offLed) offLed.style.backgroundColor = data.off ? "red" : "gray";

      // Device numeric fields
      setField("acFrequency", data.acFrequency);
      setField("chargeResistor", data.chargeResistor);
      setField("currLimit", data.currLimit);
      setField("tempWarnC", data.tempWarnC);
      setField("tempTripC", data.tempTripC);
      setField("idleCurrentA", data.idleCurrentA);
      setField("wireTauSec", data.wireTauSec);
      setField("onTime", data.onTime);
      setField("offTime", data.offTime);
      if (data.deviceId !== undefined) setField("userDeviceId", data.deviceId);
      if (data.wifiSSID !== undefined) setField("wifiSSID", data.wifiSSID);
      const fanSlider = document.getElementById("fanSlider");
      if (fanSlider && typeof data.fanSpeed === "number") {
        fanSlider.value = data.fanSpeed;
      }
      const coolingToggle = document.getElementById("coolingAirToggle");
      if (coolingToggle) coolingToggle.checked = !!data.coolingAir;
      const loopModeSelect = document.getElementById("loopModeSelect");
      if (loopModeSelect && data.loopMode) {
        loopModeSelect.value = data.loopMode;
      }
      const timingModeSelect = document.getElementById("timingModeSelect");
      if (timingModeSelect && data.timingMode) {
        timingModeSelect.value = data.timingMode;
      }
      const timingProfileSelect = document.getElementById("timingProfileSelect");
      if (timingProfileSelect && data.timingProfile) {
        timingProfileSelect.value = data.timingProfile;
      }
      if (getTimingMode() === "preset") {
        applyTimingPreset();
      } else {
        setTimingVisibility();
        renderTimingResolved();
      }
      syncTimingUiFromCurrentFields();
      if (data.wireGauge !== undefined) {
        setField("wireGauge", data.wireGauge);
      }
      if (data.coolBuriedScale !== undefined) {
        setField("coolBuriedScale", data.coolBuriedScale);
      }
      if (data.coolKCold !== undefined) {
        setField("coolKCold", data.coolKCold);
      }
      if (data.coolDropMaxC !== undefined) {
        setField("coolDropMaxC", data.coolDropMaxC);
      }
      if (data.wireOhmPerM !== undefined) {
        setField("wireOhmPerM", data.wireOhmPerM);
      }
      if (data.capacitanceF !== undefined) {
        renderCapacitance(parseFloat(data.capacitanceF));
      }

      // Nichrome R01..R10 from wireRes{1..10}
      const wireRes = data.wireRes || {};
      for (let i = 1; i <= 10; i++) {
        const v = wireRes[String(i)];
        setField("r" + String(i).padStart(2, "0") + "ohm", v);
      }

      // Target
      setField("rTarget", data.targetRes);

      // Output states
      const states = data.outputs || {};
      for (let i = 1; i <= 10; i++) {
        const checked = !!states["output" + i];
        const itemSel = "#manualOutputs .manual-item:nth-child(" + i + ")";
        const checkbox = document.querySelector(
          itemSel + ' input[type="checkbox"]'
        );
        const led = document.querySelector(itemSel + " .led");
        if (checkbox) checkbox.checked = checked;
        if (led) led.classList.toggle("active", checked);
      }

      // Output Access flags
      const access = data.outputAccess || {};
      for (let i = 1; i <= 10; i++) {
        const checkbox = document.querySelector(
          "#userAccessGrid .manual-item:nth-child(" +
            i +
            ') input[type="checkbox"]'
        );
        if (checkbox) checkbox.checked = !!access["output" + i];
      }

      // Relay mirror
      const relayFromServer = !!data.relay;
      const relayToggle = document.getElementById("relayToggle");
      if (relayToggle) relayToggle.checked = relayFromServer;
      if (typeof setDot === "function") setDot("relay", relayFromServer);

      // Apply power button look from LEDs if desired
      applyReadyOffFlagsToPower(data.ready, data.off);
    } catch (err) {
      console.error("Failed to load controls:", err);
    }
  }

  function applyReadyOffFlagsToPower(readyBool, offBool) {
    if (offBool) {
      setPowerUI("Shutdown", { ready: false });
    } else if (lastState === "Running") {
      setPowerUI("Running", { ready: !!readyBool });
    }
  }

  // ========================================================
  // ===============    USER / ADMIN SETTINGS    ============
  // ========================================================

  function saveUserSettings() {
    const current = document.getElementById("userCurrentPassword").value;
    const newPass = document.getElementById("userNewPassword").value;
    const newId = document.getElementById("userDeviceId").value;

    sendControlCommand("set", "userCredentials", {
      current,
      newPass,
      newId,
    }).then((res) => {
      if (res && !res.error) {
        openAlert("User Settings", "User credentials updated.", "success");
        resetUserSettings();
      } else if (res && res.error) {
        openAlert("User Settings", res.error, "danger");
      }
    });
  }

  function resetUserSettings() {
    setField("userCurrentPassword", "");
    setField("userNewPassword", "");
    setField("userDeviceId", "");
  }

  function saveAdminSettings() {
    const current = document.getElementById("adminCurrentPassword").value;
    const username = document.getElementById("adminUsername").value;
    const password = document.getElementById("adminPassword").value;
    const wifiSSID = document.getElementById("wifiSSID").value;
    const wifiPassword = document.getElementById("wifiPassword").value;

    sendControlCommand("set", "adminCredentials", {
      current,
      username,
      password,
      wifiSSID,
      wifiPassword,
    }).then((res) => {
      if (res && !res.error) {
        openAlert("Admin Settings", "Admin settings updated.", "success");
        resetAdminSettings();
      } else if (res && res.error) {
        openAlert("Admin Settings", res.error, "danger");
      }
    });
  }

  function resetAdminSettings() {
    setField("adminCurrentPassword", "");
    setField("adminUsername", "");
    setField("adminPassword", "");
    setField("wifiSSID", "");
    setField("wifiPassword", "");
  }

  // ========================================================
  // ===============         LIVE OVERLAY        ============
  // ========================================================

  const LIVE = {
    svg: null,
    interval: null,
    markers: [],
    tempMarkers: [],
  };

  (function initLiveMarkers() {
    deltax = 10;
    const offset = 2.6;
    const l = 3;
    const h = 2.5 - deltax;

    LIVE.markers = [
      // AC (red) bottom-left
      {
        id: "ac",
        color: "red",
        x: 8 + h,
        y: 81 + offset,
        tx: 35, // where you want it to touch near live-core
        ty: 65,
        layout: "L", // 90° path
      },
      // Relay (yellow) bottom-right
      {
        id: "relay",
        color: "yellow",
        x: 92 - h,
        y: 81 + offset,
        tx: 65, // where you want it to touch near live-core
        ty: 65,
        layout: "L", // 90° path
      },
      // Left side outputs 6..10
      {
        id: "o6",
        color: "cyan",
        x: 8 + h,
        y: 11 + offset,
        tx: 45,
        ty: 40,
        layout: "L", // vertical then horizontal
      },
      {
        id: "o7",
        color: "cyan",
        x: 8 + h,
        y: 23 + offset,
        tx: 35,
        ty: 40,
        layout: "L", // vertical then horizontal
      },
      {
        id: "o8",
        color: "cyan",
        x: 8 + h,
        y: 35 + offset,
        tx: 45,
        ty: 35 + offset,
        layout: "straight",
      },
      {
        id: "o9",
        color: "cyan",
        x: 8 + h,
        y: 47 + offset,
        tx: 45,
        ty: 47 + offset,
        layout: "straight",
      },
      {
        id: "o10",
        color: "cyan",
        x: 8 + h,
        y: 59 + offset,
        tx: 45,
        ty: 59 + offset,
        layout: "straight",
      },
      // Right side outputs 5..1
      {
        id: "o5",
        color: "cyan",
        x: 92 - h,
        y: 11 + offset,
        tx: 55,
        ty: 40,
        layout: "L", // vertical then horizontal
      },
      {
        id: "o4",
        color: "cyan",
        x: 92 - h,
        y: 23 + offset,
        tx: 65,
        ty: 40,
        layout: "L", // vertical then horizontal
      },
      {
        id: "o3",
        color: "cyan",
        x: 92 - h,
        y: 35 + offset,
        tx: 45,
        ty: 35 + offset,
        layout: "straight",
      },
      {
        id: "o2",
        color: "cyan",
        x: 92 - h,
        y: 47 + offset,
        tx: 45,
        ty: 47 + offset,
        layout: "straight",
      },
      {
        id: "o1",
        color: "cyan",
        x: 92 - h,
        y: 59 + offset,
        tx: 45,
        ty: 59 + offset,
        layout: "straight",
      },
    ];
    delaty = 2.5;

    LIVE.tempMarkers = [
      // near left outputs 6..10
      { wire: 6, x: 20 - deltax, y: 11 + delaty },
      { wire: 7, x: 20 - deltax, y: 23 + delaty },
      { wire: 8, x: 20 - deltax, y: 35 + delaty },
      { wire: 9, x: 20 - deltax, y: 47 + delaty },
      { wire: 10, x: 20 - deltax, y: 59 + delaty },
      // near right outputs 5..1
      { wire: 5, x: 80 + deltax, y: 11 + delaty },
      { wire: 4, x: 80 + deltax, y: 23 + delaty },
      { wire: 3, x: 80 + deltax, y: 35 + delaty },
      { wire: 2, x: 80 + deltax, y: 47 + delaty },
      { wire: 1, x: 80 + deltax, y: 59 + delaty },
    ];
  })();

  function liveRender() {
    const svg = document.querySelector("#liveTab .live-overlay");
    if (!svg) return;

    LIVE.svg = svg;
    svg.innerHTML = "";

    const ns = "http://www.w3.org/2000/svg";
    const coreBox = computeCoreBox(svg);
    LIVE.coreBox = coreBox;

    // ---- Traces + dots ----
    for (const m of LIVE.markers) {
      const pts = buildTracePoints(m, coreBox);
      if (!pts || pts.length < 2) continue;

      // Draw the trace as a polyline (supports straight or L)
      const poly = document.createElementNS(ns, "polyline");
      poly.setAttribute("class", "trace");
      poly.setAttribute("points", pts.map((p) => `${p.x},${p.y}`).join(" "));
      svg.appendChild(poly);

      // Dot at the first point (wire endpoint)
      const first = pts[0];
      const dot = document.createElementNS(ns, "circle");
      dot.setAttribute("class", "dot " + m.color + " off");
      dot.setAttribute("r", 3.2);
      dot.setAttribute("cx", first.x);
      dot.setAttribute("cy", first.y);
      dot.dataset.id = m.id;
      svg.appendChild(dot);
    }

    // ---- Temperature badges (unchanged) ----
    for (const t of LIVE.tempMarkers) {
      const g = document.createElementNS(ns, "g");
      g.setAttribute("class", "temp-badge");
      g.dataset.wire = String(t.wire);

      const c = document.createElementNS(ns, "circle");
      c.setAttribute("class", "temp-circle");
      c.setAttribute("r", 4.3);
      c.setAttribute("cx", t.x);
      c.setAttribute("cy", t.y);

      const txt = document.createElementNS(ns, "text");
      txt.setAttribute("class", "temp-label");
      txt.setAttribute("x", t.x);
      txt.setAttribute("y", t.y + 0.3);
      txt.textContent = "--";

      g.appendChild(c);
      g.appendChild(txt);
      svg.appendChild(g);
    }
  }

  function setDot(id, state) {
    if (!LIVE.svg) return;

    const c = LIVE.svg.querySelector('circle[data-id="' + id + '"]');
    if (!c) return;

    // Remove previous state flags
    c.classList.remove("on", "off", "missing");

    // Support:
    //  - true  => ON
    //  - false => OFF (connected)
    //  - "missing"/"disconnected"/null => not connected (cyan / missing)
    if (state === "missing" || state === "disconnected" || state === null) {
      c.classList.add("missing");
      return;
    }

    const on = !!state;
    c.classList.toggle("on", on);
    c.classList.toggle("off", !on);
  }

  async function pollLiveOnce() {
    try {
      const res = await fetch("/monitor", { cache: "no-store" });
      if (res.status === 403) {
        window.location.href = "http://powerboard.local/login";
        return;
      }
      if (!res.ok) return;
      const mon = await res.json();
      // --- Session stats (Live tab headline) ---
      if (mon.session) {
        updateSessionStatsUI(mon.session);
      }

      // --- Lifetime counters ---
      if (mon.sessionTotals) {
        const t = mon.sessionTotals;
        const totalEnergyEl = document.getElementById("totalEnergy");
        const totalSessionsEl = document.getElementById("totalSessions");
        const totalOkEl = document.getElementById("totalSessionsOk");

        if (totalEnergyEl)
          totalEnergyEl.textContent =
            (t.totalEnergy_Wh || 0).toFixed(2) + " Wh";
        if (totalSessionsEl)
          totalSessionsEl.textContent = (t.totalSessions || 0).toString();
        if (totalOkEl)
          totalOkEl.textContent = (t.totalSessionsOk || 0).toString();
      }

      const outs = mon.outputs || {};
      for (let i = 1; i <= 10; i++) {
        setDot("o" + i, !!outs["output" + i]);
      }

      // Per-wire temps
      const wireTemps = mon.wireTemps || [];
      if (LIVE.svg) {
        for (const cfg of LIVE.tempMarkers) {
          const badge = LIVE.svg.querySelector(
            'g.temp-badge[data-wire="' + cfg.wire + '"]'
          );
          if (!badge) continue;

          const label = badge.querySelector("text.temp-label");
          if (!label) continue;

          const t = wireTemps[cfg.wire - 1];
          let txt = "--";

          badge.classList.remove("warn", "hot");

          if (typeof t === "number" && t > -100) {
            const rounded = Math.round(t);
            txt = String(rounded);

            if (t >= 400) badge.classList.add("hot");
            else if (t >= 250) badge.classList.add("warn");

            badge.setAttribute(
              "title",
              "Wire " + cfg.wire + ": " + t.toFixed(1) + "°C"
            );
          } else {
            badge.removeAttribute("title");
          }

          label.textContent = txt;
        }
      }

      // Relay + AC
      const serverRelay = mon.relay === true;
      const ac = mon.ac === true;
      setDot("relay", serverRelay);
      setDot("ac", ac);

      const relayToggle = document.getElementById("relayToggle");
      if (relayToggle) relayToggle.checked = serverRelay;

      // Voltage gauge (cap voltage or fallback)
      const fallback = 325;
      let shownV = fallback;
      if (ac) {
        const v = parseFloat(mon.capVoltage);
        if (Number.isFinite(v)) shownV = v;
      }
      updateGauge("voltageValue", shownV, "V", 400);

      // Raw ADC display (scaled /100, e.g., 4095 -> 40.95)
      const adcEl = document.getElementById("adcRawValue");
      if (adcEl) {
        const rawScaled = parseFloat(mon.capAdcRaw);
        adcEl.textContent = Number.isFinite(rawScaled)
          ? rawScaled.toFixed(2)
          : "--";
      }

      // Current gauge
      let rawCurrent = parseFloat(mon.current);
      if (!ac || !Number.isFinite(rawCurrent)) rawCurrent = 0;
      rawCurrent = Math.max(0, Math.min(100, rawCurrent));
      updateGauge("currentValue", rawCurrent, "A", 100);

      // Temperatures (up to 12 sensors)
      const temps = mon.temperatures || [];
      for (let i = 0; i < 12; i++) {
        const id = "temp" + (i + 1) + "Value";
        const t = temps[i];
        if (t === -127 || t === undefined) {
          updateGauge(id, "Off", "AøC", 150);
        } else {
          updateGauge(id, Number(t), "AøC", 150);
        }
      }

      // Capacitance (F -> mF)
      const capF = parseFloat(mon.capacitanceF);
      renderCapacitance(capF);

      // Ready / Off LEDs
      const readyLed = document.getElementById("readyLed");
      const offLed = document.getElementById("offLed");
      if (readyLed) readyLed.style.backgroundColor = mon.ready ? "limegreen" : "gray";
      if (offLed) offLed.style.backgroundColor = mon.off ? "red" : "gray";

      // Fan slider reflect
      const fanSlider = document.getElementById("fanSlider");
      if (fanSlider && typeof mon.fanSpeed === "number") {
        fanSlider.value = mon.fanSpeed;
      }
    } catch (err) {
      console.warn("live poll failed:", err);
    }
  }

  // -------------------- Live SSE playback (constant speed) --------------------
  function applyLiveSample(sample) {
    if (!sample) return;

    // Outputs mask -> dots
    if (typeof sample.mask === "number") {
      for (let i = 0; i < 10; i++) {
        const on = !!(sample.mask & (1 << i));
        setDot("o" + (i + 1), on);
      }
    }

    // Wire temps -> badges
    const wireTemps = sample.wireTemps || [];
    if (LIVE.svg) {
      for (const cfg of LIVE.tempMarkers) {
        const badge = LIVE.svg.querySelector(
          'g.temp-badge[data-wire="' + cfg.wire + '"]'
        );
        if (!badge) continue;

        const label = badge.querySelector("text.temp-label");
        if (!label) continue;

        const t = wireTemps[cfg.wire - 1];
        let txt = "--";

        badge.classList.remove("warn", "hot");

        if (typeof t === "number" && t > -100) {
          const rounded = Math.round(t);
          txt = String(rounded);

          if (t >= 400) badge.classList.add("hot");
          else if (t >= 250) badge.classList.add("warn");

          badge.setAttribute(
            "title",
            "Wire " + cfg.wire + ": " + Number(t).toFixed(1) + "°C"
          );
        } else {
          badge.removeAttribute("title");
        }

        label.textContent = txt;
      }
    }

    setDot("relay", sample.relay === true);
    setDot("ac", sample.ac === true);

    const relayToggle = document.getElementById("relayToggle");
    if (relayToggle) relayToggle.checked = sample.relay === true;
  }

  // Live snapshots are polled from /monitor (no server push).
  function computeCoreBox(svg) {
    const core = document.querySelector("#liveTab .live-core");
    if (!svg || !core) return null;

    const vb = svg.viewBox.baseVal;
    const svgRect = svg.getBoundingClientRect();
    const coreRect = core.getBoundingClientRect();

    const scaleX = vb.width / svgRect.width;
    const scaleY = vb.height / svgRect.height;

    const x1 = (coreRect.left - svgRect.left) * scaleX;
    const y1 = (coreRect.top - svgRect.top) * scaleY;
    const x2 = (coreRect.right - svgRect.left) * scaleX;
    const y2 = (coreRect.bottom - svgRect.top) * scaleY;

    return {
      x1,
      y1,
      x2,
      y2,
      cx: (x1 + x2) / 2,
      cy: (y1 + y2) / 2,
    };
  }

  function clamp(v, min, max) {
    return v < min ? min : v > max ? max : v;
  }

  function makeTracePath(start, end, layout) {
    // layout:
    //  - "straight"  => direct line
    //  - "L" / "Lh"  => 90° with horizontal-then-vertical
    //  - "Lv"        => 90° with vertical-then-horizontal
    //  - undefined   => default "L"

    const mode = layout || "L";

    if (mode === "straight") {
      return [start, end];
    }

    // Decide orientation for L
    if (mode === "Lv") {
      // vertical then horizontal
      const mid = { x: start.x, y: end.y };
      return [start, mid, end];
    }

    // "L" or "Lh" or default:
    // horizontal then vertical
    const horizontalFirst =
      mode === "Lh" ||
      mode === "L" ||
      Math.abs(end.x - start.x) >= Math.abs(end.y - start.y);

    if (horizontalFirst) {
      const mid = { x: end.x, y: start.y };
      return [start, mid, end];
    } else {
      const mid = { x: start.x, y: end.y };
      return [start, mid, end];
    }
  }

  function getCoreAnchor(start, coreBox) {
    // Hit the nearest edge of the live-core box, perpendicular-style
    if (!coreBox) return start;

    const { x1, y1, x2, y2 } = coreBox;

    // Left of core -> go to left edge
    if (start.x < x1) {
      return { x: x1, y: clamp(start.y, y1, y2) };
    }

    // Right of core -> go to right edge
    if (start.x > x2) {
      return { x: x2, y: clamp(start.y, y1, y2) };
    }

    // Above core -> go to top edge
    if (start.y < y1) {
      return { x: clamp(start.x, x1, x2), y: y1 };
    }

    // Below core -> go to bottom edge
    return { x: clamp(start.x, x1, x2), y: y2 };
  }
  function buildTracePoints(m, coreBox) {
    const start = { x: m.x, y: m.y };

    // 1) Full manual override: explicit polyline
    //    m.points = [ {x,y}, {x,y}, ... ]
    if (Array.isArray(m.points) && m.points.length >= 2) {
      return m.points;
    }

    // 2) New: explicit end point with chosen layout
    //    m.tx, m.ty, optional m.layout
    if (typeof m.tx === "number" && typeof m.ty === "number") {
      const end = { x: m.tx, y: m.ty };
      return makeTracePath(start, end, m.layout);
    }

    // 3) Backward compat: ax/ay (old style)
    //    Now also respects m.layout if present
    if (typeof m.ax === "number" && typeof m.ay === "number") {
      const end = { x: m.ax, y: m.ay };
      return makeTracePath(start, end, m.layout);
    }

    // 4) If we can't see the core, only show the dot
    if (!coreBox) return [start];

    // 5) Auto: anchor on live-core box + layout rules
    const anchor = getCoreAnchor(start, coreBox);
    return makeTracePath(start, anchor, m.layout);
  }

  // ========================================================
  // ===============   Session Stats Frontend   =============
  // ========================================================
  //
  // Pure UI helpers; backend will call updateSessionStatsUI()
  // later with:
  // {
  //   valid: bool,
  //   running: bool,            // optional, if current session
  //   energy_Wh: number,
  //   duration_s: number,
  //   peakPower_W: number,
  //   peakCurrent_A: number
  // }

  function updateSessionStatsUI(session) {
    const statusEl = document.getElementById("sessionStatus");
    const eEl = document.getElementById("sessionEnergy");
    const dEl = document.getElementById("sessionDuration");
    const pWEl = document.getElementById("sessionPeakPower");
    const pAEl = document.getElementById("sessionPeakCurrent");

    if (!statusEl || !eEl || !dEl || !pWEl || !pAEl) return;

    // No data / no active session
    if (!session || !session.valid) {
      statusEl.textContent = "No active session";
      statusEl.className = "session-status session-status-idle";

      eEl.textContent = "-- Wh";
      dEl.textContent = "-- s";
      pWEl.textContent = "-- W";
      pAEl.textContent = "-- A";
      return;
    }

    const running = !!session.running;

    if (running) {
      statusEl.textContent = "Session running";
      statusEl.className = "session-status session-status-running";
    } else {
      statusEl.textContent = "Last session";
      statusEl.className = "session-status session-status-finished";
    }

    eEl.textContent = (session.energy_Wh ?? 0).toFixed(2) + " Wh";
    dEl.textContent = (session.duration_s ?? 0).toString() + " s";
    pWEl.textContent = (session.peakPower_W ?? 0).toFixed(1) + " W";
    pAEl.textContent = (session.peakCurrent_A ?? 0).toFixed(2) + " A";
  }

  // History modal controls (frontend only)
  function openSessionHistory() {
    const m = document.getElementById("sessionHistoryModal");
    if (!m) return;
    m.classList.add("show");
  }

  function closeSessionHistory() {
    const m = document.getElementById("sessionHistoryModal");
    if (!m) return;
    m.classList.remove("show");
  }

  function bindSessionHistoryButton() {
    const btn = document.getElementById("sessionHistoryBtn");
    if (!btn) return;

    btn.addEventListener("click", loadSessionHistoryAndOpen);
  }

  async function loadSessionHistoryAndOpen() {
    try {
      // Load from SPIFFS/static file
      const res = await fetch("/History.json", { cache: "no-store" });
      if (!res.ok) {
        console.error("Failed to load History.json:", res.status);
        openSessionHistory(); // Show empty modal instead of doing nothing
        return;
      }

      const data = await res.json();

      // Support both { history: [...] } and plain [...]
      const history = Array.isArray(data) ? data : data.history || [];

      const tbody = document.getElementById("sessionHistoryTableBody");
      const placeholder = document.querySelector(
        ".session-history-placeholder"
      );
      if (!tbody) {
        openSessionHistory();
        return;
      }

      // Clear previous rows
      tbody.innerHTML = "";

      // Hide "No history loaded yet" placeholder once we try to load
      if (placeholder) {
        placeholder.style.display = "none";
      }

      if (!history.length) {
        const tr = document.createElement("tr");
        const td = document.createElement("td");
        td.colSpan = 5;
        td.textContent = "No sessions recorded yet.";
        tr.appendChild(td);
        tbody.appendChild(tr);
      } else {
        history.forEach((s, idx) => {
          const tr = document.createElement("tr");

          const startSec = (s.start_ms || 0) / 1000;
          const duration = s.duration_s || 0;
          const energyWh = Number(s.energy_Wh || 0);
          const peakP = Number(s.peakPower_W || 0);
          const peakI = Number(s.peakCurrent_A || 0);

          tr.innerHTML = `
          <td>${idx + 1}</td>
          <td>${startSec.toFixed(0)} s</td>
          <td>${duration}s</td>
          <td>${energyWh.toFixed(2)} Wh</td>
          <td>${peakP.toFixed(1)} W</td>
          <td>${peakI.toFixed(2)} A</td>
        `;
          tbody.appendChild(tr);
        });
      }

      // ✅ Open the modal (this is the correct function)
      openSessionHistory();
    } catch (e) {
      console.error("Session history load failed", e);
    }
  }

  function scheduleLiveInterval() {
    if (monitorPollTimer) clearInterval(monitorPollTimer);
    const ms = lastState === "Running" ? 250 : 1000;
    pollLiveOnce();
    monitorPollTimer = setInterval(pollLiveOnce, ms);
  }

  // Hook power UI -> live interval
  const _origSetPowerUI = setPowerUI;
  setPowerUI = function (state, extras) {
    _origSetPowerUI(state, extras || {});
    scheduleLiveInterval();
  };

  // ========================================================
  // ===============        MONITOR GAUGES       ============
  // ========================================================

  function updateGauge(id, value, unit, maxValue) {
    const display = document.getElementById(id);
    if (!display) return;
    const svg = display.closest("svg");
    if (!svg) return;
    const stroke = svg.querySelector("path.gauge-fg");
    if (!stroke) return;

    if (value === "Off") {
      stroke.setAttribute("stroke-dasharray", "0, 100");
      display.textContent = "Off";
      return;
    }

    const num = parseFloat(value);
    if (!Number.isFinite(num)) return;

    const percent = Math.min((num / maxValue) * 100, 100);
    stroke.setAttribute("stroke-dasharray", percent + ", 100");
    display.textContent = num.toFixed(2) + unit;
  }

  function renderCapacitance(capF) {
    const display = document.getElementById("capacitanceValue");
    if (!display) return;

    const svg = display.closest("svg");
    const stroke = svg ? svg.querySelector("path.gauge-fg") : null;

    if (!Number.isFinite(capF) || capF <= 0) {
      if (stroke) {
        stroke.setAttribute("stroke-dasharray", "0, 100");
      }
      display.textContent = "--";
      return;
    }

    const capMilli = capF * 1000.0;
    updateGauge("capacitanceValue", capMilli, "mF", 500);
  }

  function startMonitorPolling(intervalMs = 400) {
    if (window.__MONITOR_INTERVAL__) {
      clearInterval(window.__MONITOR_INTERVAL__);
    }

    window.__MONITOR_INTERVAL__ = setInterval(async () => {
      try {
        const res = await fetch("/monitor", { cache: "no-store" });
        if (res.status === 403) {
          window.location.href = "http://powerboard.local/login";
          return;
        }
        if (!res.ok) return;
        const data = await res.json();

        // Relay sync
        const serverRelay = data.relay === true;
        setDot("relay", serverRelay);
        const relayToggle = document.getElementById("relayToggle");
        if (relayToggle) relayToggle.checked = serverRelay;

        // AC presence
        const ac = data.ac === true;
        setDot("ac", ac);

        // Voltage gauge (cap voltage or fallback)
      const fallback = 325;
        let shownV = fallback;
        if (ac) {
          const v = parseFloat(data.capVoltage);
          if (Number.isFinite(v)) shownV = v;
        }
        updateGauge("voltageValue", shownV, "V", 400);

        // Raw ADC display (scaled /100, e.g., 4095 -> 40.95)
        const adcEl = document.getElementById("adcRawValue");
        if (adcEl) {
          const rawScaled = parseFloat(data.capAdcRaw);
          adcEl.textContent = Number.isFinite(rawScaled)
            ? rawScaled.toFixed(2)
            : "--";
        }

        // Current gauge
        let rawCurrent = parseFloat(data.current);
        if (!ac || !Number.isFinite(rawCurrent)) rawCurrent = 0;
        rawCurrent = Math.max(0, Math.min(100, rawCurrent));
        updateGauge("currentValue", rawCurrent, "A", 100);

        // Temperatures (up to 12 sensors)
        const temps = data.temperatures || [];
        for (let i = 0; i < 12; i++) {
          const id = "temp" + (i + 1) + "Value";
          const t = temps[i];
          if (t === -127 || t === undefined) {
            updateGauge(id, "Off", "°C", 150);
          } else {
            updateGauge(id, Number(t), "°C", 150);
          }
        }

        // Capacitance (F -> mF)
        const capF = parseFloat(data.capacitanceF);
        renderCapacitance(capF);

        // Ready / Off LEDs
        const readyLed = document.getElementById("readyLed");
        const offLed = document.getElementById("offLed");
        if (readyLed)
          readyLed.style.backgroundColor = data.ready ? "limegreen" : "gray";
        if (offLed) offLed.style.backgroundColor = data.off ? "red" : "gray";

        // Fan slider reflect
        const fanSlider = document.getElementById("fanSlider");
        if (fanSlider && typeof data.fanSpeed === "number") {
          fanSlider.value = data.fanSpeed;
        }
      } catch (err) {
        console.error("Monitor error:", err);
      }
    }, intervalMs);
  }

  // ========================================================
  // ===============      AUTO LOGOUT HOOKS     =============
  // ========================================================

  function sendInstantLogout() {
    try {
      const payload = new Blob([JSON.stringify({ action: "disconnect" })], {
        type: "application/json",
      });
      navigator.sendBeacon("/disconnect", payload);
    } catch (e) {
      fetch("/disconnect", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ action: "disconnect" }),
      });
    }
    try {
      window.location.href = "http://powerboard.local/login";
    } catch (e) {
      // ignore navigation errors
    }
  }

  window.addEventListener("pagehide", sendInstantLogout);
  document.addEventListener("visibilitychange", () => {
    if (document.visibilityState === "hidden") {
      sendInstantLogout();
    }
  });

  // ========================================================
  // ===============        DOM READY INIT      =============
  // ========================================================

  document.addEventListener("DOMContentLoaded", () => {
    bindConfirmModal();

    renderAllOutputs("manualOutputs", true);
    renderAllOutputs("userAccessGrid", false);

    enableDragScroll("manualOutputs");
    enableDragScroll("userAccessGrid");

    initPowerButton();
    bindTimingUi();
    liveRender();
    scheduleLiveInterval();

    // Keep session alive with backend heartbeat
    startHeartbeat(4000);
    loadControls();
    bindSessionHistoryButton();
    updateSessionStatsUI(null);
    // Disconnect button
    const disconnectBtn = document.getElementById("disconnectBtn");
    if (disconnectBtn) {
      disconnectBtn.addEventListener("click", disconnectDevice);
    }

    // Relay toggle (manual)
    const relayToggle = document.getElementById("relayToggle");
    if (relayToggle) {
      relayToggle.addEventListener("change", async () => {
        await ensureManualTakeover("relay");
        sendControlCommand("set", "relay", relayToggle.checked);
      });
    }

    // Fan slider manual control
    const fanSlider = document.getElementById("fanSlider");
    if (fanSlider) {
      fanSlider.addEventListener("input", async () => {
        await ensureManualTakeover("fan");
        const speed = parseInt(fanSlider.value, 10) || 0;
        sendControlCommand("set", "fanSpeed", speed);
      });
    }

    // Ensure tooltips exist for all visible UI elements.
    applyTooltips(document);
  });

  // ========================================================
  // ===============      EXPORT GLOBAL API     =============
  // ========================================================

  window.switchTab = switchTab;
  window.toggleUserMenu = toggleUserMenu;
  window.toggleMode = toggleMode;
  window.toggleLT = toggleLT;
  window.toggleMute = toggleMute;
  window.handleOutputToggle = handleOutputToggle;
  window.updateOutputAccess = updateOutputAccess;
  window.openConfirm = openConfirm;
  window.saveUserSettings = saveUserSettings;
  window.resetUserSettings = resetUserSettings;
  window.saveAdminSettings = saveAdminSettings;
  window.resetAdminSettings = resetAdminSettings;
  window.saveDeviceAndNichrome = saveDeviceAndNichrome;
  window.resetDeviceAndNichrome = resetDeviceAndNichrome;
  window.startSystem = startSystem;
  window.shutdownSystem = shutdownSystem;
  window.forceCalibration = forceCalibration;
  window.resetSystem = resetSystem;
  window.rebootSystem = rebootSystem;
  window.loadControls = loadControls;
  window.openSessionHistory = openSessionHistory;
  window.closeSessionHistory = closeSessionHistory;
  window.updateSessionStatsUI = updateSessionStatsUI; // for backend to call later
})();
