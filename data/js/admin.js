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
      "Wire thermal time constant tau (seconds). Higher = slower temperature response.",
    wireKLoss:
      "Heat loss coefficient k (W/K). Higher = faster cooling toward ambient.",
    wireThermalC:
      "Thermal mass C (J/K). Higher = slower temperature rise per watt.",
    wireKp: "Wire PI proportional gain (Kp).",
    wireKi: "Wire PI integral gain (Ki).",
    floorKp: "Floor PI proportional gain (Kp).",
    floorKi: "Floor PI integral gain (Ki).",
    ntcBeta: "NTC beta constant (B value).",
    ntcR0: "NTC resistance at the reference temperature (Ohms).",
    ntcFixedRes: "Fixed divider resistor value (Ohms).",
    ntcMinC: "Minimum valid NTC temperature (degC).",
    ntcMaxC: "Maximum valid NTC temperature (degC).",
    ntcSamples: "ADC samples per NTC update (1..64).",
    ntcGateIndex: "Wire index physically tied to the NTC sensor.",
    ntcPressMv: "Button press threshold (mV) for the NTC analog input.",
    ntcReleaseMv: "Button release threshold (mV) for the NTC analog input.",
    ntcDebounceMs: "Button debounce time (ms) for the NTC analog input.",
    wireTestTargetC: "Target wire temperature for the NTC-attached wire test.",
    wireTestWireIndex: "Wire index to drive during the wire test (blank = NTC gate).",
    wireTestStartBtn: "Start PI-controlled wire test using the NTC wire.",
    wireTestStopBtn: "Stop the wire test immediately.",
    ntcCalRef: "Reference temperature for NTC calibration (blank = use heatsink).",
    ntcCalibrateBtn: "Calibrate NTC using the reference (or heatsink if blank).",
    calibSuggestRefresh: "Compute suggested thermal model and PI gains from last calibration data.",
    calibPersistBtn: "Persist suggested thermal/PI values and reload settings.",

    // Loop timing
    onTime: "Pulse ON duration (ms). Used in manual timing mode.",
    offTime: "Pulse OFF duration (ms). Used in manual timing mode.",
    loopModeSelect:
      "Loop mode. Advanced = grouped outputs; Sequential = one at a time; Mixed = sequential with preheat + keep-warm pulses.",
    mixPreheatMs:
      "Total preheat duration for mixed mode (all allowed wires get pulses).",
    mixPreheatOnMs:
      "Primary wire pulse length during mixed-mode preheat frames.",
    mixKeepMs:
      "Keep-warm pulse for non-primary wires inside each mixed-mode frame.",
    mixFrameMs:
      "Frame period that contains all serialized mixed-mode pulses.",
    timingModeSelect:
      "Timing UI mode. Normal uses Hot/Medium/Gentle presets; Advanced exposes manual Ton/Toff.",
    timingProfileSelect:
      "Preset heat profile (Hot/Medium/Gentle). Updates Ton/Toff automatically.",
    timingResolved: "Read-only preview of the currently resolved Ton/Toff.",

    // Calibration + nichrome
    calibrationBtn: "Open calibration tools and live temperature trace.",
    startNtcCalibBtn: "Start NTC calibration recording.",
    startModelCalibBtn: "Start temperature model calibration recording.",
    stopCalibBtn: "Stop recording and save calibration data.",
    calibLatestBtn: "Jump to the most recent part of the chart.",
    calibPauseBtn: "Pause/resume chart updates so you can inspect history.",
    calibHistoryBtn: "Load and view the full calibration history buffer.",
      calibClearBtn: "Clear calibration buffer and saved data.",
      calibrationInfoBtn: "Show the calibration help overlay.",
      wireOhmPerM: "Nichrome resistivity (Ohms per meter).",
      floorThicknessMm: "Floor/cover thickness above the wire (20-50 mm).",
      floorMaterial: "Floor material selection (wood, epoxy, concrete, slate, marble, granite).",
      floorMaxC: "Max allowed floor temperature (C, capped at 35).",
      nichromeFinalTempC:
      "Target final nichrome temperature for the current installation (degC).",
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

  function floorMaterialFromCode(code) {
    switch (code) {
      case 0:
        return "wood";
      case 1:
        return "epoxy";
      case 2:
        return "concrete";
      case 3:
        return "slate";
      case 4:
        return "marble";
      case 5:
        return "granite";
      default:
        return "wood";
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
    mixed: {
      hot: { onMs: 10, offMs: 5 },
      medium: { onMs: 10, offMs: 20 },
      gentle: { onMs: 10, offMs: 80 },
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

    if (loopMode === "sequential" || loopMode === "mixed") {
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

  function setMixedVisibility() {
    const isMixed = getLoopMode() === "mixed";
    const nodes = document.querySelectorAll(".mixed-only");
    nodes.forEach((el) => {
      el.style.display = isMixed ? "" : "none";
    });
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
        setMixedVisibility();
        if (getTimingMode() === "preset") {
          applyTimingPreset();
        } else {
          renderTimingResolved();
        }
      });
    }

    // Initial visibility (default: preset)
    setTimingVisibility();
    setMixedVisibility();
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

    const wireKLoss = getFloat("wireKLoss");
    if (
      wireKLoss !== undefined &&
      !approxEqual(wireKLoss, cur.wireKLoss, 0.001)
    ) {
      cmds.push(["set", "wireKLoss", wireKLoss]);
    }

    const wireThermalC = getFloat("wireThermalC");
    if (
      wireThermalC !== undefined &&
      !approxEqual(wireThermalC, cur.wireThermalC, 0.01)
    ) {
      cmds.push(["set", "wireThermalC", wireThermalC]);
    }

    const wireKp = getFloat("wireKp");
    if (wireKp !== undefined && !approxEqual(wireKp, cur.wireKp, 0.001)) {
      cmds.push(["set", "wireKp", wireKp]);
    }

    const wireKi = getFloat("wireKi");
    if (wireKi !== undefined && !approxEqual(wireKi, cur.wireKi, 0.001)) {
      cmds.push(["set", "wireKi", wireKi]);
    }

    const floorKp = getFloat("floorKp");
    if (floorKp !== undefined && !approxEqual(floorKp, cur.floorKp, 0.001)) {
      cmds.push(["set", "floorKp", floorKp]);
    }

    const floorKi = getFloat("floorKi");
    if (floorKi !== undefined && !approxEqual(floorKi, cur.floorKi, 0.001)) {
      cmds.push(["set", "floorKi", floorKi]);
    }

    // NTC / analog input settings
    const ntcBeta = getFloat("ntcBeta");
    if (ntcBeta !== undefined && !approxEqual(ntcBeta, cur.ntcBeta, 1)) {
      cmds.push(["set", "ntcBeta", ntcBeta]);
    }

    const ntcR0 = getFloat("ntcR0");
    if (ntcR0 !== undefined && !approxEqual(ntcR0, cur.ntcR0, 1)) {
      cmds.push(["set", "ntcR0", ntcR0]);
    }

    const ntcFixedRes = getFloat("ntcFixedRes");
    if (
      ntcFixedRes !== undefined &&
      !approxEqual(ntcFixedRes, cur.ntcFixedRes, 1)
    ) {
      cmds.push(["set", "ntcFixedRes", ntcFixedRes]);
    }

    const ntcMinC = getFloat("ntcMinC");
    if (ntcMinC !== undefined && !approxEqual(ntcMinC, cur.ntcMinC, 0.1)) {
      cmds.push(["set", "ntcMinC", ntcMinC]);
    }

    const ntcMaxC = getFloat("ntcMaxC");
    if (ntcMaxC !== undefined && !approxEqual(ntcMaxC, cur.ntcMaxC, 0.1)) {
      cmds.push(["set", "ntcMaxC", ntcMaxC]);
    }

    const ntcSamples = getInt("ntcSamples");
    if (ntcSamples !== undefined && ntcSamples !== cur.ntcSamples) {
      cmds.push(["set", "ntcSamples", ntcSamples]);
    }

    const ntcGateIndex = getInt("ntcGateIndex");
    if (ntcGateIndex !== undefined && ntcGateIndex !== cur.ntcGateIndex) {
      cmds.push(["set", "ntcGateIndex", ntcGateIndex]);
    }

    const ntcPressMv = getFloat("ntcPressMv");
    if (ntcPressMv !== undefined && !approxEqual(ntcPressMv, cur.ntcPressMv, 1)) {
      cmds.push(["set", "ntcPressMv", ntcPressMv]);
    }

    const ntcReleaseMv = getFloat("ntcReleaseMv");
    if (
      ntcReleaseMv !== undefined &&
      !approxEqual(ntcReleaseMv, cur.ntcReleaseMv, 1)
    ) {
      cmds.push(["set", "ntcReleaseMv", ntcReleaseMv]);
    }

    const ntcDebounceMs = getInt("ntcDebounceMs");
    if (ntcDebounceMs !== undefined && ntcDebounceMs !== cur.ntcDebounceMs) {
      cmds.push(["set", "ntcDebounceMs", ntcDebounceMs]);
    }

    const onTime = getInt("onTime");
    if (onTime !== undefined && onTime !== cur.onTime) {
      cmds.push(["set", "onTime", onTime]);
    }

    const offTime = getInt("offTime");
    if (offTime !== undefined && offTime !== cur.offTime) {
      cmds.push(["set", "offTime", offTime]);
    }

    const loopModeSelect = document.getElementById("loopModeSelect");
    if (loopModeSelect) {
      const modeVal = loopModeSelect.value || "advanced";
      if (modeVal !== cur.loopMode) {
        cmds.push(["set", "loopMode", modeVal]);
      }
    }

    const mixPreheatMs = getInt("mixPreheatMs");
    if (
      mixPreheatMs !== undefined &&
      mixPreheatMs !== cur.mixPreheatMs
    ) {
      cmds.push(["set", "mixPreheatMs", mixPreheatMs]);
    }

    const mixPreheatOnMs = getInt("mixPreheatOnMs");
    if (
      mixPreheatOnMs !== undefined &&
      mixPreheatOnMs !== cur.mixPreheatOnMs
    ) {
      cmds.push(["set", "mixPreheatOnMs", mixPreheatOnMs]);
    }

    const mixKeepMs = getInt("mixKeepMs");
    if (mixKeepMs !== undefined && mixKeepMs !== cur.mixKeepMs) {
      cmds.push(["set", "mixKeepMs", mixKeepMs]);
    }

    const mixFrameMs = getInt("mixFrameMs");
    if (mixFrameMs !== undefined && mixFrameMs !== cur.mixFrameMs) {
      cmds.push(["set", "mixFrameMs", mixFrameMs]);
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

    const floorThicknessMm = getFloat("floorThicknessMm");
    if (
      floorThicknessMm !== undefined &&
      !approxEqual(floorThicknessMm, cur.floorThicknessMm, 0.1)
    ) {
      cmds.push(["set", "floorThicknessMm", floorThicknessMm]);
    }

    const floorMaterialSelect = document.getElementById("floorMaterial");
    if (floorMaterialSelect) {
      const floorMaterial = floorMaterialSelect.value || "wood";
      const curFloorMaterial =
        cur.floorMaterial !== undefined
          ? cur.floorMaterial
          : floorMaterialFromCode(cur.floorMaterialCode);
      if (floorMaterial !== curFloorMaterial) {
        cmds.push(["set", "floorMaterial", floorMaterial]);
      }
    }

    const floorMaxC = getFloat("floorMaxC");
    if (floorMaxC !== undefined && !approxEqual(floorMaxC, cur.floorMaxC, 0.1)) {
      cmds.push(["set", "floorMaxC", floorMaxC]);
    }

    const nichromeFinalTempC = getFloat("nichromeFinalTempC");
    if (
      nichromeFinalTempC !== undefined &&
      !approxEqual(nichromeFinalTempC, cur.nichromeFinalTempC, 0.1)
    ) {
      cmds.push(["set", "nichromeFinalTempC", nichromeFinalTempC]);
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
    setField("wireKLoss", data.wireKLoss);
    setField("wireThermalC", data.wireThermalC);
    setField("wireKp", data.wireKp);
    setField("wireKi", data.wireKi);
    setField("floorKp", data.floorKp);
    setField("floorKi", data.floorKi);
    setField("ntcBeta", data.ntcBeta);
    setField("ntcR0", data.ntcR0);
    setField("ntcFixedRes", data.ntcFixedRes);
    setField("ntcMinC", data.ntcMinC);
    setField("ntcMaxC", data.ntcMaxC);
    setField("ntcSamples", data.ntcSamples);
    setField("ntcGateIndex", data.ntcGateIndex);
    setField("ntcPressMv", data.ntcPressMv);
    setField("ntcReleaseMv", data.ntcReleaseMv);
    setField("ntcDebounceMs", data.ntcDebounceMs);
    setField("onTime", data.onTime);
    setField("offTime", data.offTime);
    setField("mixPreheatMs", data.mixPreheatMs);
    setField("mixPreheatOnMs", data.mixPreheatOnMs);
    setField("mixKeepMs", data.mixKeepMs);
    setField("mixFrameMs", data.mixFrameMs);
    const loopModeSelect = document.getElementById("loopModeSelect");
    if (loopModeSelect && data.loopMode) {
      loopModeSelect.value = data.loopMode;
    }
    setMixedVisibility();
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
    if (data.wireOhmPerM !== undefined) {
      setField("wireOhmPerM", data.wireOhmPerM);
    }
    if (data.floorThicknessMm !== undefined) {
      setField("floorThicknessMm", data.floorThicknessMm);
    }
    if (data.floorMaterial !== undefined) {
      setField("floorMaterial", data.floorMaterial);
    } else if (data.floorMaterialCode !== undefined) {
      setField("floorMaterial", floorMaterialFromCode(data.floorMaterialCode));
    } else {
      setField("floorMaterial", "wood");
    }
    if (data.floorMaxC !== undefined) {
      setField("floorMaxC", data.floorMaxC);
    }
    if (data.nichromeFinalTempC !== undefined) {
      setField("nichromeFinalTempC", data.nichromeFinalTempC);
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
      setField("wireKLoss", data.wireKLoss);
      setField("wireThermalC", data.wireThermalC);
      setField("wireKp", data.wireKp);
      setField("wireKi", data.wireKi);
      setField("floorKp", data.floorKp);
      setField("floorKi", data.floorKi);
      setField("ntcBeta", data.ntcBeta);
      setField("ntcR0", data.ntcR0);
      setField("ntcFixedRes", data.ntcFixedRes);
      setField("ntcMinC", data.ntcMinC);
      setField("ntcMaxC", data.ntcMaxC);
      setField("ntcSamples", data.ntcSamples);
      setField("ntcGateIndex", data.ntcGateIndex);
      setField("ntcPressMv", data.ntcPressMv);
      setField("ntcReleaseMv", data.ntcReleaseMv);
      setField("ntcDebounceMs", data.ntcDebounceMs);
      setField("onTime", data.onTime);
      setField("offTime", data.offTime);
      setField("mixPreheatMs", data.mixPreheatMs);
      setField("mixPreheatOnMs", data.mixPreheatOnMs);
      setField("mixKeepMs", data.mixKeepMs);
      setField("mixFrameMs", data.mixFrameMs);
      if (data.deviceId !== undefined) setField("userDeviceId", data.deviceId);
      // Always clear credential fields on load to avoid autofill showing deviceId.
      setField("userCurrentPassword", "");
      setField("userNewPassword", "");
      if (data.wifiSSID !== undefined) setField("wifiSSID", data.wifiSSID);
      const fanSlider = document.getElementById("fanSlider");
      if (fanSlider && typeof data.fanSpeed === "number") {
        fanSlider.value = data.fanSpeed;
      }
      const loopModeSelect = document.getElementById("loopModeSelect");
      if (loopModeSelect && data.loopMode) {
        loopModeSelect.value = data.loopMode;
      }
      setMixedVisibility();
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
      if (data.wireOhmPerM !== undefined) {
        setField("wireOhmPerM", data.wireOhmPerM);
      }
      if (data.floorThicknessMm !== undefined) {
        setField("floorThicknessMm", data.floorThicknessMm);
      }
      if (data.floorMaterial !== undefined) {
        setField("floorMaterial", data.floorMaterial);
      } else if (data.floorMaterialCode !== undefined) {
        setField("floorMaterial", floorMaterialFromCode(data.floorMaterialCode));
      } else {
        setField("floorMaterial", "wood");
      }
      if (data.floorMaxC !== undefined) {
        setField("floorMaxC", data.floorMaxC);
      }
      if (data.nichromeFinalTempC !== undefined) {
        setField("nichromeFinalTempC", data.nichromeFinalTempC);
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
          updateGauge(id, "Off", "\u00B0C", 150);
        } else {
          updateGauge(id, Number(t), "\u00B0C", 150);
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

  // Calibration modal controls
  let calibPollTimer = null;
  let calibSamples = [];
  let calibViewMode = "live";
  let calibLastMeta = null;
  let wireTestPollTimer = null;
  let calibChartPaused = false;
  let calibChartDrag = {
    dragging: false,
    startX: 0,
    startScrollLeft: 0,
  };

  function setCalibText(id, value) {
    const el = document.getElementById(id);
    if (el) el.textContent = value;
  }

  function mountOverlayInsideUiContainer(id) {
    const el = document.getElementById(id);
    const stage = document.querySelector(".ui-container");
    if (!el || !stage) return;
    if (el.parentElement !== stage) stage.appendChild(el);
  }

  function mountAllOverlays() {
    mountOverlayInsideUiContainer("sessionHistoryModal");
    mountOverlayInsideUiContainer("calibrationModal");
    const shClose = document.querySelector(
      "#sessionHistoryModal .session-history-close"
    );
    if (shClose) shClose.textContent = "x";
  }

  function openCalibrationModal() {
    const m = document.getElementById("calibrationModal");
    if (!m) return;
    m.classList.add("show");
    calibChartPaused = false;
    const pauseBtn = document.getElementById("calibPauseBtn");
    if (pauseBtn) pauseBtn.textContent = "Pause";
    calibViewMode = "live";
    calibSamples = [];
    const historyBtn = document.getElementById("calibHistoryBtn");
    if (historyBtn) historyBtn.textContent = "Load History";
    setCalibrationInfoVisible(false);
    initCalibrationChartUi();
    startCalibrationPoll();
    startWireTestPoll();
    fetchCalibPiSuggest();
  }

  function closeCalibrationModal() {
    const m = document.getElementById("calibrationModal");
    if (m) m.classList.remove("show");
    calibChartPaused = false;
    setCalibrationInfoVisible(false);
    stopCalibrationPoll();
    stopWireTestPoll();
  }

  function bindCalibrationButton() {
    const btn = document.getElementById("calibrationBtn");
    if (btn) btn.addEventListener("click", openCalibrationModal);

    const infoBtn = document.getElementById("calibrationInfoBtn");
    if (infoBtn) {
      infoBtn.addEventListener("click", toggleCalibrationInfo);
    }
    const infoClose = document.getElementById("calibrationInfoCloseBtn");
    if (infoClose) {
      infoClose.addEventListener("click", () => setCalibrationInfoVisible(false));
    }
    const latestBtn = document.getElementById("calibLatestBtn");
    if (latestBtn) {
      latestBtn.addEventListener("click", () => scrollCalibToLatest());
    }
    const pauseBtn = document.getElementById("calibPauseBtn");
    if (pauseBtn) {
      pauseBtn.addEventListener("click", toggleCalibPause);
    }
    const refreshSug = document.getElementById("calibSuggestRefresh");
    if (refreshSug) {
      refreshSug.addEventListener("click", fetchCalibPiSuggest);
    }
    const persistSug = document.getElementById("calibPersistBtn");
    if (persistSug) {
      persistSug.addEventListener("click", saveCalibPiSuggest);
    }

    const startNtc = document.getElementById("startNtcCalibBtn");
    if (startNtc) {
      startNtc.addEventListener("click", () => startCalibration("ntc"));
    }

    const startModel = document.getElementById("startModelCalibBtn");
    if (startModel) {
      startModel.addEventListener("click", () => startCalibration("model"));
    }

    const stopBtn = document.getElementById("stopCalibBtn");
    if (stopBtn) {
      stopBtn.addEventListener("click", stopCalibration);
    }

    const wireStart = document.getElementById("wireTestStartBtn");
    if (wireStart) {
      wireStart.addEventListener("click", startWireTest);
    }

    const wireStop = document.getElementById("wireTestStopBtn");
    if (wireStop) {
      wireStop.addEventListener("click", stopWireTest);
    }

    const ntcCalBtn = document.getElementById("ntcCalibrateBtn");
    if (ntcCalBtn) {
      ntcCalBtn.addEventListener("click", ntcCalibrate);
    }

    const historyBtn = document.getElementById("calibHistoryBtn");
    if (historyBtn) {
      historyBtn.addEventListener("click", toggleCalibrationHistory);
    }

    const clearBtn = document.getElementById("calibClearBtn");
    if (clearBtn) {
      clearBtn.addEventListener("click", clearCalibrationData);
    }
  }

  document.addEventListener("keydown", (e) => {
    if (e.key === "Escape") {
      setCalibrationInfoVisible(false);
    }
  });

  function scrollCalibToLatest() {
    const wrap = document.getElementById("calibScrollWrap");
    if (!wrap) return;
    wrap.scrollLeft = wrap.scrollWidth;
  }

  function toggleCalibPause() {
    calibChartPaused = !calibChartPaused;
    const pauseBtn = document.getElementById("calibPauseBtn");
    if (pauseBtn) pauseBtn.textContent = calibChartPaused ? "Resume" : "Pause";
    if (!calibChartPaused) {
      // Catch up immediately after resuming.
      pollCalibrationOnce();
    }
  }

  function setCalibrationInfoVisible(show) {
    const pop = document.getElementById("calibrationInfoPopover");
    if (!pop) return;
    if (show) pop.classList.add("show");
    else pop.classList.remove("show");
    pop.setAttribute("aria-hidden", show ? "false" : "true");
  }

  function toggleCalibrationInfo() {
    const pop = document.getElementById("calibrationInfoPopover");
    if (!pop) return;
    const show = !pop.classList.contains("show");
    setCalibrationInfoVisible(show);
  }

  async function fetchCalibPiSuggest() {
    try {
      const res = await fetch("/calib_pi_suggest", { cache: "no-store" });
      if (!res.ok) return;
      const d = await res.json();
      const setTxt = (id, val, suffix = "") => {
        const el = document.getElementById(id);
        if (!el) return;
        if (val === undefined || val === null || Number.isNaN(val)) {
          el.textContent = "--";
        } else {
          el.textContent = `${Number(val).toFixed(3)}${suffix}`;
        }
      };
      setTxt("calibTauText", d.wire_tau, " ");
      setTxt("calibKText", d.wire_k_loss, " ");
      setTxt("calibCText", d.wire_c, " ");
      setTxt("calibPmaxText", d.max_power_w, " ");
      setTxt("calibWireKpSug", d.wire_kp_suggest, "");
      setTxt("calibWireKiSug", d.wire_ki_suggest, "");
      setTxt("calibFloorKpSug", d.floor_kp_suggest, "");
      setTxt("calibFloorKiSug", d.floor_ki_suggest, "");
      setTxt("calibWireKpCur", d.wire_kp_current, "");
      setTxt("calibWireKiCur", d.wire_ki_current, "");
      setTxt("calibFloorKpCur", d.floor_kp_current, "");
      setTxt("calibFloorKiCur", d.floor_ki_current, "");
      // Store for persist
      window.__calibPiSuggestion = d;
    } catch (err) {
      console.warn("PI suggest fetch failed", err);
    }
  }

  async function saveCalibPiSuggest() {
    const d = window.__calibPiSuggestion || {};
    const payload = {
      wire_tau: d.wire_tau,
      wire_k_loss: d.wire_k_loss,
      wire_c: d.wire_c,
      wire_kp: d.wire_kp_suggest,
      wire_ki: d.wire_ki_suggest,
      floor_kp: d.floor_kp_suggest,
      floor_ki: d.floor_ki_suggest,
    };
    try {
      const res = await fetch("/calib_pi_save", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(payload),
      });
      if (!res.ok) {
        openAlert("Calibration", "Persist failed.", "danger");
        return;
      }
      openAlert("Calibration", "Model & PI values saved. Reloading settings...", "success");
      await loadControls();
      fetchCalibPiSuggest();
    } catch (err) {
      console.error("Persist PI failed:", err);
      openAlert("Calibration", "Persist failed.", "danger");
    }
  }

  async function startCalibration(mode) {
    const payload = {
      mode,
      interval_ms: 500,
      max_samples: 1200,
    };

    try {
      const res = await fetch("/calib_start", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(payload),
      });
      if (!res.ok) {
        console.error("Calibration start failed:", res.status);
      }
      calibViewMode = "live";
      const historyBtn = document.getElementById("calibHistoryBtn");
      if (historyBtn) historyBtn.textContent = "Load History";
      await pollCalibrationOnce();
    } catch (err) {
      console.error("Calibration start error:", err);
    }
  }

  async function stopCalibration() {
    try {
      const res = await fetch("/calib_stop", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: "{}",
      });
      if (!res.ok) {
        console.error("Calibration stop failed:", res.status);
      }
      await pollCalibrationOnce();
    } catch (err) {
      console.error("Calibration stop error:", err);
    }
  }

  async function startWireTest() {
    const target = parseFloat(
      (document.getElementById("wireTestTargetC") || {}).value
    );
    const wireIdx = parseInt(
      (document.getElementById("wireTestWireIndex") || {}).value,
      10
    );

    if (!isFinite(target) || target <= 0) {
      openAlert("Wire Test", "Enter a valid target temperature.", "warning");
      return;
    }

    const payload = { target_c: target };
    if (isFinite(wireIdx) && wireIdx > 0) {
      payload.wire_index = wireIdx;
    }

    try {
      const res = await fetch("/wire_test_start", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(payload),
      });
      if (!res.ok) {
        openAlert("Wire Test", "Start failed.", "danger");
      } else {
        openAlert("Wire Test", "Started.", "success");
      }
      await pollWireTestOnce();
    } catch (err) {
      console.error("Wire test start error:", err);
    }
  }

  async function stopWireTest() {
    try {
      await fetch("/wire_test_stop", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: "{}",
      });
      await pollWireTestOnce();
    } catch (err) {
      console.error("Wire test stop error:", err);
    }
  }

  async function clearCalibrationData() {
    try {
      const res = await fetch("/calib_clear", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: "{}",
      });
      if (!res.ok) {
        console.error("Calibration clear failed:", res.status);
      }
      calibSamples = [];
      calibViewMode = "live";
      const historyBtn = document.getElementById("calibHistoryBtn");
      if (historyBtn) historyBtn.textContent = "Load History";
      renderCalibrationChart();
      await pollCalibrationOnce();
    } catch (err) {
      console.error("Calibration clear error:", err);
    }
  }

  async function pollWireTestOnce() {
    try {
      const res = await fetch("/wire_test_status", { cache: "no-store" });
      if (!res.ok) return;
      const data = await res.json();

      const running = !!data.running;
      const stateEl = document.getElementById("wireTestState");
      if (stateEl) stateEl.textContent = running ? "Running" : "Idle";

      const tempEl = document.getElementById("wireTestTemp");
      if (tempEl) {
        tempEl.textContent = isFinite(data.temp_c)
          ? `${data.temp_c.toFixed(1)} C`
          : "--";
      }

      const dutyEl = document.getElementById("wireTestDuty");
      if (dutyEl) {
        dutyEl.textContent = `${Math.round((data.duty || 0) * 100)}%`;
      }

      const onOffEl = document.getElementById("wireTestOnOff");
      if (onOffEl) {
        if (data.on_ms != null && data.off_ms != null) {
          onOffEl.textContent = `${data.on_ms} / ${data.off_ms} ms`;
        } else {
          onOffEl.textContent = "--";
        }
      }

      const tgtInput = document.getElementById("wireTestTargetC");
      if (tgtInput && isFinite(data.target_c)) {
        tgtInput.value = data.target_c;
      }
      const wireInput = document.getElementById("wireTestWireIndex");
      if (wireInput && data.wire_index) {
        wireInput.value = data.wire_index;
      }

      const startBtn = document.getElementById("wireTestStartBtn");
      const stopBtn = document.getElementById("wireTestStopBtn");
      if (startBtn) startBtn.disabled = running;
      if (stopBtn) stopBtn.disabled = !running;
    } catch (err) {
      console.warn("Wire test status error:", err);
    }
  }

  function startWireTestPoll() {
    stopWireTestPoll();
    pollWireTestOnce();
    wireTestPollTimer = setInterval(pollWireTestOnce, 1000);
  }

  function stopWireTestPoll() {
    if (wireTestPollTimer) {
      clearInterval(wireTestPollTimer);
      wireTestPollTimer = null;
    }
  }

  async function ntcCalibrate() {
    const refVal = (document.getElementById("ntcCalRef") || {}).value;
    const payload = {};
    if (refVal != null && String(refVal).trim().length > 0) {
      const ref = parseFloat(refVal);
      if (!isFinite(ref)) {
        openAlert("NTC Calibrate", "Enter a valid reference temperature.", "warning");
        return;
      }
      payload.ref_temp_c = ref;
    }

    const statusEl = document.getElementById("ntcCalibrateStatus");
    if (statusEl) statusEl.textContent = "Running...";

    try {
      const res = await fetch("/ntc_calibrate", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(payload),
      });
      const data = res.ok ? await res.json() : {};
      if (!res.ok || data.error) {
        openAlert("NTC Calibrate", data.error || "Failed.", "danger");
        if (statusEl) statusEl.textContent = "Failed";
      } else {
        openAlert("NTC Calibrate", "Calibration done.", "success");
        if (statusEl) {
          statusEl.textContent = `R0=${(data.r0_ohm || 0).toFixed(1)} ohm @ ${data.ref_c} C`;
        }
      }
    } catch (err) {
      console.error("NTC calibrate error:", err);
      if (statusEl) statusEl.textContent = "Failed";
    }
  }

  async function fetchCalibrationStatus() {
    try {
      const res = await fetch("/calib_status", { cache: "no-store" });
      if (!res.ok) return null;
      return await res.json();
    } catch (err) {
      console.warn("Calibration status error:", err);
      return null;
    }
  }

  async function fetchCalibrationPage(offset, count) {
    const url = `/calib_data?offset=${offset}&count=${count}`;
    const res = await fetch(url, { cache: "no-store" });
    if (!res.ok) return null;
    return await res.json();
  }

  async function loadCalibrationHistory() {
    const meta = calibLastMeta || (await fetchCalibrationStatus());
    if (!meta || !meta.count) {
      calibSamples = [];
      renderCalibrationChart();
      return;
    }

    const total = meta.count || 0;
    const all = [];
    let offset = 0;
    const pageSize = 200;

    while (offset < total) {
      const page = await fetchCalibrationPage(offset, pageSize);
      if (!page || !page.samples) break;
      all.push(...page.samples);
      offset += page.samples.length;
      if (page.samples.length < pageSize) break;
    }

    calibSamples = all;
    renderCalibrationChart();
    scrollCalibToLatest();

    const last = [...calibSamples].reverse().find((s) => isFinite(s.temp_c));
    if (last && isFinite(last.temp_c)) {
      setCalibText("calibTempText", `${last.temp_c.toFixed(1)} C`);
    }
  }

  async function toggleCalibrationHistory() {
    const btn = document.getElementById("calibHistoryBtn");
    if (calibViewMode === "history") {
      calibViewMode = "live";
      if (btn) btn.textContent = "Load History";
      await pollCalibrationOnce();
      return;
    }

    calibViewMode = "history";
    if (btn) btn.textContent = "Resume Live";
    await loadCalibrationHistory();
  }

  async function pollCalibrationOnce() {
    const meta = await fetchCalibrationStatus();
    if (!meta) return;

    calibLastMeta = meta;
    setCalibText("calibStatusText", meta.running ? "Running" : "Idle");
    setCalibText("calibModeText", meta.mode || "--");
    setCalibText("calibCountText", meta.count != null ? String(meta.count) : "0");
    setCalibText(
      "calibIntervalText",
      meta.interval_ms ? `${meta.interval_ms} ms` : "--"
    );

    if (calibViewMode === "history") return;
    if (calibChartPaused) return;

    const count = meta.count || 0;
    if (!count) {
      calibSamples = [];
      renderCalibrationChart();
      setCalibText("calibTempText", "--");
      return;
    }

    const page = await fetchCalibrationPage(Math.max(0, count - 200), 200);
    if (page && page.samples) {
      calibSamples = page.samples;
      renderCalibrationChart();

      const last = [...calibSamples].reverse().find((s) => isFinite(s.temp_c));
      if (last && isFinite(last.temp_c)) {
        setCalibText("calibTempText", `${last.temp_c.toFixed(1)} C`);
      } else {
        setCalibText("calibTempText", "--");
      }
    }
  }

  function startCalibrationPoll() {
    stopCalibrationPoll();
    pollCalibrationOnce();
    calibPollTimer = setInterval(pollCalibrationOnce, 1000);
  }

  function stopCalibrationPoll() {
    if (calibPollTimer) {
      clearInterval(calibPollTimer);
      calibPollTimer = null;
    }
  }

  function initCalibrationChartUi() {
    drawCalibYAxis();
    bindCalibChartDrag();
  }

  function clamp(v, lo, hi) {
    return Math.max(lo, Math.min(hi, v));
  }

  function fmtUptime(ms) {
    const total = Math.max(0, Math.floor((ms || 0) / 1000));
    const hh = Math.floor(total / 3600);
    const mm = Math.floor((total % 3600) / 60);
    const ss = total % 60;
    if (hh > 0) return `${hh}:${String(mm).padStart(2, "0")}:${String(ss).padStart(2, "0")}`;
    return `${mm}:${String(ss).padStart(2, "0")}`;
  }

  const CALIB_T_MIN = 0;
  const CALIB_T_MAX = 150;
  const CALIB_Y_TICK_STEP = 10;
  const CALIB_H = 220;
  const CALIB_PLOT_PAD_LEFT = 10;
  const CALIB_Y1 = 18;
  const CALIB_Y0 = CALIB_H - 46;
  const CALIB_RIGHT_PAD = 24;
  const CALIB_DX = 10;
  const CALIB_TICK_EVERY_SECONDS = 5;

  function yFromTemp(tC) {
    const t = clamp(Number(tC), CALIB_T_MIN, CALIB_T_MAX);
    return CALIB_Y0 - ((t - CALIB_T_MIN) * (CALIB_Y0 - CALIB_Y1)) / (CALIB_T_MAX - CALIB_T_MIN);
  }

  function tag(x, y, text) {
    const padX = 6;
    const charW = 7;
    const w = Math.max(34, String(text).length * charW + padX * 2);
    const h = 20;
    return `
      <g>
        <rect class="tag" x="${x}" y="${y - h / 2}" width="${w}" height="${h}" rx="9"></rect>
        <text class="tagText" x="${x + padX}" y="${y + 4}">${text}</text>
      </g>
    `;
  }

  function drawCalibYAxis() {
    const yAxisSvg = document.getElementById("calibYAxis");
    if (!yAxisSvg) return;

    const W = 72;
    yAxisSvg.setAttribute("width", W);
    yAxisSvg.setAttribute("height", CALIB_H);
    yAxisSvg.setAttribute("viewBox", `0 0 ${W} ${CALIB_H}`);

    let s = `
      <line class="axisLine" x1="${W - 1}" y1="${CALIB_Y1}" x2="${W - 1}" y2="${CALIB_Y0}"></line>
      <text class="label" x="16" y="${(CALIB_Y1 + CALIB_Y0) / 2}" transform="rotate(-90 16 ${(CALIB_Y1 + CALIB_Y0) / 2})">C</text>
    `;

    for (let t = CALIB_T_MIN; t <= CALIB_T_MAX; t += CALIB_Y_TICK_STEP) {
      const y = yFromTemp(t);
      s += `<line class="yTick" x1="${W - 8}" y1="${y}" x2="${W - 1}" y2="${y}"></line>`;
      s += `<text class="yLabel" x="${W - 12}" y="${y + 4}" text-anchor="end">${t}</text>`;
    }
    yAxisSvg.innerHTML = s;
  }

  function buildCalibGrid(xMax, intervalMs, pointCount) {
    let s = `<g class="grid">`;
    for (let t = CALIB_T_MIN; t <= CALIB_T_MAX; t += CALIB_Y_TICK_STEP) {
      const y = yFromTemp(t);
      s += `<line class="gridLine" x1="0" y1="${y}" x2="${xMax}" y2="${y}"></line>`;
    }
    const vEverySamples = Math.max(1, Math.round(1000 / Math.max(50, intervalMs || 500)));
    for (let i = 0; i < pointCount; i += vEverySamples) {
      const x = CALIB_PLOT_PAD_LEFT + i * CALIB_DX;
      s += `<line x1="${x}" y1="${CALIB_Y1}" x2="${x}" y2="${CALIB_Y0}"></line>`;
    }
    s += `</g>`;
    return s;
  }

  function buildCalibXAxis(xMax) {
    return `
      <line class="xAxis" x1="0" y1="${CALIB_Y0}" x2="${xMax}" y2="${CALIB_Y0}"></line>
      <text class="label" x="${Math.max(40, xMax / 2 - 20)}" y="${CALIB_H - 10}">Time</text>
    `;
  }

  function buildCalibTimeTicks(samples, intervalMs, startMs) {
    let s = `<g>`;
    const tickEverySamples = Math.max(
      1,
      Math.round((CALIB_TICK_EVERY_SECONDS * 1000) / Math.max(50, intervalMs || 500))
    );
    for (let i = 0; i < samples.length; i += tickEverySamples) {
      const x = CALIB_PLOT_PAD_LEFT + i * CALIB_DX;
      const tAbs = (startMs || 0) + (samples[i]?.t_ms || 0);
      const label = fmtUptime(tAbs);
      s += `<line class="tick" x1="${x}" y1="${CALIB_Y0}" x2="${x}" y2="${CALIB_Y0 + 6}"></line>`;
      s += `<text class="subtext" x="${x - 18}" y="${CALIB_Y0 + 22}">${label}</text>`;
    }
    s += `</g>`;
    return s;
  }

  function buildCalibPolyline(samples) {
    if (samples.length === 0) return "";
    const pts = samples
      .map((p, i) => `${CALIB_PLOT_PAD_LEFT + i * CALIB_DX},${yFromTemp(p.temp_c)}`)
      .join(" ");
    return `<polyline class="temp-line" points="${pts}"></polyline>`;
  }

  function buildCalibLatestMarker(samples, xMax, startMs) {
    if (samples.length === 0) return "";
    const i = samples.length - 1;
    const p = samples[i];
    const x = CALIB_PLOT_PAD_LEFT + i * CALIB_DX;
    const y = yFromTemp(p.temp_c);

    const timeLabel = fmtUptime((startMs || 0) + (p.t_ms || 0));
    const tempLabel = `${Number(p.temp_c).toFixed(1)}C`;
    const timeTagX = clamp(x + 8, 8, xMax - 110);

    return `
      <g>
        <line class="crosshair" x1="${x}" y1="${CALIB_Y1}" x2="${x}" y2="${CALIB_Y0}"></line>
        <line class="crosshair" x1="0" y1="${y}" x2="${xMax}" y2="${y}"></line>
        <circle class="end-dot" cx="${x}" cy="${y}" r="6"></circle>
        ${tag(timeTagX, CALIB_Y0 + 26, timeLabel)}
        ${tag(8, clamp(y, CALIB_Y1 + 12, CALIB_Y0 - 12), tempLabel)}
      </g>
    `;
  }

  function renderCalibrationChart() {
    const plotSvg = document.getElementById("calibPlot");
    const scrollWrap = document.getElementById("calibScrollWrap");
    if (!plotSvg || !scrollWrap) return;

    drawCalibYAxis();

    const samples = (calibSamples || []).filter(
      (s) => isFinite(s.temp_c) && isFinite(s.t_ms)
    );

    if (!samples.length) {
      plotSvg.setAttribute("width", 600);
      plotSvg.setAttribute("height", CALIB_H);
      plotSvg.setAttribute("viewBox", `0 0 600 ${CALIB_H}`);
      plotSvg.innerHTML = `<text class="subtext" x="8" y="18">No calibration data yet.</text>`;
      const tEl = document.getElementById("calibNowTempPill");
      const tsEl = document.getElementById("calibNowTimePill");
      if (tEl) tEl.textContent = "--";
      if (tsEl) tsEl.textContent = "--:--";
      return;
    }

    const intervalMs =
      (calibLastMeta && calibLastMeta.interval_ms) ||
      (calibLastMeta && calibLastMeta.intervalMs) ||
      500;
    const startMs =
      (calibLastMeta && (calibLastMeta.start_ms ?? calibLastMeta.startMs)) || 0;

    const xMax =
      CALIB_PLOT_PAD_LEFT + Math.max(1, samples.length - 1) * CALIB_DX + CALIB_RIGHT_PAD;
    const nearEnd =
      scrollWrap.scrollLeft + scrollWrap.clientWidth >= scrollWrap.scrollWidth - 30;

    plotSvg.setAttribute("width", xMax);
    plotSvg.setAttribute("height", CALIB_H);
    plotSvg.setAttribute("viewBox", `0 0 ${xMax} ${CALIB_H}`);
    plotSvg.innerHTML = `
      ${buildCalibGrid(xMax, intervalMs, samples.length)}
      ${buildCalibXAxis(xMax)}
      ${buildCalibTimeTicks(samples, intervalMs, startMs)}
      <text class="subtext" x="8" y="18">Latest point: dot + dotted guides</text>
      ${buildCalibPolyline(samples)}
      ${buildCalibLatestMarker(samples, xMax, startMs)}
    `;

    const last = samples[samples.length - 1];
    const tEl = document.getElementById("calibNowTempPill");
    const tsEl = document.getElementById("calibNowTimePill");
    if (tEl) tEl.textContent = Number(last.temp_c).toFixed(1);
    if (tsEl) tsEl.textContent = fmtUptime(startMs + (last.t_ms || 0));

    if (!calibChartPaused && nearEnd) {
      scrollCalibToLatest();
    }
  }

  function bindCalibChartDrag() {
    const scrollWrap = document.getElementById("calibScrollWrap");
    if (!scrollWrap || scrollWrap.__dragBound) return;
    scrollWrap.__dragBound = true;

    const dragStart = (clientX) => {
      calibChartDrag.dragging = true;
      scrollWrap.classList.add("dragging");
      calibChartDrag.startX = clientX;
      calibChartDrag.startScrollLeft = scrollWrap.scrollLeft;
    };
    const dragMove = (clientX) => {
      if (!calibChartDrag.dragging) return;
      const dx = clientX - calibChartDrag.startX;
      scrollWrap.scrollLeft = calibChartDrag.startScrollLeft - dx;
    };
    const dragEnd = () => {
      calibChartDrag.dragging = false;
      scrollWrap.classList.remove("dragging");
    };

    scrollWrap.addEventListener("mousedown", (e) => {
      if (e.button !== 0) return;
      dragStart(e.clientX);
    });
    window.addEventListener("mousemove", (e) => dragMove(e.clientX));
    window.addEventListener("mouseup", dragEnd);

    scrollWrap.addEventListener(
      "touchstart",
      (e) => {
        if (!e.touches || e.touches.length !== 1) return;
        dragStart(e.touches[0].clientX);
      },
      { passive: true }
    );
    scrollWrap.addEventListener(
      "touchmove",
      (e) => {
        if (!e.touches || e.touches.length !== 1) return;
        dragMove(e.touches[0].clientX);
      },
      { passive: true }
    );
    scrollWrap.addEventListener("touchend", dragEnd);
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
            updateGauge(id, "Off", "\u00B0C", 150);
          } else {
            updateGauge(id, Number(t), "\u00B0C", 150);
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
    mountAllOverlays();
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
    bindCalibrationButton();
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
  window.openCalibrationModal = openCalibrationModal;
  window.closeCalibrationModal = closeCalibrationModal;
  window.updateSessionStatsUI = updateSessionStatsUI; // for backend to call later
})();
