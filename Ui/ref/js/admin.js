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
    const targetTab = tabs[index];
    if (targetTab && targetTab.classList.contains("tab-disabled")) {
      return;
    }
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
  let pendingConfirmAction = null;
  let lastLoadedControls = null; // Snapshot from /load_controls
  let lastSetupStatus = null; // Snapshot from /setup_status
  let lastMonitor = null; // Snapshot from /monitor
  let isMuted = false; // Buzzer mute cached state
  let stateStream = null; // EventSource for zero-lag state
  let eventStream = null; // EventSource for warnings/errors
  let statePollTimer = null; // Fallback polling timer
  let monitorPollTimer = null; // /monitor polling timer (UI snapshot)
  let heartbeatTimer = null; // /heartbeat keepalive timer
  let dashboardClockTimer = null; // dashboard clock updater timer
  let calibrationBusy = false; // prevent overlapping manual calibrations
  let setupRunAllowed = true;
  let setupConfigOk = true;
  let setupCalibOk = true;
  let wizardActive = false;
  let wizardStepIndex = 0;
  const WIZARD_SKIP_DEFAULTS = {
    wire: false,
    floor: false,
    wifi: false,
    cred: false,
    presence: false,
  };
  let wizardSkipped = { ...WIZARD_SKIP_DEFAULTS };
  const WIZARD_SKIP_STORAGE_KEY = "pbWizardSkipped";
  const wizardNodeCache = [];
  let ntcCalPollTimer = null;
  let ntcCalPollIntervalMs = 1000;
  let liveControlSamples = [];
  let liveControlStartPerf = null;
  let liveControlChartPaused = false;
  let liveControlMaxSamples = 1200;
  let liveControlLastIntervalMs = 1000;
  let liveControlModalOpen = false;
  let testModeState = { active: false, label: "--", targetC: NaN };
  let ambientWaitState = { active: false, label: "--", tolC: NaN, sinceMs: 0 };
  let lastWireTestStatus = null;
  let lastEventUnread = { warn: 0, error: 0 };
  let lastEventToastKind = null;
  let eventToastVisible = false;
  let authFailCount = 0;
  const AUTH_FAIL_THRESHOLD = 3;
  let liveControlDrag = {
    dragging: false,
    startX: 0,
    startScrollLeft: 0,
  };
  let wiresCoolPromptTimer = null;

  const CBOR_MIME = "application/cbor";

  function cborHeaders(extra) {
    if (window.pbCborHeaders) return window.pbCborHeaders(extra);
    const headers = new Headers(extra || {});
    if (!headers.has("Content-Type")) headers.set("Content-Type", CBOR_MIME);
    if (!headers.has("Accept")) headers.set("Accept", CBOR_MIME);
    return headers;
  }

  function encodeCbor(payload) {
    if (window.pbEncodeCbor) return window.pbEncodeCbor(payload);
    return new Uint8Array(0);
  }

  function encodeCborBase64(payload) {
    if (window.pbEncodeCborBase64) return window.pbEncodeCborBase64(payload);
    return "";
  }

  function decodeCborBase64(raw) {
    if (!raw) return null;
    if (window.pbDecodeCborBase64) return window.pbDecodeCborBase64(raw);
    return null;
  }

  async function readCbor(res, fallback) {
    if (!res) return fallback;
    try {
      if (window.pbReadCbor) {
        const data = await window.pbReadCbor(res);
        return data == null ? fallback : data;
      }
      if (window.pbDecodeCbor) {
        const buf = await res.arrayBuffer();
        if (!buf || !buf.byteLength) return fallback;
        const data = window.pbDecodeCbor(buf);
        return data == null ? fallback : data;
      }
    } catch (e) {
      return fallback;
    }
    return fallback;
  }

  function decodeSsePayload(ev) {
    const raw = ev && typeof ev.data === "string" ? ev.data : "";
    if (!raw) return null;
    return decodeCborBase64(raw);
  }

  function loadWizardSkipped() {
    try {
      const raw = localStorage.getItem(WIZARD_SKIP_STORAGE_KEY);
      if (!raw) return;
      const data = decodeCborBase64(raw);
      if (!data || typeof data !== "object") {
        localStorage.removeItem(WIZARD_SKIP_STORAGE_KEY);
        return;
      }
      wizardSkipped = { ...wizardSkipped, ...data };
    } catch (e) {
      // ignore storage errors
    }
  }

  function saveWizardSkipped() {
    try {
      const encoded = encodeCborBase64(wizardSkipped);
      if (!encoded) {
        localStorage.removeItem(WIZARD_SKIP_STORAGE_KEY);
        return;
      }
      localStorage.setItem(WIZARD_SKIP_STORAGE_KEY, encoded);
    } catch (e) {
      // ignore storage errors
    }
  }

  function resetWizardSkipped() {
    wizardSkipped = { ...WIZARD_SKIP_DEFAULTS };
    saveWizardSkipped();
  }

  function setWizardSkipped(key, value) {
    if (!key) return;
    const next = !!value;
    if (wizardSkipped[key] === next) return;
    wizardSkipped[key] = next;
    saveWizardSkipped();
  }

  loadWizardSkipped();

  function redirectToLogin() {
    if (window.pbClearToken) window.pbClearToken();
    if (window.pbLoginUrl) {
      window.location.href = window.pbLoginUrl();
    } else {
      window.location.href = "login.html";
    }
  }

  function noteAuthFailure() {
    authFailCount += 1;
    if (authFailCount >= AUTH_FAIL_THRESHOLD) {
      redirectToLogin();
    }
  }

  function resetAuthFailures() {
    authFailCount = 0;
  }

  if (window.pbGetToken && !window.pbGetToken()) {
    redirectToLogin();
    return;
  }

  // ========================================================
  // ===============          TOOLTIPS          =============
  // ========================================================

  const TOOLTIP_BY_ID = {
    // Dashboard / global controls
    ltToggle:
      "LT (LED feedback). When enabled, output LEDs mirror output states during the loop.",
    muteBtn: "Mute/unmute buzzer sounds.",
    powerButton:
      "Start the device (Idle prep -> RUN). Press again to power off.",
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
    relayToggle: "Input relay control.",

    // Device settings (Sampling & Power)
    acFrequency:
      "Sampling rate for current/telemetry (Hz). Higher = faster updates (50..500).",
    chargeResistor:
      "Bleed/charge resistor value between capacitor bank negative and system GND (Ohms).",
    currLimit:
      "Over-current trip threshold (A). Trips if current stays above this for a short time window.",
    currentSource:
      "Select ACS sensor or CSP discharge estimate for current calculations.",

    // Thermal safety
    tempWarnC:
      "Temperature warning threshold (degC). Shows a warning overlay before shutdown.",
    tempTripC:
      "Temperature trip threshold (degC). Triggers shutdown and Error when exceeded.",

    wireTestTargetC:
      "Target temperature used by the energy loop (wire test + model calibration).",
    wireTestStartBtn: "Start a loop test across all allowed outputs.",
    wireTestStopBtn: "Stop the wire test immediately.",
    calibWireText: "Wire index used for calibration.",
    calibTargetText: "Target temperature used by model calibration.",
    calibElapsedText: "Elapsed time for the current calibration recording.",

    // Calibration + nichrome
    calibrationBtn: "Open calibration tools and live temperature trace.",
    liveControlBtn: "Open the live control chart (temps + setpoint).",
    errorBtn: "Show the last stop/error details.",
    logBtn: "Open device log (latest debug output).",
    logRefreshBtn: "Reload log file from the device.",
    logClearBtn: "Clear the saved log file.",
    startModelCalibBtn: "Start temperature model calibration recording.",
    stopCalibBtn: "Stop recording and save calibration data.",
    calibLatestBtn: "Jump to the most recent part of the chart.",
    calibPauseBtn: "Pause/resume chart updates so you can inspect history.",
    calibHistoryBtn: "Load and view the in-memory calibration history buffer.",
    calibClearBtn: "Clear calibration buffer and saved data.",
    calibrationInfoBtn: "Show the calibration help overlay.",
    calibHistorySelect: "Pick a saved calibration history file.",
    calibHistoryLoadBtn: "Load the selected saved history into the chart.",
    calibHistoryRefreshBtn: "Refresh the saved history list.",
    liveControlLatestBtn: "Jump to the latest point in the live control chart.",
    liveControlPauseBtn: "Pause/resume live chart updates.",
    liveControlClearBtn: "Clear the live control chart history.",
    testModeBadge: "Test mode is active (wire test or calibration heating).",
    ambientWaitBadge:
      "Cooling to ambient before running or model calibration.",
    topStopTestBtn: "Stop the active test or calibration run.",
    topCalibBtn: "Open the calibration tools.",
    topLiveControlBtn: "Open the live control chart (temps + setpoint).",
    wireOhmPerM: "Nichrome resistivity (Ohms per meter).",
    floorThicknessMm: "Floor/cover thickness above the wire (20-50 mm).",
    floorMaterial:
      "Floor material selection (wood, epoxy, concrete, slate, marble, granite).",
    floorMaxC: "Max allowed floor temperature (C, capped at 35).",
    nichromeFinalTempC:
      "Target final nichrome temperature for the current installation (degC).",
    ntcGateIndex:
      "Wire channel tied to the NTC sensor (used for wire test and model calibration).",
    floorSwitchMarginC:
      "Floor temp margin used to switch from boost to equilibrium control.",
    modelParamTarget: "Select a wire or floor model to view/edit tau, k, C.",
    modelTau: "Thermal time constant (seconds) for the selected model.",
    modelK: "Heat loss coefficient (W/K) for the selected model.",
    modelC: "Thermal capacitance (J/K) for the selected model.",
    ntcBeta: "NTC beta constant (K).",
    ntcT0C: "Reference temperature T0 (degC).",
    ntcR0: "NTC resistance at T0 (ohms).",
    ntcFixedRes: "Fixed pull-up resistor value (ohms).",
    presenceMinDropV:
      "Minimum voltage drop (V) required to mark a wire as present.",

    // Admin settings
    adminCurrentPassword:
      "Current admin password (required to change settings).",
    adminUsername: "New admin username.",
    adminPassword: "New admin password.",
    wifiSSID: "WiFi station SSID to connect to.",
    wifiPassword: "WiFi station password.",
    apSSID: "Access point name (SSID).",
    apPassword: "Access point password.",

    // User settings
    userCurrentPassword:
      "Current user password (required to change user settings).",
    userNewPassword: "New user password to set.",
    userDeviceId: "Change the displayed device ID/label.",

    // Setup wizard
    setupStageInput: "Setup wizard stage index (admin only).",
    setupSubstageInput: "Setup wizard substage index (admin only).",
    setupWireIndexInput: "Wire index currently being calibrated.",
    setupUpdateBtn: "Update setup stage/substage/wire index.",
    setupDoneBtn: "Mark setup as complete (requires config + calibration).",
    setupClearDoneBtn: "Clear the setup-done flag.",
    setupRefreshBtn: "Refresh setup wizard status from device.",
    setupResetBtn: "Reset setup and calibration flags.",
    setupResetClearModels: "Clear wire and floor model parameters on reset.",
    setupResetClearWire: "Clear wire model parameters on reset.",
    setupResetClearFloor: "Clear floor model parameters on reset.",
    setupBannerOpenBtn: "Jump to the setup wizard tab.",
    setupBannerCalibBtn: "Open calibration tools.",

    // Confirm wires cool
    wiresCoolConfirmBtn: "Confirm wires are cool before RUN.",

    // Calibration inputs
    ntcCalTargetC: "NTC calibration target temperature (optional).",
    ntcCalSampleMs: "NTC calibration sample interval (ms).",
    ntcCalTimeoutMs: "NTC calibration timeout (ms).",
    ntcCalStartBtn: "Start NTC multi-point calibration.",
    ntcCalStopBtn: "Stop NTC calibration.",
    ntcBetaRefC: "Reference temperature for beta calibration.",
    ntcBetaCalBtn: "Run single-point beta calibration.",
    floorCalTargetC: "Target floor temperature for floor calibration.",
    floorCalDutyPct: "Duty cycle used during floor calibration.",
    floorCalAmbientMin: "Ambient stabilization time (minutes).",
    floorCalHeatMin: "Heating time (minutes).",
    floorCalCoolMin: "Cool-down time (minutes).",
    floorCalIntervalMs: "Sampling interval for floor calibration (ms).",
    floorCalWireIndex: "Wire index used for floor calibration.",
    startFloorCalibBtn: "Start floor model calibration capture.",
    presenceProbeBtn: "Run wire presence probe.",
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
    const userIcon = root.querySelector
      ? root.querySelector(".user-icon")
      : null;
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
      const value = card.querySelector
        ? card.querySelector(".gauge-value")
        : null;
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

  // ========================================================
  // ===============       DASHBOARD CLOCK      =============
  // ========================================================

  function initDashboardClock() {
    const hourHand = document.getElementById("dashboardClockHourHand");
    const minuteHand = document.getElementById("dashboardClockMinuteHand");
    const secondHand = document.getElementById("dashboardClockSecondHand");
    const dayEl = document.getElementById("dashboardClockDay");
    const dateEl = document.getElementById("dashboardClockDate");
    const timeEl = document.getElementById("dashboardClockTime");

    if (!hourHand || !minuteHand || !secondHand) return;

    const pad2 = (n) => String(n).padStart(2, "0");
    const cX = 60;
    const cY = 60;

    function render() {
      const now = new Date();
      const seconds = now.getSeconds() + now.getMilliseconds() / 1000;
      const minutes = now.getMinutes() + seconds / 60;
      const hours = (now.getHours() % 12) + minutes / 60;

      hourHand.setAttribute("transform", `rotate(${hours * 30} ${cX} ${cY})`);
      minuteHand.setAttribute(
        "transform",
        `rotate(${minutes * 6} ${cX} ${cY})`
      );
      secondHand.setAttribute(
        "transform",
        `rotate(${seconds * 6} ${cX} ${cY})`
      );

      if (dayEl) {
        try {
          dayEl.textContent = now.toLocaleDateString(undefined, {
            weekday: "long",
          });
        } catch (e) {
          dayEl.textContent = now.toDateString();
        }
      }

      if (dateEl) {
        try {
          dateEl.textContent = now.toLocaleDateString(undefined, {
            day: "2-digit",
            month: "short",
            year: "numeric",
          });
        } catch (e) {
          dateEl.textContent = now.toDateString();
        }
      }

      if (timeEl) {
        timeEl.textContent =
          pad2(now.getHours()) +
          ":" +
          pad2(now.getMinutes()) +
          ":" +
          pad2(now.getSeconds());
      }
    }

    render();
    if (dashboardClockTimer) clearInterval(dashboardClockTimer);
    dashboardClockTimer = setInterval(render, 250);
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

  function updateModePills() {
    const ltToggle = document.getElementById("ltToggle");
    const ltPill = document.getElementById("ltPill");

    const ltOn = !!(ltToggle && ltToggle.checked);
    if (ltPill) ltPill.classList.toggle("is-active", ltOn);
  }

  function setFanSpeedValue(value) {
    const el = document.getElementById("fanSpeedValue");
    if (!el) return;
    const v = Number(value);
    el.textContent = Number.isFinite(v) ? `${Math.round(v)}%` : "--%";
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

  function setText(id, val) {
    const el = document.getElementById(id);
    if (!el) return;
    if (val === undefined || val === null || val === "") {
      el.textContent = "--";
    } else {
      el.textContent = val;
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

  function getBool(id) {
    const el = document.getElementById(id);
    if (!el) return false;
    return !!el.checked;
  }

  function formatValue(val, digits = 2, unit = "") {
    const n = Number(val);
    if (!Number.isFinite(n)) return "--";
    const base = n.toFixed(digits);
    return unit ? `${base} ${unit}` : base;
  }

  const SETUP_KEY_LABELS = {
    DEVID: "Device ID",
    ADMID: "Admin username",
    ADMPW: "Admin password",
    USRID: "User username",
    USRPW: "User password",
    WIFSSD: "Wi-Fi SSID",
    WIFPASS: "Wi-Fi password",
    APNAM: "AP SSID",
    APPSS: "AP password",
    TMPTH: "Temp trip",
    TPWRN: "Temp warn",
    FLMAX: "Floor max temp",
    NCFIN: "Nichrome final temp",
    FLMRG: "Floor switch margin",
    CURRLT: "Current limit",
    CSRC: "Current source",
    ACFRQ: "AC frequency",
    ACVLT: "AC voltage",
    CHRES: "Charge resistor",
    WOPERM: "Wire ohm/m",
    WIREG: "Wire gauge",
    NTCGT: "NTC linked channel",
    NTCBT: "NTC beta",
    NTCT0: "NTC T0",
    NTCR0: "NTC R0 / R25",
    NTCFR: "NTC fixed resistor",
    PMIND: "Presence min voltage drop",
    outputs: "Output access flags",
    CALCAP: "Cap calibration",
    CPCAPF: "Capacitance",
    CALFLR: "Floor model calibration",
    CALPRS: "Presence calibration",
  };

  for (let i = 1; i <= 10; i++) {
    const rKey = "R" + String(i).padStart(2, "0") + "OHM";
    SETUP_KEY_LABELS[rKey] = `Wire ${i} resistance`;
    SETUP_KEY_LABELS["CALW" + String(i)] = `Wire ${i} calibration`;
  }

  function setupLabelForKey(key) {
    if (!key) return "--";
    return SETUP_KEY_LABELS[key] || key;
  }

  function filterMissingForAccess(keys) {
    if (!Array.isArray(keys)) return [];
    const access =
      (lastLoadedControls && lastLoadedControls.outputAccess) || null;
    if (!access) return keys.slice();

    return keys.filter((raw) => {
      const key = String(raw || "");
      let match = /^CALW(\d+)$/.exec(key);
      if (!match) match = /^R(\d{2})OHM$/.exec(key);
      if (!match) return true;
      const idx = parseInt(match[1], 10);
      if (!idx) return true;
      const flag = access["output" + idx];
      return flag !== false;
    });
  }

  function renderSetupMissingList(listId, keys) {
    const ul = document.getElementById(listId);
    if (!ul) return;
    ul.innerHTML = "";
    const filtered = filterMissingForAccess(keys);
    if (!Array.isArray(filtered) || filtered.length === 0) {
      const li = document.createElement("li");
      li.textContent = "None";
      ul.appendChild(li);
      return;
    }
    filtered.forEach((k) => {
      const li = document.createElement("li");
      li.textContent = setupLabelForKey(k);
      ul.appendChild(li);
    });
  }

  function setSetupPill(id, label, state) {
    const el = document.getElementById(id);
    if (!el) return;
    el.textContent = label;
    el.classList.remove("ok", "warn", "bad");
    if (state === "ok") el.classList.add("ok");
    else if (state === "bad") el.classList.add("bad");
    else el.classList.add("warn");
  }

  function setUserTabEnabled(enabled) {
    const tab = document.getElementById("userTabBtn");
    if (!tab) return;
    tab.classList.toggle("tab-disabled", !enabled);
    tab.title = enabled ? "" : "User mode locked until setup is complete.";
    if (!enabled && tab.classList.contains("active")) {
      switchTab(0);
    }
  }

  function renderSetupWireGrid(status) {
    const grid = document.getElementById("setupWireGrid");
    if (!grid) return;
    grid.innerHTML = "";
    const stageMap = (status && status.wireStage) || {};
    const runMap = (status && status.wireRunning) || {};
    const doneMap = (status && status.wireCalibrated) || {};
    const access =
      (lastLoadedControls && lastLoadedControls.outputAccess) || null;

    for (let i = 1; i <= 10; i++) {
      if (access && access["output" + i] === false) {
        continue;
      }
      const key = String(i);
      const stage = Number(stageMap[key] || 0);
      const running = !!runMap[key];
      const done = !!doneMap[key];
      const chip = document.createElement("div");
      chip.className = "setup-wire-chip";
      if (done) chip.classList.add("is-done");
      else if (running) chip.classList.add("is-running");
      let label = `W${i}`;
      if (running) label += ` - stage ${stage || 0}`;
      else if (done) label += " - done";
      else if (stage > 0) label += ` - stage ${stage}`;
      else label += " - pending";
      chip.textContent = label;
      grid.appendChild(chip);
    }
  }

  function updateSetupBanner(status) {
    const banner = document.getElementById("setupBanner");
    if (!banner) return;
    const setupDone = !!(status && status.setupDone);
    const configOk = !!(status && status.configOk);
    const calibOk = !!(status && status.calibOk);
    const needsConfig = !setupDone || !configOk;
    const needsCalib = setupDone && configOk && !calibOk;
    const show = needsConfig || needsCalib;
    banner.classList.toggle("show", show);
    const dashboard = document.getElementById("dashboardTab");
    if (dashboard) dashboard.classList.toggle("banner-active", show);

    const titleEl = document.getElementById("setupBannerTitle");
    const mainTextEl = document.getElementById("setupBannerMainText");
    const detailEl = document.getElementById("setupBannerDetail");

    if (titleEl) {
      titleEl.textContent = needsCalib ? "Calibration pending" : "Setup required";
    }
    if (mainTextEl) {
      mainTextEl.textContent = needsCalib
        ? "Calibration is not finished yet. You can complete it later."
        : "Complete the setup wizard before running the device.";
    }

    const missingRaw = needsConfig
      ? (status && status.missingConfig) || []
      : (status && status.missingCalib) || [];
    const missing = filterMissingForAccess(missingRaw);
    const detail = missing.slice(0, 5).map(setupLabelForKey).join(", ");
    if (detailEl) {
      if (missing.length) {
        detailEl.textContent = `Missing: ${detail}`;
      } else if (needsCalib) {
        detailEl.textContent = "All required calibration steps are done.";
      } else {
        detailEl.textContent = "All required items complete.";
      }
    }
  }

  function updateSetupUiFromStatus(status) {
    if (!status) return;
    lastSetupStatus = status;
    setupConfigOk = !!status.configOk;
    setupCalibOk = !!status.calibOk;
    setupRunAllowed = !!status.runAllowed && setupCalibOk;
    syncWizardStepFromStatus(status);

    const setupDone = !!status.setupDone;
    setSetupPill(
      "setupDonePill",
      `Setup: ${setupDone ? "Done" : "Pending"}`,
      setupDone ? "ok" : "warn"
    );
    setSetupPill(
      "setupConfigPill",
      `Config: ${setupConfigOk ? "OK" : "Missing"}`,
      setupConfigOk ? "ok" : "warn"
    );
    setSetupPill(
      "setupCalibPill",
      `Calibration: ${setupCalibOk ? "OK" : "Missing"}`,
      setupCalibOk ? "ok" : "warn"
    );
    setSetupPill(
      "setupRunPill",
      `Run: ${setupRunAllowed ? "Allowed" : "Blocked"}`,
      setupRunAllowed ? "ok" : "bad"
    );

    setField("setupStageInput", status.stage);
    setField("setupSubstageInput", status.substage);
    setField("setupWireIndexInput", status.wireIndex);

    renderSetupMissingList("setupMissingConfigList", status.missingConfig);
    renderSetupMissingList("setupMissingCalibList", status.missingCalib);

    const floorCal = !!status.floorCalibrated;
    const floorRunning = !!status.floorRunning;
    const floorStage = Number(status.floorStage || 0);
    const floorStatus = floorCal
      ? "Done"
      : floorRunning
      ? `Running (stage ${floorStage})`
      : floorStage
      ? `Stage ${floorStage}`
      : "Pending";
    setText("setupFloorStatus", floorStatus);
    setText("floorCalStageText", floorStage ? String(floorStage) : "--");
    setText("floorCalRunningText", floorRunning ? "Yes" : "No");
    if (status.floorCalibrated !== undefined) {
      setText("floorCalDoneText", floorCal ? "Yes" : "No");
    }
    setText(
      "setupPresenceStatus",
      status.presenceCalibrated ? "Done" : "Pending"
    );
    setText("setupCapStatus", status.capCalibrated ? "Done" : "Pending");

    renderSetupWireGrid(status);
    updateSetupBanner(status);
    setUserTabEnabled(setupDone);
    updateSetupWizardOverlay(status);
    maybeShowSetupWizard(status);
    applySafetyLocks();
  }

  // ========================================================
  // ===============     SETUP WIZARD FLOW      =============
  // ========================================================

  const WIZARD_STEPS = [
    {
      id: "wizardStepIntro",
      title: "Welcome",
      desc: "Review missing items before starting setup.",
      optional: false,
      stage: 0,
    },
    {
      id: "wizardStepCredentials",
      title: "Credentials",
      desc: "Set admin and user accounts.",
      optional: true,
      skipKey: "cred",
      stage: 1,
    },
    {
      id: "wizardStepWifi",
      title: "Wi-Fi",
      desc: "Configure station and access point settings.",
      optional: true,
      skipKey: "wifi",
      stage: 2,
    },
    {
      id: "wizardStepOutputs",
      title: "Output Selection",
      desc: "Enable the outputs used for setup and runtime.",
      optional: false,
      stage: 3,
    },
    {
      id: "wizardStepPresence",
      title: "Presence Check",
      desc: "Run the voltage-drop presence probe.",
      optional: true,
      skipKey: "presence",
      stage: 4,
    },
    {
      id: "wizardStepNtcParams",
      title: "NTC Parameters",
      desc: "Confirm beta, T0, and resistance values.",
      optional: false,
      stage: 5,
    },
    {
      id: "wizardStepDevice",
      title: "Device Settings",
      desc: "Set device, safety, wire, and floor configuration.",
      optional: false,
      stage: 6,
    },
    {
      id: "wizardStepWireCal",
      title: "Wire Calibration",
      desc: "Calibrate wires now or skip for later.",
      optional: true,
      skipKey: "wire",
      stage: 7,
    },
    {
      id: "wizardStepFloorCal",
      title: "Floor Calibration",
      desc: "Calibrate floor model now or skip for later.",
      optional: true,
      skipKey: "floor",
      stage: 8,
    },
    {
      id: "wizardStepFinish",
      title: "Finish",
      desc: "Complete setup and review calibration status.",
      optional: false,
      stage: 9,
    },
  ];

  const WIZARD_CRED_KEYS = new Set(["ADMID", "ADMPW", "USRID", "USRPW"]);
  const WIZARD_WIFI_KEYS = new Set(["WIFSSD", "WIFPASS", "APNAM", "APPSS"]);
  const WIZARD_OUTPUT_KEYS = new Set(["outputs"]);
  const WIZARD_NTC_KEYS = new Set(["NTCBT", "NTCT0", "NTCR0", "NTCFR"]);
  const WIZARD_OPTIONAL_CALIB_KEYS = new Set([]);

  function normalizeSetupKeys(list) {
    if (!Array.isArray(list)) return [];
    return list.map((k) => String(k || "").trim()).filter((k) => k.length);
  }

  function getMissingConfigForStep(status, stepKey) {
    const missing = normalizeSetupKeys(status && status.missingConfig);
    if (stepKey === "cred") {
      return missing.filter((k) => WIZARD_CRED_KEYS.has(k));
    }
    if (stepKey === "wifi") {
      return missing.filter((k) => WIZARD_WIFI_KEYS.has(k));
    }
    if (stepKey === "outputs") {
      return missing.filter((k) => WIZARD_OUTPUT_KEYS.has(k));
    }
    if (stepKey === "ntc") {
      return missing.filter((k) => WIZARD_NTC_KEYS.has(k));
    }
    if (stepKey === "device") {
      return missing.filter(
        (k) =>
          !WIZARD_CRED_KEYS.has(k) &&
          !WIZARD_WIFI_KEYS.has(k) &&
          !WIZARD_OUTPUT_KEYS.has(k) &&
          !WIZARD_NTC_KEYS.has(k)
      );
    }
    return [];
  }

  function getMissingCalibForStep(status, stepKey) {
    const missing = normalizeSetupKeys(status && status.missingCalib);
    if (stepKey === "wire") {
      return missing.filter((k) => /^CALW\d+$/.test(k));
    }
    if (stepKey === "floor") {
      return missing.filter((k) => k === "CALFLR");
    }
    if (stepKey === "presence") {
      return missing.filter((k) => k === "CALPRS");
    }
    return [];
  }

  function getWizardMissingConfig(status) {
    let missing = normalizeSetupKeys(status && status.missingConfig);
    if (wizardSkipped.cred) {
      missing = missing.filter((k) => !WIZARD_CRED_KEYS.has(k));
    }
    if (wizardSkipped.wifi) {
      missing = missing.filter((k) => !WIZARD_WIFI_KEYS.has(k));
    }
    return filterMissingForAccess(missing);
  }

  function getWizardMissingCalib(status) {
    let missing = normalizeSetupKeys(status && status.missingCalib);
    missing = missing.filter((k) => !WIZARD_OPTIONAL_CALIB_KEYS.has(k));
    if (wizardSkipped.wire) {
      missing = missing.filter((k) => !/^CALW\d+$/.test(k));
    }
    if (wizardSkipped.floor) {
      missing = missing.filter((k) => k !== "CALFLR");
    }
    if (wizardSkipped.presence) {
      missing = missing.filter((k) => k !== "CALPRS");
    }
    return filterMissingForAccess(missing);
  }

  function isWizardConfigOk(status) {
    return getWizardMissingConfig(status).length === 0;
  }

  function isWizardCalibOk(status) {
    return getWizardMissingCalib(status).length === 0;
  }

  function syncWizardSkippedWithStatus(status) {
    if (!status) return;
    if (wizardSkipped.cred && getMissingConfigForStep(status, "cred").length === 0) {
      setWizardSkipped("cred", false);
    }
    if (wizardSkipped.wifi && getMissingConfigForStep(status, "wifi").length === 0) {
      setWizardSkipped("wifi", false);
    }
    if (wizardSkipped.wire && getMissingCalibForStep(status, "wire").length === 0) {
      setWizardSkipped("wire", false);
    }
    if (wizardSkipped.floor && getMissingCalibForStep(status, "floor").length === 0) {
      setWizardSkipped("floor", false);
    }
    if (
      wizardSkipped.presence &&
      getMissingCalibForStep(status, "presence").length === 0
    ) {
      setWizardSkipped("presence", false);
    }
  }

  function isWizardStepComplete(index, status) {
    const step = WIZARD_STEPS[index];
    if (!step) return false;
    if (step.id === "wizardStepIntro") return true;
    if (step.id === "wizardStepCredentials") {
      return (
        wizardSkipped.cred ||
        getMissingConfigForStep(status, "cred").length === 0
      );
    }
    if (step.id === "wizardStepWifi") {
      return (
        wizardSkipped.wifi ||
        getMissingConfigForStep(status, "wifi").length === 0
      );
    }
    if (step.id === "wizardStepDevice") {
      return getMissingConfigForStep(status, "device").length === 0;
    }
    if (step.id === "wizardStepPresence") {
      return (
        wizardSkipped.presence ||
        getMissingCalibForStep(status, "presence").length === 0
      );
    }
    if (step.id === "wizardStepOutputs") {
      return getMissingConfigForStep(status, "outputs").length === 0;
    }
    if (step.id === "wizardStepNtcParams") {
      return getMissingConfigForStep(status, "ntc").length === 0;
    }
    if (step.id === "wizardStepWireCal") {
      return (
        wizardSkipped.wire ||
        getMissingCalibForStep(status, "wire").length === 0
      );
    }
    if (step.id === "wizardStepFloorCal") {
      return (
        wizardSkipped.floor ||
        getMissingCalibForStep(status, "floor").length === 0
      );
    }
    if (step.id === "wizardStepFinish") {
      return isWizardConfigOk(status) && isWizardCalibOk(status);
    }
    return true;
  }

  function wizardMaxAccessibleIndex(status) {
    if (!status) return wizardStepIndex;
    for (let i = 0; i < WIZARD_STEPS.length; i++) {
      if (!isWizardStepComplete(i, status)) return i;
    }
    return WIZARD_STEPS.length - 1;
  }

  function refreshWizardRailState(status) {
    const rail = document.getElementById("setupWizardRail");
    if (!rail) return;
    const items = rail.querySelectorAll(".wizard-rail-item");
    if (!items.length) return;
    const maxIndex = wizardMaxAccessibleIndex(status);
    items.forEach((item, i) => {
      const step = WIZARD_STEPS[i];
      const skipped = !!(step && step.skipKey && wizardSkipped[step.skipKey]);
      item.classList.toggle("active", i === wizardStepIndex);
      item.classList.toggle("disabled", i > maxIndex);
      item.classList.toggle("complete", isWizardStepComplete(i, status));
      item.classList.toggle("skipped", skipped);
      item.setAttribute("aria-disabled", i > maxIndex ? "true" : "false");
      if (i > maxIndex) {
        item.title = "Complete or skip previous steps first.";
      } else if (skipped) {
        item.title = "Skipped for now.";
      } else {
        item.title = "";
      }
    });
  }

  function wizardIndexForStage(stage) {
    const s = Number(stage);
    if (!Number.isFinite(s)) return 0;
    const idx = WIZARD_STEPS.findIndex((step) => step.stage === s);
    if (idx >= 0) return idx;
    return s <= WIZARD_STEPS[0].stage ? 0 : WIZARD_STEPS.length - 1;
  }

  function syncWizardStepFromStatus(status) {
    if (!status) return;
    syncWizardSkippedWithStatus(status);
    const nextIndex = wizardIndexForStage(status.stage);
    const maxIndex = wizardMaxAccessibleIndex(status);
    const targetIndex = Math.min(nextIndex, maxIndex);
    if (targetIndex === wizardStepIndex) {
      if (wizardActive) refreshWizardRailState(status);
      return;
    }
    wizardStepIndex = targetIndex;
    if (wizardActive) {
      setWizardStep(wizardStepIndex, false);
    }
  }

  function hasMissingWireCalibration(status) {
    const missingRaw =
      status && Array.isArray(status.missingCalib) ? status.missingCalib : [];
    const missing = filterMissingForAccess(missingRaw);
    return missing.some((key) => /^CALW\d+$/.test(String(key || "")));
  }

  function formatWizardWireStatus(status) {
    if (!status) return "Pending";
    const runMap = status.wireRunning || {};
    const stageMap = status.wireStage || {};
    const runningKey = Object.keys(runMap).find((k) => !!runMap[k]);
    if (runningKey) {
      const stage = Number(stageMap[runningKey] || 0);
      const stageLabel = stage ? ` stage ${stage}` : "";
      return `Running (W${runningKey}${stageLabel})`;
    }
    return hasMissingWireCalibration(status) ? "Pending" : "Done";
  }

  function formatWizardFloorStatus(status) {
    if (!status) return "Pending";
    const floorStage = Number(status.floorStage || 0);
    if (status.floorRunning) {
      return `Running (stage ${floorStage || 0})`;
    }
    if (status.floorCalibrated) return "Done";
    if (floorStage) return `Stage ${floorStage}`;
    return "Pending";
  }

  function setupWizardOverlay() {
    return document.getElementById("setupWizardOverlay");
  }

  function buildWizardRail() {
    const rail = document.getElementById("setupWizardRail");
    if (!rail) return;
    rail.innerHTML = "";
    WIZARD_STEPS.forEach((step, i) => {
      const item = document.createElement("div");
      item.className = "wizard-rail-item";
      if (step.optional) item.classList.add("optional");
      item.textContent = `${i + 1}. ${step.title}`;
      item.addEventListener("click", () => requestWizardStep(i));
      rail.appendChild(item);
    });
    refreshWizardRailState(lastSetupStatus);
  }

  function cacheWizardNode(el) {
    if (!el) return;
    const exists = wizardNodeCache.some((item) => item.el === el);
    if (exists) return;
    wizardNodeCache.push({
      el,
      parent: el.parentNode,
      next: el.nextSibling,
    });
  }

  function mountWizardNodes() {
    const map = [
      {
        selector: ".admin-credentials-card",
        mount: "wizardMountCredentials",
      },
      {
        selector: ".user-settings-card",
        mount: "wizardMountUser",
      },
      {
        selector: ".wifi-station-card",
        mount: "wizardMountWifiStation",
      },
      {
        selector: ".wifi-ap-card",
        mount: "wizardMountWifiAp",
      },
      {
        selector: ".device-settings-box",
        mount: "wizardMountDevice",
      },
      {
        selector: "#ntcParamsCard",
        mount: "wizardMountNtcParams",
      },
      {
        selector: "#presenceProbeSection",
        mount: "wizardMountPresence",
      },
      {
        selector: ".user-access-card",
        mount: "wizardMountOutputs",
      },
    ];

    map.forEach((entry) => {
      const el = document.querySelector(entry.selector);
      const mount = document.getElementById(entry.mount);
      if (!el || !mount) return;
      cacheWizardNode(el);
      mount.appendChild(el);
    });
  }

  function restoreWizardNodes() {
    wizardNodeCache.forEach((item) => {
      if (!item.el || !item.parent) return;
      if (item.next && item.next.parentNode === item.parent) {
        item.parent.insertBefore(item.el, item.next);
      } else {
        item.parent.appendChild(item.el);
      }
    });
  }

  function setWizardStep(index, updateStage = false) {
    if (index < 0 || index >= WIZARD_STEPS.length) return;
    wizardStepIndex = index;

    WIZARD_STEPS.forEach((step, i) => {
      const el = document.getElementById(step.id);
      if (el) el.classList.toggle("active", i === index);
    });

    const rail = document.getElementById("setupWizardRail");
    if (rail) {
      const items = rail.querySelectorAll(".wizard-rail-item");
      items.forEach((item, i) => {
        item.classList.toggle("active", i === index);
      });
    }

    const step = WIZARD_STEPS[index];
    const titleEl = document.getElementById("setupWizardStepTitle");
    const descEl = document.getElementById("setupWizardStepDesc");
    if (titleEl) titleEl.textContent = step.title;
    if (descEl) descEl.textContent = step.desc;

    updateWizardControls();
    updateWizardNote();
    updateWizardFloorGate(lastSetupStatus);
    refreshWizardRailState(lastSetupStatus);

    if (updateStage) {
      updateSetupStatus({ stage: step.stage, substage: 0 });
    }
  }

  function updateWizardControls() {
    const backBtn = document.getElementById("wizardBackBtn");
    const nextBtn = document.getElementById("wizardNextBtn");
    const skipBtn = document.getElementById("wizardSkipBtn");
    const finishBtn = document.getElementById("wizardFinishBtn");

    const isFirst = wizardStepIndex === 0;
    const isLast = wizardStepIndex === WIZARD_STEPS.length - 1;
    const step = WIZARD_STEPS[wizardStepIndex];
    const status = lastSetupStatus;
    const canAdvance = !status || isWizardStepComplete(wizardStepIndex, status);
    const canFinish = !status || isWizardConfigOk(status);

    if (backBtn) backBtn.disabled = isFirst;
    if (nextBtn) nextBtn.style.display = isLast ? "none" : "";
    if (nextBtn) nextBtn.disabled = !canAdvance;
    if (finishBtn) finishBtn.style.display = isLast ? "" : "none";
    if (finishBtn) finishBtn.disabled = !canFinish;
    if (skipBtn) skipBtn.style.display = step.optional ? "" : "none";
  }

  function updateWizardNote() {
    const note = document.getElementById("setupWizardNote");
    if (!note) return;
    const step = WIZARD_STEPS[wizardStepIndex];
    if (step && step.optional) {
      const skipped = !!(step.skipKey && wizardSkipped[step.skipKey]);
      note.textContent = skipped
        ? "Step skipped. You can complete it later."
        : "You can skip this step and return later.";
    } else {
      note.textContent = "Complete this step to proceed.";
    }
  }

  function updateWizardFloorGate(status) {
    const note = document.getElementById("wizardFloorGateNote");
    const floorBtn = document.getElementById("wizardOpenFloorCalBtn");
    if (!note && !floorBtn) return;

    const wireCalComplete =
      !status || getMissingCalibForStep(status, "wire").length === 0;
    if (note) note.classList.toggle("show", !wireCalComplete);
    if (floorBtn) floorBtn.disabled = !wireCalComplete;
  }

  function updateWizardStatus(status) {
    const configPill = document.getElementById("setupWizardConfigPill");
    const calibPill = document.getElementById("setupWizardCalibPill");
    const wizardConfigOk = isWizardConfigOk(status);
    const wizardCalibOk = isWizardCalibOk(status);
    if (configPill) {
      setSetupPill(
        "setupWizardConfigPill",
        `Config: ${wizardConfigOk ? "OK" : "Missing"}`,
        wizardConfigOk ? "ok" : "warn"
      );
    }
    if (calibPill) {
      setSetupPill(
        "setupWizardCalibPill",
        `Calibration: ${wizardCalibOk ? "OK" : "Pending"}`,
        wizardCalibOk ? "ok" : "warn"
      );
    }

    renderSetupMissingList("wizardMissingConfigList", getWizardMissingConfig(status));
    renderSetupMissingList("wizardMissingCalibList", getWizardMissingCalib(status));

    const wireStatusEl = document.getElementById("wizardWireCalStatus");
    if (wireStatusEl) {
      wireStatusEl.textContent = formatWizardWireStatus(status);
    }

    const floorStatusEl = document.getElementById("wizardFloorCalStatus");
    if (floorStatusEl) {
      floorStatusEl.textContent = formatWizardFloorStatus(status);
    }

    const summaryConfig = document.getElementById("wizardSummaryConfig");
    const summaryCalib = document.getElementById("wizardSummaryCalib");
    if (summaryConfig) {
      summaryConfig.textContent = wizardConfigOk ? "Complete" : "Incomplete";
    }
    if (summaryCalib) {
      summaryCalib.textContent = wizardCalibOk ? "Complete" : "Pending";
    }
    updateWizardFloorGate(status);
    updateWizardControls();
    refreshWizardRailState(status);
  }

  function updateSetupWizardOverlay(status) {
    if (!wizardActive) return;
    if (!status) return;
    updateWizardStatus(status);
  }

  function openSetupWizard() {
    const overlay = setupWizardOverlay();
    if (!overlay) return;
    if (!wizardActive) {
      wizardActive = true;
      overlay.classList.add("show");
      overlay.setAttribute("aria-hidden", "false");
      document.body.classList.add("wizard-active");
      mountWizardNodes();
      bindDeviceSettingsSubtabs(document);
      buildWizardRail();
      const maxIndex = wizardMaxAccessibleIndex(lastSetupStatus);
      wizardStepIndex = Math.min(wizardStepIndex, maxIndex);
      setWizardStep(wizardStepIndex);
    }
  }

  function closeSetupWizard() {
    const overlay = setupWizardOverlay();
    if (!overlay) return;
    overlay.classList.remove("show");
    overlay.setAttribute("aria-hidden", "true");
    document.body.classList.remove("wizard-active");
    wizardActive = false;
    restoreWizardNodes();
  }

  function maybeShowSetupWizard(status) {
    if (!status) return;
    const needsWizard = !status.setupDone || !isWizardConfigOk(status);
    if (needsWizard) {
      openSetupWizard();
    } else if (wizardActive) {
      closeSetupWizard();
    }
  }

  function requestWizardStep(index) {
    if (index < 0 || index >= WIZARD_STEPS.length) return;
    const status = lastSetupStatus;
    if (status) {
      const maxIndex = wizardMaxAccessibleIndex(status);
      if (index > maxIndex && index !== wizardStepIndex) {
        openAlert(
          "Setup Wizard",
          "Complete or skip the previous step before continuing.",
          "warning"
        );
        return;
      }
    }
    setWizardStep(index, true);
  }

  function wizardNext() {
    const status = lastSetupStatus;
    if (status && !isWizardStepComplete(wizardStepIndex, status)) {
      openAlert(
        "Setup Wizard",
        "Complete or skip this step to continue.",
        "warning"
      );
      return;
    }
    const next = Math.min(wizardStepIndex + 1, WIZARD_STEPS.length - 1);
    requestWizardStep(next);
  }

  function wizardBack() {
    const prev = Math.max(wizardStepIndex - 1, 0);
    requestWizardStep(prev);
  }

  function wizardSkip() {
    const step = WIZARD_STEPS[wizardStepIndex];
    if (step && step.skipKey) {
      setWizardSkipped(step.skipKey, true);
      updateWizardStatus(lastSetupStatus);
    }
    wizardNext();
  }

  async function wizardFinish() {
    const status = await fetchSetupStatus();
    if (!status) return;

    const missingConfig = getWizardMissingConfig(status);
    if (missingConfig.length) {
      const detail = missingConfig.slice(0, 5).map(setupLabelForKey).join(", ");
      openAlert(
        "Setup Wizard",
        `Configuration is incomplete: ${detail}`,
        "warning"
      );
      return;
    }

    const missingCalib = getWizardMissingCalib(status);
    if (missingCalib.length) {
      const detail = missingCalib.slice(0, 5).map(setupLabelForKey).join(", ");
      openAlert(
        "Setup Wizard",
        `Calibration is still pending: ${detail}. You can finish now and calibrate later.`,
        "warning"
      );
    }

    const finishStage = WIZARD_STEPS[WIZARD_STEPS.length - 1].stage;
    await updateSetupStatus({ setup_done: true, stage: finishStage, substage: 0 });
    const updated = await fetchSetupStatus();
    if (updated && updated.setupDone) {
      closeSetupWizard();
    }
  }

  function bindWizardControls() {
    const backBtn = document.getElementById("wizardBackBtn");
    const nextBtn = document.getElementById("wizardNextBtn");
    const skipBtn = document.getElementById("wizardSkipBtn");
    const finishBtn = document.getElementById("wizardFinishBtn");
    const wireBtn = document.getElementById("wizardOpenWireCalBtn");
    const floorBtn = document.getElementById("wizardOpenFloorCalBtn");
    const floorWireBtn = document.getElementById("wizardFloorGoWireBtn");
    const floorNtcBtn = document.getElementById("wizardFloorGoNtcBtn");

    if (backBtn) backBtn.addEventListener("click", wizardBack);
    if (nextBtn) nextBtn.addEventListener("click", wizardNext);
    if (skipBtn) skipBtn.addEventListener("click", wizardSkip);
    if (finishBtn) finishBtn.addEventListener("click", wizardFinish);

    if (wireBtn) {
      wireBtn.addEventListener("click", () => {
        openCalibrationModal();
      });
    }
    if (floorBtn) {
      floorBtn.addEventListener("click", () => {
        openCalibrationModal();
      });
    }
    if (floorWireBtn) {
      floorWireBtn.addEventListener("click", () => {
        const idx = WIZARD_STEPS.findIndex(
          (step) => step.id === "wizardStepWireCal"
        );
        if (idx >= 0) requestWizardStep(idx);
      });
    }
    if (floorNtcBtn) {
      floorNtcBtn.addEventListener("click", () => {
        const idx = WIZARD_STEPS.findIndex(
          (step) => step.id === "wizardStepNtcParams"
        );
        if (idx >= 0) requestWizardStep(idx);
      });
    }
  }

  // ========================================================
  // ===============        CONFIRM MODAL        ============
  // ========================================================

  function openConfirm(kind, action) {
    pendingConfirm = kind || null;
    pendingConfirmAction = typeof action === "function" ? action : null;

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
      message.textContent = "This will reset the device (soft reset). Proceed-";
      okBtn.textContent = "Yes, Reset";
      okBtn.classList.add("danger");
    } else if (kind === "reboot") {
      title.textContent = "Confirm Reboot";
      message.textContent = "The device will reboot. Continue-";
      okBtn.textContent = "Yes, Reboot";
      okBtn.classList.add("danger");
    } else if (kind === "wifiRestart") {
      title.textContent = "Restart Required";
      message.textContent =
        "Changing Wi-Fi settings will restart the device and disconnect this session. Continue-";
      okBtn.textContent = "Confirm & Restart";
      okBtn.classList.add("danger");
    } else if (kind === "sessionChange") {
      title.textContent = "Confirm Session Change";
      message.textContent =
        "Updating credentials will disconnect all users and return to login. Continue-";
      okBtn.textContent = "Confirm & Disconnect";
      okBtn.classList.add("danger");
    } else if (kind === "sessionWifiChange") {
      title.textContent = "Confirm Changes";
      message.textContent =
        "Updating credentials and Wi-Fi settings will disconnect all users and restart the device. Continue-";
      okBtn.textContent = "Confirm";
      okBtn.classList.add("danger");
    } else if (kind === "setupReset") {
      title.textContent = "Reset Setup";
      message.textContent =
        "This clears setup progress and calibration flags. Continue-";
      okBtn.textContent = "Reset Setup";
      okBtn.classList.add("danger");
    } else {
      title.textContent = "Confirm Action";
      message.textContent = "Are you sure-";
      okBtn.textContent = "Confirm";
      okBtn.classList.add("warning");
    }

    modal.style.display = "flex";
  }

  function closeConfirm() {
    const modal = document.getElementById("confirmModal");
    if (modal) modal.style.display = "none";
    pendingConfirm = null;
    pendingConfirmAction = null;
  }

  function bindConfirmModal() {
    const modal = document.getElementById("confirmModal");
    const okBtn = document.getElementById("confirmOkBtn");
    const cancelBtn = document.getElementById("confirmCancelBtn");

    if (!modal || !okBtn) return;

    if (cancelBtn) {
      cancelBtn.addEventListener("click", () => {
        pendingConfirm = null;
        pendingConfirmAction = null;
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
      } else if (pendingConfirmAction) {
        pendingConfirmAction();
      }

      pendingConfirm = null;
      pendingConfirmAction = null;
      closeConfirm();
    });

    // Click outside to close
    modal.addEventListener("click", (e) => {
      if (e.target === modal) {
        pendingConfirm = null;
        pendingConfirmAction = null;
        closeConfirm();
      }
    });

    // ESC key closes
    document.addEventListener("keydown", (e) => {
      if (e.key === "Escape") {
        pendingConfirm = null;
        pendingConfirmAction = null;
        closeConfirm();
      }
    });
  }

  // Generic alert that reuses confirm modal with no pendingConfirm
  function openAlert(title, message, variant = "warning") {
    pendingConfirm = null;
    pendingConfirmAction = null;

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
  // ===============       SAFETY INTERLOCKS     ============
  // ========================================================

  function isAutoLoopRunning() {
    return lastState === "Running";
  }

  function isCalibrationActive() {
    const wireActive = !!(lastWireTestStatus && lastWireTestStatus.running);
    const calibRunning = !!(calibLastMeta && calibLastMeta.running);
    return (
      !!(testModeState && testModeState.active) ||
      wireActive ||
      calibRunning
    );
  }

  function guardUnsafeAction(actionLabel, opts = {}) {
    if (opts.blockCalib && isCalibrationActive()) {
      openAlert(
        "Calibration active",
        `Stop the active calibration/test before ${actionLabel}.`,
        "warning"
      );
      return true;
    }
    if (opts.blockAuto && isAutoLoopRunning()) {
      openAlert(
        "Auto loop running",
        `Stop the loop before ${actionLabel}.`,
        "warning"
      );
      return true;
    }
    return false;
  }

  function applySafetyLocks() {
    const autoRunning = isAutoLoopRunning();
    const calibActive = isCalibrationActive();
    const blockSettings = autoRunning || calibActive;
    const blockStart = !setupRunAllowed && lastState === "Shutdown";

    const powerBtn = powerEl();
    if (powerBtn) {
      const disable = calibActive || blockStart;
      powerBtn.disabled = disable;
      powerBtn.classList.toggle("action-locked", disable);
      powerBtn.setAttribute("aria-disabled", disable ? "true" : "false");
    }

    document.querySelectorAll(".round-button.reset").forEach((btn) => {
      btn.classList.toggle("action-locked", calibActive);
      btn.setAttribute("aria-disabled", calibActive ? "true" : "false");
    });

    const confirmBtn = document.getElementById("wiresCoolConfirmBtn");
    if (confirmBtn) {
      const disable = autoRunning || calibActive;
      confirmBtn.classList.toggle("action-locked", disable);
      confirmBtn.setAttribute("aria-disabled", disable ? "true" : "false");
    }

    const forceBtn = document.getElementById("forceCalibrationBtn");
    if (forceBtn) forceBtn.disabled = blockSettings;

    const saveSelector =
      "#deviceSettingsTab .settings-btn-primary," +
      " #adminSettingsTab .settings-btn-primary," +
      " #userSettingsTab .settings-btn-primary";
    document.querySelectorAll(saveSelector).forEach((btn) => {
      btn.disabled = blockSettings;
    });

    document
      .querySelectorAll(
        "#deviceSettingsTab input, #deviceSettingsTab select, #deviceSettingsTab textarea"
      )
      .forEach((el) => {
        el.disabled = blockSettings;
      });

    document
      .querySelectorAll("#deviceSettingsTab .settings-btn")
      .forEach((btn) => {
        btn.disabled = blockSettings;
      });

    document
      .querySelectorAll('#userAccessGrid input[type="checkbox"]')
      .forEach((cb) => {
        cb.disabled = blockSettings;
      });

    document
      .querySelectorAll(".setup-wizard-card input")
      .forEach((el) => {
        el.disabled = blockSettings;
      });

      const lockIds = [
        "startModelCalibBtn",
        "wireTestStartBtn",
        "ntcCalStartBtn",
        "ntcCalStopBtn",
        "ntcBetaCalBtn",
        "startFloorCalibBtn",
        "presenceProbeBtn",
        "setupUpdateBtn",
        "setupDoneBtn",
        "setupClearDoneBtn",
        "setupResetBtn",
      ];
    lockIds.forEach((id) => {
      const el = document.getElementById(id);
      if (el) el.disabled = blockSettings;
    });
  }

  // ========================================================
  // ===============        POWER BUTTON UI      ============
  // ========================================================

  function setPowerUI(state, extras = {}) {
    const btn = powerEl();
    const labelEl = powerText();
    if (!btn || !labelEl) {
      lastState = state || lastState;
      updateStatusBarState();
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
    updateStatusBarState();
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
        headers: cborHeaders(),
        body: encodeCbor({ action: "get", target: "status" }),
      });
      if (res.status === 401) {
        noteAuthFailure();
        return;
      }
      if (!res.ok) return;
      resetAuthFailures();
      const data = await readCbor(res, {});
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
          const data = decodeSsePayload(ev) || {};
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

  function startEventStream() {
    if (eventStream) return;
    try {
      eventStream = new EventSource("/event_stream");
      const handleEvent = (ev) => {
        try {
          const data = decodeSsePayload(ev) || {};
          if (data && data.unread) {
            handleEventNotice(data);
          }
        } catch (e) {
          console.warn("Event stream parse error:", e);
        }
      };
      eventStream.addEventListener("event", handleEvent);
      eventStream.onmessage = handleEvent;
      eventStream.onerror = () => {
        if (eventStream) {
          eventStream.close();
          eventStream = null;
        }
      };
    } catch (err) {
      console.warn("Event stream failed to start:", err);
    }
  }

  function initPowerButton() {
    const btn = powerEl();
    if (btn) {
      btn.addEventListener("click", onPowerClick);
    }
    // Initial state via SSE (with polling fallback)
    startStateStream();
    startEventStream();
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
  // ===============         LT TOGGLES         ============
  // ========================================================

  function toggleLT() {
    const ltToggle = document.getElementById("ltToggle");
    const isOn = !!(ltToggle && ltToggle.checked);
    updateModePills();
    sendControlCommand("set", "ledFeedback", isOn);
  }

  // ========================================================
  // ===============    MANUAL OUTPUT CONTROL    ============
  // ========================================================

  async function handleOutputToggle(index, checkbox) {
    const isOn = !!checkbox.checked;
    checkbox.disabled = true;

    if (
      guardUnsafeAction("toggling outputs", {
        blockAuto: true,
        blockCalib: true,
      })
    ) {
      checkbox.checked = !isOn;
      checkbox.disabled = false;
      return;
    }

    const pre = lastMonitor || (await fetchMonitorSnapshot());
    if (pre) applyMonitorSnapshot(pre);

    const resp = await sendControlCommand("set", "output" + index, isOn);
    if (resp && resp.error) {
      checkbox.disabled = false;
      await pollLiveOnce();
      return;
    }

    const mon = await waitForMonitorMatch(
      (m) => m && m.outputs && m.outputs["output" + index] === isOn
    );
    if (mon) applyMonitorSnapshot(mon);
    else await pollLiveOnce();

    checkbox.disabled = false;
  }

  async function updateOutputAccess(index, newState) {
    if (
      guardUnsafeAction("changing output access", {
        blockAuto: true,
        blockCalib: true,
      })
    ) {
      loadControls();
      return;
    }
    const key = "output" + index;
    const res = await sendControlCommand("set", "Access" + index, !!newState);
    if (res && res.error) {
      loadControls();
      return;
    }
    if (!lastLoadedControls) lastLoadedControls = {};
    if (!lastLoadedControls.outputAccess) lastLoadedControls.outputAccess = {};
    lastLoadedControls.outputAccess[key] = !!newState;
    renderLiveControlChart();
  }

  function toggleOutput(index, state) {
    sendControlCommand("set", "output" + index, !!state);
  }

  // ========================================================
  // ===============     SYSTEM CONTROL CMDS     ============
  // ========================================================

  async function startSystem() {
    if (guardUnsafeAction("starting the loop", { blockCalib: true })) {
      return;
    }
    if (!setupRunAllowed) {
      openAlert(
        "Setup incomplete",
        "Complete configuration and calibration before running.",
        "warning"
      );
      return;
    }
    showWiresCoolPrompt();
    const res = await sendControlCommand("set", "systemStart", true);
    if (res && res.error) {
      hideWiresCoolPrompt();
      openAlert("Start system", res.error, "danger");
    }
  }

  async function shutdownSystem() {
    if (guardUnsafeAction("stopping the loop", { blockCalib: true })) {
      return;
    }
    await sendControlCommand("set", "systemShutdown", true);
  }

  async function forceCalibration() {
    if (
      guardUnsafeAction("starting calibration", {
        blockAuto: true,
        blockCalib: true,
      })
    ) {
      return;
    }
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
    if (guardUnsafeAction("resetting the device", { blockCalib: true })) {
      return;
    }
    await sendControlCommand("set", "systemReset", true);
  }

  async function rebootSystem() {
    if (guardUnsafeAction("rebooting the device", { blockCalib: true })) {
      return;
    }
    await sendControlCommand("set", "reboot", true);
  }

  function showWiresCoolPrompt() {
    const prompt = document.getElementById("wiresCoolPrompt");
    if (!prompt) return;

    prompt.classList.add("show");

    if (wiresCoolPromptTimer) {
      clearTimeout(wiresCoolPromptTimer);
      wiresCoolPromptTimer = null;
    }

    wiresCoolPromptTimer = setTimeout(() => {
      hideWiresCoolPrompt();
    }, 5000);
  }

  function hideWiresCoolPrompt() {
    const prompt = document.getElementById("wiresCoolPrompt");
    if (!prompt) return;

    prompt.classList.remove("show");

    if (wiresCoolPromptTimer) {
      clearTimeout(wiresCoolPromptTimer);
      wiresCoolPromptTimer = null;
    }
  }

  async function confirmWiresCool() {
    if (
      guardUnsafeAction("confirming wires cool", {
        blockAuto: true,
        blockCalib: true,
      })
    ) {
      return;
    }
    const res = await sendControlCommand("set", "confirmWiresCool", true);
    if (res && res.error) {
      openAlert("Confirm Wires Cool", res.error, "danger");
      return;
    }
    hideWiresCoolPrompt();
    openAlert("Confirm Wires Cool", "Confirmation sent.", "success");
  }

  // ========================================================
  // ===============       HEARTBEAT / LOGIN     ============
  // ========================================================

  function startHeartbeat(intervalMs = 1500) {
    if (heartbeatTimer) clearInterval(heartbeatTimer);
    const tick = async () => {
      try {
        const res = await fetch("/heartbeat", { cache: "no-store" });
        if (res.status === 401) {
          noteAuthFailure();
          return;
        }
        if (res.ok) resetAuthFailures();
      } catch (err) {
        console.warn("Heartbeat failed:", err);
      }
    };
    tick();
    heartbeatTimer = setInterval(tick, intervalMs);
  }

  function disconnectDevice() {
    fetch("/disconnect", {
      method: "POST",
      headers: cborHeaders(),
      body: encodeCbor({ action: "disconnect" }),
      redirect: "follow",
    })
      .then((response) => {
        if (response.status === 401) {
          redirectToLogin();
          return null;
        }
        if (response.redirected) {
          redirectToLogin();
          return null;
        }
        if (response.ok) {
          redirectToLogin();
          return null;
        } else {
          return readCbor(response, {}).then((data) => {
            openAlert(
              "Disconnect",
              (data && data.error) || "Unexpected response"
            );
          });
        }
      })
      .catch((err) => {
        console.error("Disconnect failed:", err);
        redirectToLogin();
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
    if (action === "set") {
      payload.epoch = Math.floor(Date.now() / 1000);
    }

    try {
      const res = await fetch("/control", {
        method: "POST",
        headers: cborHeaders(),
        body: encodeCbor(payload),
      });

      const data = await readCbor(res, {});

      if (res.status === 401) {
        noteAuthFailure();
      }
      if (!res.ok) {
        console.warn("Control error:", res.status, data.error || data);
        return { error: data.error || "HTTP " + res.status };
      }
      resetAuthFailures();

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

  function bindDeviceSettingsSubtabs(root = document) {
    if (!root) return;

    const buttons = Array.from(
      root.querySelectorAll(".device-subtab-btn[data-device-subtab]")
    );
    const panels = Array.from(
      root.querySelectorAll(".device-subtab-panel[data-device-subtab-panel]")
    );

    if (!buttons.length || !panels.length) return;

    const setActive = (name) => {
      buttons.forEach((btn) => {
        const active = btn.dataset.deviceSubtab === name;
        btn.classList.toggle("is-active", active);
        btn.setAttribute("aria-selected", active ? "true" : "false");
      });

      panels.forEach((panel) => {
        const active = panel.dataset.deviceSubtabPanel === name;
        panel.classList.toggle("is-active", active);
        panel.hidden = !active;
      });

      try {
        localStorage.setItem("deviceSettingsSubtab", name);
      } catch (e) {
        // ignore storage errors
      }
    };

    const needsBind = buttons.some((btn) => !btn.dataset.subtabBound);
    if (!needsBind) return;

    buttons.forEach((btn) => {
      if (btn.dataset.subtabBound) return;
      btn.dataset.subtabBound = "true";
      btn.addEventListener("click", () => {
        const name = btn.dataset.deviceSubtab;
        if (name) setActive(name);
      });
    });

    let initial = "nichrome";
    try {
      const saved = localStorage.getItem("deviceSettingsSubtab");
      if (saved && buttons.some((btn) => btn.dataset.deviceSubtab === saved)) {
        initial = saved;
      }
    } catch (e) {
      // ignore storage errors
    }

    setActive(initial);
  }

  async function waitUntilApplied(expected, timeoutMs = 2000, stepMs = 120) {
    const deadline = Date.now() + timeoutMs;

    while (Date.now() < deadline) {
      try {
        const res = await fetch("/load_controls", {
          cache: "no-store",
          headers: cborHeaders(),
        });
        if (!res.ok) break;
        const data = await readCbor(res, {});

        let ok = true;

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

  function parseModelParamTarget(raw) {
    const value = String(raw || "").toLowerCase();
    if (value === "floor") return { kind: "floor" };
    const match = /^wire(\d+)$/.exec(value);
    const idx = match ? parseInt(match[1], 10) : 1;
    const safeIdx =
      Number.isFinite(idx) && idx >= 1 && idx <= 10 ? idx : 1;
    return { kind: "wire", index: safeIdx };
  }

  function getModelParamTarget() {
    const el = document.getElementById("modelParamTarget");
    if (!el) return { kind: "wire", index: 1 };
    return parseModelParamTarget(el.value || "wire1");
  }

  function getModelParamValues(data, target) {
    if (!data || !target) return { tau: undefined, k: undefined, c: undefined };
    if (target.kind === "floor") {
      return {
        tau: data.floorTau,
        k: data.floorK,
        c: data.floorC,
      };
    }
    const key = String(target.index);
    const wireTau = data.wireTau || {};
    const wireK = data.wireK || {};
    const wireC = data.wireC || {};
    return {
      tau: wireTau[key],
      k: wireK[key],
      c: wireC[key],
    };
  }

  function updateModelParamFields(data) {
    const targetEl = document.getElementById("modelParamTarget");
    if (!targetEl) return;
    const target = parseModelParamTarget(targetEl.value || "wire1");
    const values = getModelParamValues(data, target);
    setField("modelTau", values.tau);
    setField("modelK", values.k);
    setField("modelC", values.c);
  }

  async function saveDeviceAndNichrome() {
    if (
      guardUnsafeAction("saving device settings", {
        blockAuto: true,
        blockCalib: true,
      })
    ) {
      return;
    }
    const cmds = [];
    const expected = { wireRes: {} };
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
    if (
      currLimit !== undefined &&
      !approxEqual(currLimit, cur.currLimit, 0.05)
    ) {
      cmds.push(["set", "currLimit", currLimit]);
    }

    const currentSource = getInt("currentSource");
    if (
      currentSource !== undefined &&
      (currentSource === 0 || currentSource === 1) &&
      currentSource !== cur.currentSource
    ) {
      cmds.push(["set", "currentSource", currentSource]);
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
    if (
      floorMaxC !== undefined &&
      !approxEqual(floorMaxC, cur.floorMaxC, 0.1)
    ) {
      cmds.push(["set", "floorMaxC", floorMaxC]);
    }

    const floorSwitchMarginC = getFloat("floorSwitchMarginC");
    if (
      floorSwitchMarginC !== undefined &&
      !approxEqual(floorSwitchMarginC, cur.floorSwitchMarginC, 0.1)
    ) {
      cmds.push(["set", "floorSwitchMarginC", floorSwitchMarginC]);
    }

    const nichromeFinalTempC = getFloat("nichromeFinalTempC");
    if (
      nichromeFinalTempC !== undefined &&
      !approxEqual(nichromeFinalTempC, cur.nichromeFinalTempC, 0.1)
    ) {
      cmds.push(["set", "nichromeFinalTempC", nichromeFinalTempC]);
    }

    const ntcGateIndex = getInt("ntcGateIndex");
    if (
      ntcGateIndex !== undefined &&
      Number.isFinite(ntcGateIndex) &&
      ntcGateIndex !== cur.ntcGateIndex
    ) {
      cmds.push(["set", "ntcGateIndex", ntcGateIndex]);
    }

    const ntcBeta = getFloat("ntcBeta");
    if (ntcBeta !== undefined && !approxEqual(ntcBeta, cur.ntcBeta, 0.5)) {
      cmds.push(["set", "ntcBeta", ntcBeta]);
    }

    const ntcT0C = getFloat("ntcT0C");
    if (ntcT0C !== undefined && !approxEqual(ntcT0C, cur.ntcT0C, 0.05)) {
      cmds.push(["set", "ntcT0C", ntcT0C]);
    }

    const ntcR0 = getFloat("ntcR0");
    if (ntcR0 !== undefined && !approxEqual(ntcR0, cur.ntcR0, 0.5)) {
      cmds.push(["set", "ntcR0", ntcR0]);
    }

    const ntcFixedRes = getFloat("ntcFixedRes");
    if (
      ntcFixedRes !== undefined &&
      !approxEqual(ntcFixedRes, cur.ntcFixedRes, 0.5)
    ) {
      cmds.push(["set", "ntcFixedRes", ntcFixedRes]);
    }

    const presenceMinDropV = getFloat("presenceMinDropV");
    if (
      presenceMinDropV !== undefined &&
      !approxEqual(presenceMinDropV, cur.presenceMinDropV, 0.01)
    ) {
      cmds.push(["set", "presenceMinDropV", presenceMinDropV]);
    }

    const modelTarget = getModelParamTarget();
    const modelTau = getFloat("modelTau");
    const modelK = getFloat("modelK");
    const modelC = getFloat("modelC");
    const curModel = getModelParamValues(cur, modelTarget);
    if (
      modelTau !== undefined &&
      !approxEqual(modelTau, curModel.tau, 0.01)
    ) {
      const key =
        modelTarget.kind === "floor"
          ? "floorTau"
          : "wireTau" + modelTarget.index;
      cmds.push(["set", key, modelTau]);
    }
    if (modelK !== undefined && !approxEqual(modelK, curModel.k, 0.001)) {
      const key =
        modelTarget.kind === "floor"
          ? "floorK"
          : "wireK" + modelTarget.index;
      cmds.push(["set", key, modelK]);
    }
    if (modelC !== undefined && !approxEqual(modelC, curModel.c, 0.01)) {
      const key =
        modelTarget.kind === "floor"
          ? "floorC"
          : "wireC" + modelTarget.index;
      cmds.push(["set", key, modelC]);
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
    if (data.currentSource !== undefined) {
      setField("currentSource", data.currentSource);
    }
    setField("tempWarnC", data.tempWarnC);
    setField("tempTripC", data.tempTripC);
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
    if (data.floorSwitchMarginC !== undefined) {
      setField("floorSwitchMarginC", data.floorSwitchMarginC);
    }
    if (data.nichromeFinalTempC !== undefined) {
      setField("nichromeFinalTempC", data.nichromeFinalTempC);
    }
    if (data.ntcGateIndex !== undefined) {
      setField("ntcGateIndex", data.ntcGateIndex);
    }
    if (data.ntcBeta !== undefined) {
      setField("ntcBeta", data.ntcBeta);
    }
    if (data.ntcT0C !== undefined) {
      setField("ntcT0C", data.ntcT0C);
    }
    if (data.ntcR0 !== undefined) {
      setField("ntcR0", data.ntcR0);
    }
    if (data.ntcFixedRes !== undefined) {
      setField("ntcFixedRes", data.ntcFixedRes);
    }
    if (data.presenceMinDropV !== undefined) {
      setField("presenceMinDropV", data.presenceMinDropV);
    }

    const wr = data.wireRes || {};
    for (let i = 1; i <= 10; i++) {
      const key = String(i);
      setField("r" + key.padStart(2, "0") + "ohm", wr[key]);
    }

    updateModelParamFields(data);

  }

  // ========================================================
  // ===============        LOAD CONTROLS        ============
  // ========================================================

  async function loadControls() {
    try {
      const res = await fetch("/load_controls", {
        cache: "no-store",
        headers: cborHeaders(),
      });
      if (!res.ok) throw new Error("HTTP " + res.status);
      const data = await readCbor(res, {});

      console.log("Fetched /load_controls:", data);

      lastLoadedControls = data;

      // LT toggle
      const ltToggle = document.getElementById("ltToggle");
      if (ltToggle) ltToggle.checked = !!data.ledFeedback;

      updateModePills();

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
      if (data.deviceId !== undefined) setField("userDeviceId", data.deviceId);
      // Always clear credential fields on load to avoid autofill showing deviceId.
      setField("userCurrentPassword", "");
      setField("userNewPassword", "");
      const fanSlider = document.getElementById("fanSlider");
      if (fanSlider && typeof data.fanSpeed === "number") {
        fanSlider.value = data.fanSpeed;
        setFanSpeedValue(data.fanSpeed);
      }
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
        setField(
          "floorMaterial",
          floorMaterialFromCode(data.floorMaterialCode)
        );
      } else {
        setField("floorMaterial", "wood");
      }
      if (data.floorMaxC !== undefined) {
        setField("floorMaxC", data.floorMaxC);
      }
      if (data.floorSwitchMarginC !== undefined) {
        setField("floorSwitchMarginC", data.floorSwitchMarginC);
      }
      if (data.currentSource !== undefined) {
        setField("currentSource", data.currentSource);
      }
    if (data.nichromeFinalTempC !== undefined) {
      setField("nichromeFinalTempC", data.nichromeFinalTempC);
      const maxC = Number(data.nichromeFinalTempC);
      const tgt = document.getElementById("wireTestTargetC");
      if (tgt && Number.isFinite(maxC) && maxC > 0) {
        tgt.max = String(maxC);
        const cur = parseFloat(tgt.value);
        if (!Number.isFinite(cur) || cur > maxC) {
          tgt.value = maxC.toFixed(1);
        }
      }
    }
    if (data.ntcGateIndex !== undefined) {
      setField("ntcGateIndex", data.ntcGateIndex);
    }
      if (data.ntcBeta !== undefined) {
        setField("ntcBeta", data.ntcBeta);
      }
      if (data.ntcT0C !== undefined) {
        setField("ntcT0C", data.ntcT0C);
      }
      if (data.ntcR0 !== undefined) {
        setField("ntcR0", data.ntcR0);
      }
      if (data.ntcFixedRes !== undefined) {
        setField("ntcFixedRes", data.ntcFixedRes);
      }
      if (data.presenceMinDropV !== undefined) {
        setField("presenceMinDropV", data.presenceMinDropV);
      }
      if (data.ntcCalTargetC !== undefined) {
        setField("ntcCalTargetC", data.ntcCalTargetC);
      }
      if (data.ntcCalSampleMs !== undefined) {
        setField("ntcCalSampleMs", data.ntcCalSampleMs);
      }
      if (data.ntcCalTimeoutMs !== undefined) {
        setField("ntcCalTimeoutMs", data.ntcCalTimeoutMs);
      }
      if (data.floorCalibrated !== undefined) {
        setText("floorCalDoneText", data.floorCalibrated ? "Yes" : "No");
      }
      if (data.floorTau !== undefined) {
        setText("floorCalTauText", formatValue(data.floorTau, 2));
      }
      if (data.floorK !== undefined) {
        setText("floorCalKText", formatValue(data.floorK, 3));
      }
      if (data.floorC !== undefined) {
        setText("floorCalCText", formatValue(data.floorC, 2));
      }
      const floorTarget = document.getElementById("floorCalTargetC");
      if (floorTarget && !floorTarget.value && data.floorMaxC !== undefined) {
        floorTarget.value = data.floorMaxC;
      }
      const floorWire = document.getElementById("floorCalWireIndex");
      if (floorWire && !floorWire.value && data.ntcGateIndex !== undefined) {
        floorWire.value = data.ntcGateIndex;
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
      updateModelParamFields(data);

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

      setField("wifiSSID", "");
      setField("wifiPassword", "");
      setField("apSSID", "");
      setField("apPassword", "");

      if (data.setupRunAllowed !== undefined) {
        const configOk =
          data.setupConfigOk !== undefined
            ? !!data.setupConfigOk
            : setupConfigOk;
        const calibOk =
          data.setupCalibOk !== undefined
            ? !!data.setupCalibOk
            : setupCalibOk;
        setupConfigOk = configOk;
        setupCalibOk = calibOk;
        setupRunAllowed = !!data.setupRunAllowed && calibOk;
        updateSetupBanner({
          setupDone: data.setupDone,
          configOk,
          calibOk,
          missingConfig: lastSetupStatus ? lastSetupStatus.missingConfig : [],
          missingCalib: lastSetupStatus ? lastSetupStatus.missingCalib : [],
        });
        setUserTabEnabled(!!data.setupDone);
      }

      if (document.getElementById("setupMissingConfigList")) {
        fetchSetupStatus();
      }

      renderLiveControlChart();
      applySafetyLocks();
    } catch (err) {
      console.error("Failed to load controls:", err);
    }
  }

  async function fetchSetupStatus() {
    try {
      const res = await fetch("/setup_status", {
        cache: "no-store",
        headers: cborHeaders(),
      });
      if (!res.ok) throw new Error("HTTP " + res.status);
      const data = await readCbor(res, {});
      updateSetupUiFromStatus(data);
      return data;
    } catch (err) {
      console.warn("Failed to load setup status:", err);
      return null;
    }
  }

  async function updateSetupStatus(payload) {
    try {
      const res = await fetch("/setup_update", {
        method: "POST",
        headers: cborHeaders(),
        body: encodeCbor(payload || {}),
      });
      const data = await readCbor(res, {});
      if (!res.ok || (data && data.error)) {
        const msg = (data && data.error) || `HTTP ${res.status}`;
        openAlert("Setup Wizard", msg, "danger");
        return;
      }
      await fetchSetupStatus();
    } catch (err) {
      console.error("Setup update failed:", err);
      openAlert("Setup Wizard", "Update failed.", "danger");
    }
  }

  async function resetSetupWizard() {
    const payload = {
      clear_models: !!getBool("setupResetClearModels"),
      clear_wire_params: !!getBool("setupResetClearWire"),
      clear_floor_params: !!getBool("setupResetClearFloor"),
    };
    try {
      const res = await fetch("/setup_reset", {
        method: "POST",
        headers: cborHeaders(),
        body: encodeCbor(payload),
      });
      const data = await readCbor(res, {});
      if (!res.ok || (data && data.error)) {
        const msg = (data && data.error) || `HTTP ${res.status}`;
        openAlert("Setup Wizard", msg, "danger");
        return;
      }
      await fetchSetupStatus();
      resetWizardSkipped();
    } catch (err) {
      console.error("Setup reset failed:", err);
      openAlert("Setup Wizard", "Reset failed.", "danger");
    }
  }

  async function loadDeviceInfo() {
    try {
      const res = await fetch("/device_info", {
        cache: "no-store",
        headers: cborHeaders(),
      });
      if (!res.ok) throw new Error("HTTP " + res.status);
      const data = await readCbor(res, {});
      setText("deviceInfoId", data.deviceId || "--");
      setText("deviceInfoHw", data.hw || "--");
      setText("deviceInfoSw", data.sw || "--");
    } catch (err) {
      console.warn("Failed to load device info:", err);
      setText("deviceInfoId", "--");
      setText("deviceInfoHw", "--");
      setText("deviceInfoSw", "--");
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
    if (
      guardUnsafeAction("saving user settings", {
        blockAuto: true,
        blockCalib: true,
      })
    ) {
      return;
    }
    const current = document.getElementById("userCurrentPassword").value;
    const newPass = document.getElementById("userNewPassword").value;
    const newId = document.getElementById("userDeviceId").value;

    const hasChanges =
      String(newPass || "").length > 0 || String(newId || "").trim().length > 0;
    if (!hasChanges) {
      openAlert("User Settings", "Nothing to update.", "warning");
      return;
    }

    const doSave = () => {
      sendControlCommand("set", "userCredentials", {
        current,
        newPass,
        newId,
      }).then((res) => {
        if (res && !res.error) {
          disconnectDevice();
        } else if (res && res.error) {
          openAlert("User Settings", res.error, "danger");
        }
      });
    };

    openConfirm("sessionChange", doSave);
  }

  function resetUserSettings() {
    setField("userCurrentPassword", "");
    setField("userNewPassword", "");
    if (lastLoadedControls && lastLoadedControls.deviceId !== undefined) {
      setField("userDeviceId", lastLoadedControls.deviceId);
    } else {
      setField("userDeviceId", "");
    }
  }

  function saveAdminSettings(scope = "all") {
    if (
      guardUnsafeAction("saving admin settings", {
        blockAuto: true,
        blockCalib: true,
      })
    ) {
      return;
    }
    const section = scope === "admin" || scope === "wifi" ? scope : "all";

    const current = (document.getElementById("adminCurrentPassword") || {})
      .value;
    const username = (document.getElementById("adminUsername") || {}).value;
    const password = (document.getElementById("adminPassword") || {}).value;
    const wifiSSID = (document.getElementById("wifiSSID") || {}).value;
    const wifiPassword = (document.getElementById("wifiPassword") || {}).value;

    const payload = {};
    const trimmedCurrent = String(current || "").trim();
    if (trimmedCurrent) payload.current = trimmedCurrent;

    let hasChanges = false;
    if (section === "admin" || section === "all") {
      const u = String(username || "").trim();
      const p = String(password || "");
      if (u) {
        payload.username = u;
        hasChanges = true;
      }
      if (p) {
        payload.password = p;
        hasChanges = true;
      }
    }
    if (section === "wifi" || section === "all") {
      const ssid = String(wifiSSID || "").trim();
      const pw = String(wifiPassword || "");
      if (ssid) {
        payload.wifiSSID = ssid;
        hasChanges = true;
      }
      if (pw) {
        payload.wifiPassword = pw;
        hasChanges = true;
      }
    }

    if (!hasChanges) {
      openAlert("Admin Settings", "Nothing to update.", "warning");
      return;
    }

    if ((payload.username || payload.password) && !payload.current) {
      openAlert("Admin Settings", "Current password is required.", "warning");
      return;
    }

    const adminChanged = !!(payload.username || payload.password);
    const wifiChanged = !!(payload.wifiSSID || payload.wifiPassword);

    const doSave = () => {
      sendControlCommand("set", "adminCredentials", payload).then((res) => {
        if (res && !res.error) {
          resetAdminSettings(section);
          if (adminChanged) {
            disconnectDevice();
            return;
          }
          if (wifiChanged) {
            fetchSetupStatus();
            openAlert(
              "Wi-Fi Settings",
              "Wi-Fi settings saved. Restarting device...",
              "success"
            );
            return;
          }
          fetchSetupStatus();
          openAlert("Admin Settings", "Admin settings updated.", "success");
        } else if (res && res.error) {
          openAlert("Admin Settings", res.error, "danger");
        }
      });
    };

    if (adminChanged && wifiChanged) {
      openConfirm("sessionWifiChange", doSave);
      return;
    }
    if (adminChanged) {
      openConfirm("sessionChange", doSave);
      return;
    }
    if (wifiChanged) {
      openConfirm("wifiRestart", doSave);
      return;
    }

    doSave();
  }

  function resetAdminSettings(scope = "all") {
    const section = scope === "admin" || scope === "wifi" ? scope : "all";
    setField("adminCurrentPassword", "");

    if (section === "admin" || section === "all") {
      setField("adminUsername", "");
      setField("adminPassword", "");
    }

    if (section === "wifi" || section === "all") {
      setField("wifiSSID", "");
      setField("wifiPassword", "");
    }
  }

  function resetApSettings() {
    setField("apSSID", "");
    setField("apPassword", "");
  }

  function saveApSettings() {
    if (
      guardUnsafeAction("saving AP settings", {
        blockAuto: true,
        blockCalib: true,
      })
    ) {
      return;
    }

    const ssid = String(
      (document.getElementById("apSSID") || {}).value || ""
    ).trim();
    const pw = String(
      (document.getElementById("apPassword") || {}).value || ""
    );

    const payload = {};
    if (ssid) payload.apSSID = ssid;
    if (pw) payload.apPassword = pw;

    if (!payload.apSSID && !payload.apPassword) {
      openAlert("AP Settings", "Nothing to update.", "warning");
      return;
    }

    const doSave = async () => {
      try {
        const res = await fetch("/ap_config", {
          method: "POST",
          headers: cborHeaders(),
          body: encodeCbor(payload),
        });
        const data = await readCbor(res, {});
        if (!res.ok || (data && data.error)) {
          openAlert("AP Settings", data.error || "Update failed.", "danger");
          return;
        }
        resetApSettings();
        fetchSetupStatus();
        openAlert(
          "AP Settings",
          "AP settings saved. Restarting device...",
          "success"
        );
      } catch (err) {
        console.error("AP settings update failed:", err);
        openAlert("AP Settings", "Update failed.", "danger");
      }
    };

    openConfirm("wifiRestart", doSave);
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
        layout: "L", // 90 deg path
      },
      // Relay (yellow) bottom-right
      {
        id: "relay",
        color: "yellow",
        x: 92 - h,
        y: 81 + offset,
        tx: 65, // where you want it to touch near live-core
        ty: 65,
        layout: "L", // 90 deg path
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

  async function fetchMonitorSnapshot() {
    const res = await fetch("/monitor", {
      cache: "no-store",
      headers: cborHeaders(),
    });
    if (res.status === 401) {
      noteAuthFailure();
      return null;
    }
    if (!res.ok) return null;
    resetAuthFailures();
    return await readCbor(res, null);
  }

  function readAcsCurrent(mon) {
    if (!mon) return NaN;
    const acs = Number(
      mon.currentAcs !== undefined
        ? mon.currentAcs
        : mon.current_acs !== undefined
        ? mon.current_acs
        : mon.currentACS
    );
    if (Number.isFinite(acs)) return acs;
    const fallback = Number(mon.current);
    return Number.isFinite(fallback) ? fallback : NaN;
  }

    function applyMonitorSnapshot(mon) {
      if (!mon) return;
      lastMonitor = mon;
      updateAmbientWaitFromMonitor(mon.ambientWait);
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
        totalEnergyEl.textContent = (t.totalEnergy_Wh || 0).toFixed(2) + " Wh";
      if (totalSessionsEl)
        totalSessionsEl.textContent = (t.totalSessions || 0).toString();
      if (totalOkEl)
        totalOkEl.textContent = (t.totalSessionsOk || 0).toString();
    }

    const outs = mon.outputs || {};
    for (let i = 1; i <= 10; i++) {
      setDot("o" + i, !!outs["output" + i]);
    }
    for (let i = 1; i <= 10; i++) {
      const itemSel = "#manualOutputs .manual-item:nth-child(" + i + ")";
      const checkbox = document.querySelector(
        itemSel + ' input[type="checkbox"]'
      );
      const led = document.querySelector(itemSel + " .led");
      const on = !!outs["output" + i];
      if (checkbox) checkbox.checked = on;
      if (led) led.classList.toggle("active", on);
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
            "Wire " + cfg.wire + ": " + t.toFixed(1) + "degC"
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

    // Voltage gauge (always show measured bus voltage)
    const v = Number(mon.capVoltage);
    if (Number.isFinite(v)) {
      updateGauge("voltageValue", v, "V", 400);
    } else {
      setGaugeUnknown("voltageValue");
    }

    // Raw ADC display (scaled /100, e.g., 4095 -> 40.95)
    const adcEl = document.getElementById("adcRawValue");
    if (adcEl) {
      const rawScaled = parseFloat(mon.capAdcRaw);
      adcEl.textContent = Number.isFinite(rawScaled)
        ? rawScaled.toFixed(2)
        : "--";
    }

    // Current gauge
    let rawCurrent = readAcsCurrent(mon);
    if (!ac || !Number.isFinite(rawCurrent)) rawCurrent = 0;
    rawCurrent = Math.max(0, Math.min(100, rawCurrent));
    updateGauge("currentValue", rawCurrent, "A", 100);

    // Temperatures (up to 12 sensors)
    const temps = mon.temperatures || [];
    for (let i = 0; i < 12; i++) {
      const id = "temp" + (i + 1) + "Value";
      const t = temps[i];
      const num = Number(t);
      if (t === undefined || t === null || Number.isNaN(num) || num === -127) {
        updateGauge(id, "Off", "\u00B0C", 150);
      } else {
        updateGauge(id, num, "\u00B0C", 150);
      }
    }

    // Capacitance (F -> mF)
    const capF = parseFloat(mon.capacitanceF);
    renderCapacitance(capF);

    // Ready / Off LEDs
    const readyLed = document.getElementById("readyLed");
    const offLed = document.getElementById("offLed");
    if (readyLed)
      readyLed.style.backgroundColor = mon.ready ? "limegreen" : "gray";
    if (offLed) offLed.style.backgroundColor = mon.off ? "red" : "gray";

    // Fan slider reflect
    const fanSlider = document.getElementById("fanSlider");
    if (fanSlider && typeof mon.fanSpeed === "number") {
      fanSlider.value = mon.fanSpeed;
      setFanSpeedValue(mon.fanSpeed);
    }

    if (mon.eventUnread) {
      handleEventUnreadUpdate(mon.eventUnread);
    }

    updateWifiSignal(mon);
    updateTopTemps(mon);
    updateTopPower(mon);
  }

  async function waitForMonitorMatch(predicate, opts = {}) {
    const timeoutMs = opts.timeoutMs || 1500;
    const intervalMs = opts.intervalMs || 120;
    const start = Date.now();
    let last = null;
    while (Date.now() - start < timeoutMs) {
      const mon = await fetchMonitorSnapshot();
      if (mon) {
        last = mon;
        if (predicate(mon)) return mon;
      }
      await sleep(intervalMs);
    }
    return last;
  }

  async function pollLiveOnce() {
    try {
      const mon = await fetchMonitorSnapshot();
      if (!mon) return;
      applyMonitorSnapshot(mon);
      pushLiveControlSample(mon);
      if (isLiveControlOpen()) {
        renderLiveControlChart();
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
            "Wire " + cfg.wire + ": " + Number(t).toFixed(1) + "\u00B0C"
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
    //  - "L" / "Lh"  => 90 deg with horizontal-then-vertical
    //  - "Lv"        => 90 deg with vertical-then-horizontal
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

    eEl.textContent = (session.energy_Wh || 0).toFixed(2) + " Wh";
    dEl.textContent = (session.duration_s || 0).toString() + " s";
    pWEl.textContent = (session.peakPower_W || 0).toFixed(1) + " W";
    pAEl.textContent = (session.peakCurrent_A || 0).toFixed(2) + " A";
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

  // Error modal controls
  function openErrorModal() {
    const m = document.getElementById("errorModal");
    if (!m) return;
    m.classList.add("show");
  }

  function closeErrorModal() {
    const m = document.getElementById("errorModal");
    if (!m) return;
    m.classList.remove("show");
  }

  function updateEventBadge(warnCount, errCount) {
    const badge = document.getElementById("eventBadge");
    if (!badge) return;
    const w = Math.max(0, Number(warnCount) || 0);
    const e = Math.max(0, Number(errCount) || 0);
    const warnEl = document.getElementById("eventWarnCount");
    const errEl = document.getElementById("eventErrCount");
    if (warnEl) warnEl.textContent = String(w);
    if (errEl) errEl.textContent = String(e);
    if (!warnEl || !errEl) {
      badge.textContent = "warn " + w + " err " + e;
    }
    badge.classList.toggle("active", w > 0 || e > 0);
  }

  function showEventToast(kind, reason) {
    const toast = document.getElementById("eventToast");
    const kindEl = document.getElementById("eventToastKind");
    const reasonEl = document.getElementById("eventToastReason");
    if (!toast) return;
    const isError = kind === "error";
    toast.classList.remove("warn", "err");
    toast.classList.add("show");
    toast.classList.add(isError ? "err" : "warn");
    if (kindEl) kindEl.textContent = isError ? "ERROR" : "WARNING";
    if (reasonEl)
      reasonEl.textContent =
        reason || (isError ? "New error detected." : "New warning detected.");
    lastEventToastKind = isError ? "error" : "warning";
    eventToastVisible = true;
  }

  function hideEventToast() {
    const toast = document.getElementById("eventToast");
    if (!toast) return;
    toast.classList.remove("show", "warn", "err");
    eventToastVisible = false;
    lastEventToastKind = null;
  }

  async function fetchLastEvent(markRead) {
    const url = markRead ? "/last_event?mark_read=1" : "/last_event";
    const res = await fetch(url, { cache: "no-store", headers: cborHeaders() });
    if (res.status === 401) {
      noteAuthFailure();
      throw new Error("HTTP 401");
    }
    if (!res.ok) throw new Error("HTTP " + res.status);
    resetAuthFailures();
    return readCbor(res, {});
  }

  function applyUnreadCounts(warnCount, errCount, forceToast) {
    const w = Math.max(0, Number(warnCount) || 0);
    const e = Math.max(0, Number(errCount) || 0);
    const hasUnread = w > 0 || e > 0;
    const hasNew = w > lastEventUnread.warn || e > lastEventUnread.error;
    lastEventUnread = { warn: w, error: e };
    updateEventBadge(w, e);
    if (!hasUnread) {
      hideEventToast();
      return false;
    }
    return hasNew || forceToast;
  }

  function showEventToastFromPayload(data, warnCount, errCount) {
    const hasError = errCount > 0;
    let reason = "";
    if (hasError) {
      reason =
        (data && data.last_error && data.last_error.reason) ||
        (data && data.errors && data.errors[0] && data.errors[0].reason) ||
        "";
      showEventToast("error", reason);
    } else {
      reason =
        (data && data.last_warning && data.last_warning.reason) ||
        (data && data.warnings && data.warnings[0] && data.warnings[0].reason) ||
        "";
      showEventToast("warning", reason);
    }
  }

  function handleEventNotice(notice) {
    if (!notice) return;
    const unread = notice.unread || {};
    const w = Math.max(0, Number(unread.warn) || 0);
    const e = Math.max(0, Number(unread.error) || 0);
    if (!applyUnreadCounts(w, e, false)) return;
    const kind =
      notice.kind === "warning"
        ? "warning"
        : notice.kind === "error"
        ? "error"
        : e > 0
        ? "error"
        : "warning";
    const reason =
      notice.reason ||
      (kind === "error" &&
        notice.last_error &&
        notice.last_error.reason) ||
      (kind === "warning" &&
        notice.last_warning &&
        notice.last_warning.reason) ||
      "";
    showEventToast(kind, reason);
  }

  async function handleEventUnreadUpdate(unread) {
    if (!unread) return;
    const w = Math.max(0, Number(unread.warn) || 0);
    const e = Math.max(0, Number(unread.error) || 0);
    const shouldToast = applyUnreadCounts(w, e, !eventToastVisible);
    if (!shouldToast) return;
    try {
      const data = await fetchLastEvent(false);
      showEventToastFromPayload(data, w, e);
    } catch (err) {
      const kind = e > 0 ? "error" : "warning";
      showEventToast(kind, "");
    }
  }

  function rssiToBars(rssi) {
    if (!Number.isFinite(rssi)) return 0;
    if (rssi >= -55) return 4;
    if (rssi >= -67) return 3;
    if (rssi >= -75) return 2;
    if (rssi >= -85) return 1;
    return 0;
  }

  function updateWifiSignal(mon) {
    const wrap = document.getElementById("wifiSignal");
    const icon = document.getElementById("wifiSignalIcon");
    if (!wrap || !icon) return;
    if (!mon || !mon.wifiSta) {
      wrap.style.display = "none";
      return;
    }
    const connected = mon.wifiConnected !== false;
    const rssi = Number(mon.wifiRssi);
    const bars = connected ? rssiToBars(rssi) : 0;
    icon.src = "icons/wifi-" + bars + "-bars.png";
    wrap.style.display = "inline-flex";
    if (!connected) {
      wrap.title = "WiFi not connected";
    } else {
      wrap.title = Number.isFinite(rssi)
        ? "WiFi signal (" + rssi + " dBm)"
        : "WiFi signal";
    }
  }

  function formatTopTemp(val) {
    if (typeof val === "number" && val > -100) {
      return Math.round(val) + "\u00B0C";
    }
    return "Off";
  }

  function updateTopTemps(mon) {
    const boardEl = document.getElementById("boardTempText");
    const hsEl = document.getElementById("heatsinkTempText");
    if (!boardEl || !hsEl) return;
    if (!mon) return;
    boardEl.textContent = formatTopTemp(mon.boardTemp);
    hsEl.textContent = formatTopTemp(mon.heatsinkTemp);
  }

  function updateTopPower(mon) {
    const vEl = document.getElementById("dcVoltageText");
    const iEl = document.getElementById("dcCurrentText");
    if (!vEl || !iEl) return;
    if (!mon) return;
    const v = Number(mon.capVoltage);
    const i = readAcsCurrent(mon);
    vEl.textContent = Number.isFinite(v) ? v.toFixed(1) + "V" : "--V";
    iEl.textContent = Number.isFinite(i) ? i.toFixed(2) + "A" : "--A";
  }

  function bindErrorButton() {
    const btn = document.getElementById("errorBtn");
    if (!btn) return;
    btn.addEventListener("click", loadLastEventAndOpen);
  }

  function openLogModal() {
    const m = document.getElementById("logModal");
    if (!m) return;
    m.classList.add("show");
  }

  function closeLogModal() {
    const m = document.getElementById("logModal");
    if (!m) return;
    m.classList.remove("show");
  }

  async function loadDeviceLog() {
    const body = document.getElementById("logContent");
    if (!body) return;
    body.textContent = "Loading...";
    try {
      const res = await fetch("/device_log", { cache: "no-store" });
      if (!res.ok) {
        body.textContent = "Failed to load log (" + res.status + ")";
        return;
      }
      const text = await res.text();
      body.textContent = text && text.trim().length ? text : "No log data.";
    } catch (err) {
      body.textContent = "Failed to load log.";
      console.error("Log load failed:", err);
    }
  }

  async function clearDeviceLog() {
    const body = document.getElementById("logContent");
    if (body) body.textContent = "Clearing...";
    try {
      const res = await fetch("/device_log_clear", {
        method: "POST",
      });
      if (!res.ok) {
        if (body) body.textContent = "Clear failed (" + res.status + ")";
        return;
      }
      await loadDeviceLog();
    } catch (err) {
      if (body) body.textContent = "Clear failed.";
      console.error("Log clear failed:", err);
    }
  }

  function bindLogButton() {
    const btn = document.getElementById("logBtn");
    if (btn) {
      btn.addEventListener("click", async () => {
        openLogModal();
        await loadDeviceLog();
      });
    }
    const refreshBtn = document.getElementById("logRefreshBtn");
    if (refreshBtn) {
      refreshBtn.addEventListener("click", loadDeviceLog);
    }
    const clearBtn = document.getElementById("logClearBtn");
    if (clearBtn) {
      clearBtn.addEventListener("click", clearDeviceLog);
    }

    const modal = document.getElementById("logModal");
    if (modal) {
      const closeBtn = modal.querySelector(".log-close");
      const backdrop = modal.querySelector(".log-backdrop");
      if (closeBtn) closeBtn.addEventListener("click", closeLogModal);
      if (backdrop) backdrop.addEventListener("click", closeLogModal);
    }
  }

  // Expose for inline onclick handlers in HTML.
  window.closeLogModal = closeLogModal;
  window.openLogModal = openLogModal;

  function bindEventBadge() {
    const warnBtn = document.getElementById("eventWarnBtn");
    const errBtn = document.getElementById("eventErrBtn");
    if (warnBtn) {
      warnBtn.addEventListener("click", () => loadLastEventAndOpen("warning"));
      warnBtn.addEventListener("keydown", (e) => {
        if (e.key === "Enter" || e.key === " ") {
          e.preventDefault();
          loadLastEventAndOpen("warning");
        }
      });
    }
    if (errBtn) {
      errBtn.addEventListener("click", () => loadLastEventAndOpen("error"));
      errBtn.addEventListener("keydown", (e) => {
        if (e.key === "Enter" || e.key === " ") {
          e.preventDefault();
          loadLastEventAndOpen("error");
        }
      });
    }
  }

  function bindEventToast() {
    const toast = document.getElementById("eventToast");
    const viewBtn = document.getElementById("eventToastViewBtn");
    const openToast = () => {
      const focus =
        lastEventUnread.error > 0 || lastEventToastKind === "error"
          ? "error"
          : "warning";
      loadLastEventAndOpen(focus, { markRead: true });
    };
    if (viewBtn) viewBtn.addEventListener("click", openToast);
    if (toast) {
      toast.addEventListener("click", (e) => {
        if (e.target === viewBtn) return;
        openToast();
      });
    }
  }

  function formatEventTime(epochSec, ms) {
    if (epochSec) return formatEpochLocal(epochSec);
    if (ms) return fmtUptime(ms);
    return "--";
  }

  function renderEventList(listId, events, emptyText, kindLabel) {
    const list = document.getElementById(listId);
    if (!list) return;
    list.innerHTML = "";
    if (!Array.isArray(events) || events.length === 0) {
      const empty = document.createElement("div");
      empty.className = "error-empty";
      empty.textContent = emptyText || "No events logged yet.";
      list.appendChild(empty);
      return;
    }

    events.forEach((ev) => {
      const item = document.createElement("div");
      item.className =
        "event-item " + (kindLabel === "Warning" ? "warning" : "error");

      const kindEl = document.createElement("span");
      kindEl.className = "event-kind";
      kindEl.textContent = kindLabel;

      const reasonEl = document.createElement("span");
      reasonEl.className = "event-reason";
      reasonEl.textContent = ev.reason || "--";

      const timeEl = document.createElement("span");
      timeEl.className = "event-time";
      timeEl.textContent = formatEventTime(ev.epoch, ev.ms);

      item.appendChild(kindEl);
      item.appendChild(reasonEl);
      item.appendChild(timeEl);
      list.appendChild(item);
    });
  }

  function focusEventSection(sectionId) {
    const section = document.getElementById(sectionId);
    if (!section) return;
    section.scrollIntoView({ behavior: "smooth", block: "start" });
  }

  function setEventView(mode) {
    const modal = document.getElementById("errorModal");
    const title = document.getElementById("eventModalTitle");
    const warnSection = document.getElementById("warningHistorySection");
    const errSection = document.getElementById("errorHistorySection");
    const lastErrorSection = document.getElementById("lastErrorSection");
    const lastStopSection = document.getElementById("lastStopSection");
    const stateRow = document.getElementById("errorStateRow");
    if (!modal) return;
    modal.classList.remove("view-warning", "view-error");
    if (mode === "warning") {
      modal.classList.add("view-warning");
      if (title) title.textContent = "Warnings";
      if (warnSection) warnSection.style.display = "";
      if (errSection) errSection.style.display = "none";
      if (lastErrorSection) lastErrorSection.style.display = "none";
      if (lastStopSection) lastStopSection.style.display = "none";
      if (stateRow) stateRow.style.display = "none";
    } else if (mode === "error") {
      modal.classList.add("view-error");
      if (title) title.textContent = "Errors";
      if (warnSection) warnSection.style.display = "none";
      if (errSection) errSection.style.display = "";
      if (lastErrorSection) lastErrorSection.style.display = "";
      if (lastStopSection) lastStopSection.style.display = "none";
      if (stateRow) stateRow.style.display = "";
    } else {
      if (title) title.textContent = "Last Stop / Error";
      if (warnSection) warnSection.style.display = "";
      if (errSection) errSection.style.display = "";
      if (lastErrorSection) lastErrorSection.style.display = "";
      if (lastStopSection) lastStopSection.style.display = "";
      if (stateRow) stateRow.style.display = "";
    }
  }

  async function loadLastEventAndOpen(focus, opts = {}) {
    const markRead = opts.markRead !== false;
    setEventView(focus || "all");
    try {
      const data = await fetchLastEvent(markRead);
      const stateEl = document.getElementById("errorStateText");
      if (stateEl) stateEl.textContent = data.state || "--";

      const err = data.last_error || {};
      const stop = data.last_stop || {};

      const errReason = document.getElementById("errorReasonText");
      if (errReason) errReason.textContent = err.reason || "--";
      const errTime = document.getElementById("errorTimeText");
      if (errTime) errTime.textContent = formatEventTime(err.epoch, err.ms);

      const stopReason = document.getElementById("stopReasonText");
      if (stopReason) stopReason.textContent = stop.reason || "--";
      const stopTime = document.getElementById("stopTimeText");
      if (stopTime) stopTime.textContent = formatEventTime(stop.epoch, stop.ms);

      renderEventList(
        "warningList",
        data.warnings || [],
        "No warnings logged yet.",
        "Warning"
      );
      renderEventList(
        "errorList",
        data.errors || [],
        "No errors logged yet.",
        "Error"
      );
      if (data.unread) {
        applyUnreadCounts(data.unread.warn, data.unread.error, false);
      } else {
        applyUnreadCounts(0, 0, false);
      }
      if (markRead) hideEventToast();

      openErrorModal();
      if (focus === "warning") focusEventSection("warningHistorySection");
      else if (focus === "error") focusEventSection("errorHistorySection");
    } catch (err) {
      console.error("Last event load failed", err);
      setEventView(focus || "all");
      openErrorModal();
    }
  }

  async function loadSessionHistoryAndOpen() {
    try {
      // Load from SPIFFS/static file
      const res = await fetch("/History.cbor", {
        cache: "no-store",
        headers: cborHeaders(),
      });
      if (!res.ok) {
        console.error("Failed to load History.cbor:", res.status);
        openSessionHistory(); // Show empty modal instead of doing nothing
        return;
      }

      const data = await readCbor(res, {});

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

      // Open the modal (this is the correct function)
      openSessionHistory();
    } catch (e) {
      console.error("Session history load failed", e);
    }
  }

  // Calibration modal controls
  let calibPollTimer = null;
  let calibPollIntervalMs = 1000;
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
    mountOverlayInsideUiContainer("errorModal");
    mountOverlayInsideUiContainer("logModal");
    mountOverlayInsideUiContainer("liveControlModal");
    const shClose = document.querySelector(
      "#sessionHistoryModal .session-history-close"
    );
    if (shClose) shClose.textContent = "x";
  }

  function syncCalibrationInputsFromControls(data) {
    if (!data) return;
    if (data.ntcCalTargetC !== undefined) {
      setField("ntcCalTargetC", data.ntcCalTargetC);
    }
    if (data.ntcCalSampleMs !== undefined) {
      setField("ntcCalSampleMs", data.ntcCalSampleMs);
    }
    if (data.ntcCalTimeoutMs !== undefined) {
      setField("ntcCalTimeoutMs", data.ntcCalTimeoutMs);
    }
    if (data.floorMaxC !== undefined) {
      const floorTarget = document.getElementById("floorCalTargetC");
      if (floorTarget && !floorTarget.value) {
        floorTarget.value = data.floorMaxC;
      }
    }
    if (data.ntcGateIndex !== undefined) {
      const floorWire = document.getElementById("floorCalWireIndex");
      if (floorWire && !floorWire.value) {
        floorWire.value = data.ntcGateIndex;
      }
    }
    const duty = document.getElementById("floorCalDutyPct");
    if (duty && !duty.value) duty.value = "50";
    const ambient = document.getElementById("floorCalAmbientMin");
    if (ambient && !ambient.value) ambient.value = "5";
    const heat = document.getElementById("floorCalHeatMin");
    if (heat && !heat.value) heat.value = "20";
    const cool = document.getElementById("floorCalCoolMin");
    if (cool && !cool.value) cool.value = "10";
    const interval = document.getElementById("floorCalIntervalMs");
    if (interval && !interval.value) interval.value = "1000";

    if (data.floorCalibrated !== undefined) {
      setText("floorCalDoneText", data.floorCalibrated ? "Yes" : "No");
    }
    if (data.floorTau !== undefined) {
      setText("floorCalTauText", formatValue(data.floorTau, 2));
    }
    if (data.floorK !== undefined) {
      setText("floorCalKText", formatValue(data.floorK, 3));
    }
    if (data.floorC !== undefined) {
      setText("floorCalCText", formatValue(data.floorC, 2));
    }
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
    if (lastLoadedControls) {
      syncCalibrationInputsFromControls(lastLoadedControls);
    }
    initCalibrationChartUi();
    startCalibrationPoll();
    startNtcCalPoll();
    startWireTestPoll();
    refreshCalibHistoryList(true);
  }

  function closeCalibrationModal() {
    const m = document.getElementById("calibrationModal");
    if (m) m.classList.remove("show");
    calibChartPaused = false;
    setCalibrationInfoVisible(false);
    stopCalibrationPoll();
    stopNtcCalPoll();
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
      infoClose.addEventListener("click", () =>
        setCalibrationInfoVisible(false)
      );
    }
    const latestBtn = document.getElementById("calibLatestBtn");
    if (latestBtn) {
      latestBtn.addEventListener("click", () => scrollCalibToLatest());
    }
    const pauseBtn = document.getElementById("calibPauseBtn");
    if (pauseBtn) {
      pauseBtn.addEventListener("click", toggleCalibPause);
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

    const historyBtn = document.getElementById("calibHistoryBtn");
    if (historyBtn) {
      historyBtn.addEventListener("click", toggleCalibrationHistory);
    }

    const topBtn = document.getElementById("topCalibBtn");
    if (topBtn) {
      topBtn.addEventListener("click", openCalibrationModal);
    }

    const historyLoadBtn = document.getElementById("calibHistoryLoadBtn");
    if (historyLoadBtn) {
      historyLoadBtn.addEventListener("click", loadSavedCalibration);
    }

    const historyRefreshBtn = document.getElementById("calibHistoryRefreshBtn");
    if (historyRefreshBtn) {
      historyRefreshBtn.addEventListener("click", () =>
        refreshCalibHistoryList(true)
      );
    }

    const clearBtn = document.getElementById("calibClearBtn");
    if (clearBtn) {
      clearBtn.addEventListener("click", clearCalibrationData);
    }

    const ntcStart = document.getElementById("ntcCalStartBtn");
    if (ntcStart) {
      ntcStart.addEventListener("click", startNtcCalibration);
    }
    const ntcStop = document.getElementById("ntcCalStopBtn");
    if (ntcStop) {
      ntcStop.addEventListener("click", stopNtcCalibration);
    }
    const ntcBetaBtn = document.getElementById("ntcBetaCalBtn");
    if (ntcBetaBtn) {
      ntcBetaBtn.addEventListener("click", startNtcBetaCalibration);
    }
    const floorBtn = document.getElementById("startFloorCalibBtn");
    if (floorBtn) {
      floorBtn.addEventListener("click", startFloorCalibration);
    }
    const presenceBtn = document.getElementById("presenceProbeBtn");
    if (presenceBtn) {
      presenceBtn.addEventListener("click", runPresenceProbe);
    }
  }

  function bindSetupWizardControls() {
    const updateBtn = document.getElementById("setupUpdateBtn");
    if (updateBtn) {
      updateBtn.addEventListener("click", () => {
        const payload = {};
        const stage = getInt("setupStageInput");
        const substage = getInt("setupSubstageInput");
        const wireIndex = getInt("setupWireIndexInput");
        if (stage !== undefined) payload.stage = stage;
        if (substage !== undefined) payload.substage = substage;
        if (wireIndex !== undefined) payload.wire_index = wireIndex;
        if (!Object.keys(payload).length) {
          openAlert("Setup Wizard", "Enter a stage or wire index.", "warning");
          return;
        }
        updateSetupStatus(payload);
      });
    }

    const doneBtn = document.getElementById("setupDoneBtn");
    if (doneBtn) {
      doneBtn.addEventListener("click", async () => {
        const status = lastSetupStatus || (await fetchSetupStatus());
        if (!status || !status.configOk) {
          openAlert(
            "Setup Wizard",
            "Configuration is incomplete. Please finish required steps.",
            "warning"
          );
          return;
        }
        if (!status.calibOk) {
          openAlert(
            "Setup Wizard",
            "Calibration is incomplete. Finish required calibration steps first.",
            "warning"
          );
          return;
        }
        updateSetupStatus({ setup_done: true });
      });
    }

    const clearBtn = document.getElementById("setupClearDoneBtn");
    if (clearBtn) {
      clearBtn.addEventListener("click", () =>
        updateSetupStatus({ setup_done: false })
      );
    }

    const refreshBtn = document.getElementById("setupRefreshBtn");
    if (refreshBtn) {
      refreshBtn.addEventListener("click", fetchSetupStatus);
    }

    const resetBtn = document.getElementById("setupResetBtn");
    if (resetBtn) {
      resetBtn.addEventListener("click", () =>
        openConfirm("setupReset", resetSetupWizard)
      );
    }

    const bannerOpen = document.getElementById("setupBannerOpenBtn");
    if (bannerOpen) {
      bannerOpen.addEventListener("click", () => {
        if (
          lastSetupStatus &&
          (!lastSetupStatus.setupDone || !lastSetupStatus.configOk)
        ) {
          syncWizardStepFromStatus(lastSetupStatus);
          openSetupWizard();
          return;
        }
        switchTab(3);
        const card = document.querySelector(".setup-wizard-card");
        if (card && card.scrollIntoView) {
          card.scrollIntoView({ block: "center", behavior: "smooth" });
        }
      });
    }

    const bannerCalib = document.getElementById("setupBannerCalibBtn");
    if (bannerCalib) {
      bannerCalib.addEventListener("click", openCalibrationModal);
    }
  }

  document.addEventListener("keydown", (e) => {
    if (e.key === "Escape") {
      setCalibrationInfoVisible(false);
      closeErrorModal();
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

  
  
  
  
  function avgLastTemps(temps, count) {
    let sum = 0;
    let used = 0;
    for (let i = temps.length - 1; i >= 0 && used < count; i--) {
      const t = temps[i];
      if (Number.isFinite(t)) {
        sum += t;
        used++;
      }
    }
    return used ? sum / used : NaN;
  }

  function computeModelFromSamples(samples, fallback) {
    if (!Array.isArray(samples) || samples.length < 5) return null;

    const tms = [];
    const temps = [];
    const powers = [];
    let maxPower = 0;

    for (const s of samples) {
      const t = Number(s.t_ms);
      const temp = Number(s.temp_c);
      const v = Number(s.v);
      const i = Number(s.i);
      tms.push(Number.isFinite(t) ? t : NaN);
      temps.push(Number.isFinite(temp) ? temp : NaN);
      let p = NaN;
      if (Number.isFinite(v) && Number.isFinite(i)) {
        p = v * i;
        if (Number.isFinite(p) && p > maxPower) maxPower = p;
      }
      powers.push(p);
    }

    if (!Number.isFinite(maxPower) || maxPower <= 0) {
      maxPower = Number.isFinite(fallback.max_power_w)
        ? fallback.max_power_w
        : 0;
    }
    const threshold = Math.max(5, maxPower * 0.2);

    let start = 0;
    while (
      start < powers.length &&
      !(Number.isFinite(powers[start]) && powers[start] > threshold)
    ) {
      start++;
    }
    if (start >= powers.length) start = 0;

    let end = -1;
    let lowCount = 0;
    for (let i = start + 1; i < powers.length; i++) {
      if (!Number.isFinite(powers[i]) || powers[i] < threshold) {
        lowCount++;
        if (lowCount >= 3) {
          end = i - 2;
          break;
        }
      } else {
        lowCount = 0;
      }
    }

    let peakIndex = start;
    let peakTemp = -Infinity;
    for (let i = start; i < temps.length; i++) {
      const temp = temps[i];
      if (Number.isFinite(temp) && temp > peakTemp) {
        peakTemp = temp;
        peakIndex = i;
      }
      if (end >= start && i >= end) break;
    }
    if (!Number.isFinite(peakTemp)) return null;
    if (end < start || end > peakIndex) end = peakIndex;

    const ambient = avgLastTemps(temps, 10);
    if (!Number.isFinite(ambient)) return null;

    const deltaT = peakTemp - ambient;
    if (!Number.isFinite(deltaT) || deltaT <= 1.0) return null;

    const tStartMs = Number.isFinite(tms[start]) ? tms[start] : 0;
    const tPeakMs = Number.isFinite(tms[peakIndex]) ? tms[peakIndex] : tStartMs;

    const t63 = ambient + 0.632 * deltaT;
    let t63Ms = NaN;
    for (let i = start; i <= peakIndex; i++) {
      if (
        Number.isFinite(temps[i]) &&
        temps[i] >= t63 &&
        Number.isFinite(tms[i])
      ) {
        t63Ms = tms[i];
        break;
      }
    }

    let tauSec = NaN;
    if (Number.isFinite(t63Ms) && t63Ms > tStartMs) {
      tauSec = (t63Ms - tStartMs) / 1000;
    } else if (tPeakMs > tStartMs) {
      tauSec = (tPeakMs - tStartMs) / 3000;
    }

    let pSum = 0;
    let pCount = 0;
    for (let i = start; i <= peakIndex; i++) {
      const p = powers[i];
      if (Number.isFinite(p) && p > 0) {
        pSum += p;
        pCount++;
      }
    }
    let pAvg = pCount ? pSum / pCount : NaN;
    if (!Number.isFinite(pAvg) || pAvg <= 0) pAvg = maxPower;

    let kLoss = pAvg / deltaT;
    if (!Number.isFinite(kLoss) || kLoss <= 0) kLoss = NaN;

    let thermalC =
      Number.isFinite(kLoss) && Number.isFinite(tauSec) ? tauSec * kLoss : NaN;

    return {
      wire_tau: Number.isFinite(tauSec) ? tauSec : fallback.wire_tau,
      wire_k_loss: Number.isFinite(kLoss) ? kLoss : fallback.wire_k_loss,
      wire_c: Number.isFinite(thermalC) ? thermalC : fallback.wire_c,
      max_power_w: Number.isFinite(maxPower) ? maxPower : fallback.max_power_w,
    };
  }

  
  async function startCalibration(mode) {
    if (
      guardUnsafeAction("starting calibration", {
        blockAuto: true,
        blockCalib: true,
      })
    ) {
      return;
    }
    const payload = {
      mode,
      interval_ms: 500,
      max_samples: 1200,
      epoch: Math.floor(Date.now() / 1000),
    };
    const targetC = getFloat("wireTestTargetC");
    const maxC = getFloat("nichromeFinalTempC");
    if (mode === "model" && Number.isFinite(maxC) && maxC > 0) {
      payload.target_c = maxC;
    } else if (Number.isFinite(targetC) && targetC > 0) {
      payload.target_c = targetC;
    }

    setCalibText("calibStatusText", "Starting");
    const modeLabel =
      mode === "model" ? "Model" : mode === "floor" ? "Floor" : "--";
    setCalibText("calibModeText", modeLabel);
    setCalibText("calibCountText", "0");
    setCalibText("calibIntervalText", `${payload.interval_ms} ms`);
    setCalibText(
      "calibTargetText",
      Number.isFinite(payload.target_c)
        ? `${payload.target_c.toFixed(1)} C`
        : "--"
    );
    setCalibText("calibTempText", "--");
    setCalibText(
      "calibWireText",
      payload.wire_index ? `#${payload.wire_index}` : "--"
    );
    setCalibText("calibElapsedText", "--");

    try {
      const res = await fetch("/calib_start", {
        method: "POST",
        headers: cborHeaders(),
        body: encodeCbor(payload),
      });
      const data = await readCbor(res, {});
      if (!res.ok || (data && data.error)) {
        const err = (data && data.error) || `HTTP ${res.status}`;
        const extras = [];
        if (data && data.detail) extras.push(data.detail);
        if (data && data.state) extras.push(`state=${data.state}`);
        const msg = extras.length ? `${err} (${extras.join(", ")})` : err;
        console.error("Calibration start failed:", msg);
        openAlert("Calibration", msg, "danger");
        return;
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
      const epoch = Math.floor(Date.now() / 1000);
      const res = await fetch("/calib_stop", {
        method: "POST",
        headers: cborHeaders(),
        body: encodeCbor({ epoch }),
      });
      if (!res.ok) {
        console.error("Calibration stop failed:", res.status);
      }
      await pollCalibrationOnce();
      refreshCalibHistoryList(true);
      fetchSetupStatus();
    } catch (err) {
      console.error("Calibration stop error:", err);
    }
  }

  async function startNtcCalibration() {
    if (
      guardUnsafeAction("starting NTC calibration", {
        blockAuto: true,
        blockCalib: true,
      })
    ) {
      return;
    }
    const payload = {};
    const targetC = getFloat("ntcCalTargetC");
    const sampleMs = getInt("ntcCalSampleMs");
    const timeoutMs = getInt("ntcCalTimeoutMs");
    if (Number.isFinite(targetC)) payload.target_c = targetC;
    if (Number.isFinite(sampleMs)) payload.sample_ms = sampleMs;
    if (Number.isFinite(timeoutMs)) payload.timeout_ms = timeoutMs;

    try {
      const res = await fetch("/ntc_calibrate", {
        method: "POST",
        headers: cborHeaders(),
        body: encodeCbor(payload),
      });
      const data = await readCbor(res, {});
      if (!res.ok || (data && data.error)) {
        const msg = (data && data.error) || `HTTP ${res.status}`;
        openAlert("NTC Calibration", msg, "danger");
        return;
      }
      openAlert("NTC Calibration", "Started.", "success");
      await pollNtcCalOnce();
    } catch (err) {
      console.error("NTC calibration start error:", err);
      openAlert("NTC Calibration", "Start failed.", "danger");
    }
  }

  async function stopNtcCalibration() {
    try {
      await fetch("/ntc_cal_stop", {
        method: "POST",
        headers: cborHeaders(),
        body: encodeCbor({}),
      });
      await pollNtcCalOnce();
      fetchSetupStatus();
    } catch (err) {
      console.error("NTC calibration stop error:", err);
    }
  }

  async function startNtcBetaCalibration() {
    if (
      guardUnsafeAction("starting beta calibration", {
        blockAuto: true,
        blockCalib: true,
      })
    ) {
      return;
    }
    const payload = {};
    const refC = getFloat("ntcBetaRefC");
    if (Number.isFinite(refC)) payload.ref_temp_c = refC;
    try {
      const res = await fetch("/ntc_beta_calibrate", {
        method: "POST",
        headers: cborHeaders(),
        body: encodeCbor(payload),
      });
      const data = await readCbor(res, {});
      if (!res.ok || (data && data.error)) {
        const msg = (data && data.error) || `HTTP ${res.status}`;
        openAlert("NTC Beta Calibration", msg, "danger");
        return;
      }
      openAlert("NTC Beta Calibration", "Applied.", "success");
      await pollNtcCalOnce();
    } catch (err) {
      console.error("NTC beta calibration error:", err);
      openAlert("NTC Beta Calibration", "Request failed.", "danger");
    }
  }

  async function startFloorCalibration() {
    if (
      guardUnsafeAction("starting floor calibration", {
        blockAuto: true,
        blockCalib: true,
      })
    ) {
      return;
    }
    const payload = {
      mode: "floor",
      epoch: Math.floor(Date.now() / 1000),
    };

    const targetC = getFloat("floorCalTargetC");
    const dutyPct = getFloat("floorCalDutyPct");
    const ambientMin = getFloat("floorCalAmbientMin");
    const heatMin = getFloat("floorCalHeatMin");
    const coolMin = getFloat("floorCalCoolMin");
    const intervalMs = getInt("floorCalIntervalMs");
    const wireIndex = getInt("floorCalWireIndex");

    if (Number.isFinite(targetC)) payload.target_c = targetC;
    if (Number.isFinite(dutyPct)) payload.duty_pct = dutyPct;
    if (Number.isFinite(intervalMs)) payload.interval_ms = intervalMs;
    if (Number.isFinite(wireIndex)) payload.wire_index = wireIndex;

    if (Number.isFinite(ambientMin)) payload.ambient_ms = ambientMin * 60000;
    if (Number.isFinite(heatMin)) payload.heat_ms = heatMin * 60000;
    if (Number.isFinite(coolMin)) payload.cool_ms = coolMin * 60000;

    setCalibText("calibStatusText", "Starting");
    setCalibText("calibModeText", "Floor");
    setCalibText("calibCountText", "0");
    setCalibText(
      "calibIntervalText",
      Number.isFinite(payload.interval_ms) ? `${payload.interval_ms} ms` : "--"
    );
    setCalibText(
      "calibTargetText",
      Number.isFinite(payload.target_c)
        ? `${payload.target_c.toFixed(1)} C`
        : "--"
    );
    setCalibText("calibTempText", "--");
    setCalibText(
      "calibWireText",
      payload.wire_index ? `#${payload.wire_index}` : "--"
    );
    setCalibText("calibElapsedText", "--");

    try {
      const res = await fetch("/calib_start", {
        method: "POST",
        headers: cborHeaders(),
        body: encodeCbor(payload),
      });
      const data = await readCbor(res, {});
      if (!res.ok || (data && data.error)) {
        const err = (data && data.error) || `HTTP ${res.status}`;
        const extras = [];
        if (data && data.detail) extras.push(data.detail);
        if (data && data.state) extras.push(`state=${data.state}`);
        const msg = extras.length ? `${err} (${extras.join(", ")})` : err;
        openAlert("Floor Calibration", msg, "danger");
        return;
      }
      calibViewMode = "live";
      const historyBtn = document.getElementById("calibHistoryBtn");
      if (historyBtn) historyBtn.textContent = "Load History";
      await pollCalibrationOnce();
    } catch (err) {
      console.error("Floor calibration start error:", err);
    }
  }

  async function runPresenceProbe() {
    if (
      guardUnsafeAction("starting presence probe", {
        blockAuto: true,
        blockCalib: true,
      })
    ) {
      return;
    }
    const statusEl = document.getElementById("presenceProbeStatusText");
    const presenceBtn = document.getElementById("presenceProbeBtn");
    const inWizardPresence =
      wizardActive &&
      WIZARD_STEPS[wizardStepIndex] &&
      WIZARD_STEPS[wizardStepIndex].id === "wizardStepPresence";
    if (presenceBtn) presenceBtn.disabled = true;
    try {
      if (inWizardPresence) {
        if (statusEl) statusEl.textContent = "Charging (2s)...";
        await sleep(2000);
      }
      if (statusEl) statusEl.textContent = "Running...";
      const res = await fetch("/presence_probe", {
        method: "POST",
        headers: cborHeaders(),
        body: encodeCbor({}),
      });
      const data = await readCbor(res, {});
      if (!res.ok || (data && data.error)) {
        const msg = (data && data.error) || `HTTP ${res.status}`;
        if (statusEl) statusEl.textContent = "Failed";
        openAlert("Presence Probe", msg, "danger");
        return;
      }
      if (statusEl) statusEl.textContent = "Done";
      renderPresenceProbeResults(data && data.wirePresent);
      await fetchSetupStatus();
    } catch (err) {
      console.error("Presence probe error:", err);
      if (statusEl) statusEl.textContent = "Failed";
    } finally {
      if (presenceBtn) presenceBtn.disabled = false;
    }
  }

  function renderPresenceProbeResults(presentList) {
    const grid = document.getElementById("presenceProbeGrid");
    if (!grid) return;
    grid.innerHTML = "";
    const present = Array.isArray(presentList) ? presentList : [];
    const access =
      (lastLoadedControls && lastLoadedControls.outputAccess) || null;
    let rendered = false;
    for (let i = 1; i <= 10; i++) {
      if (access && access["output" + i] === false) {
        continue;
      }
      const chip = document.createElement("div");
      const isPresent = !!present[i - 1];
      chip.className = "presence-probe-chip";
      chip.classList.add(isPresent ? "is-present" : "is-missing");
      chip.textContent = `Wire ${i}: ${isPresent ? "OK" : "Missing"}`;
      grid.appendChild(chip);
      rendered = true;
    }
    if (!rendered) {
      const note = document.createElement("div");
      note.className = "presence-probe-chip is-missing";
      note.textContent = "No outputs enabled.";
      grid.appendChild(note);
    }
  }

  async function pollNtcCalOnce() {
    try {
      const res = await fetch("/ntc_cal_status", {
        cache: "no-store",
        headers: cborHeaders(),
      });
      if (!res.ok) return;
      const data = await readCbor(res, {});
      updateNtcCalUi(data);
    } catch (err) {
      console.warn("NTC calibration status failed:", err);
    }
  }

  function startNtcCalPoll() {
    stopNtcCalPoll();
    pollNtcCalOnce();
    ntcCalPollTimer = setInterval(pollNtcCalOnce, ntcCalPollIntervalMs);
  }

  function stopNtcCalPoll() {
    if (ntcCalPollTimer) {
      clearInterval(ntcCalPollTimer);
      ntcCalPollTimer = null;
    }
  }

  function updateNtcCalUi(data) {
    if (!data) return;
    let state = "Idle";
    if (data.running) state = "Running";
    else if (data.done) state = "Done";
    else if (data.error) state = "Error";
    setText("ntcCalStateText", state);
    if (data.error) {
      const err = data.error || "error";
      setText("ntcCalStateText", `Error (${err})`);
    }
    setText(
      "ntcCalTargetText",
      Number.isFinite(data.target_c) ? `${data.target_c.toFixed(1)} C` : "--"
    );
    setText(
      "ntcCalHeatsinkText",
      Number.isFinite(data.heatsink_c)
        ? `${data.heatsink_c.toFixed(1)} C`
        : "--"
    );
    setText(
      "ntcCalOhmText",
      Number.isFinite(data.ntc_ohm) ? `${data.ntc_ohm.toFixed(1)} ohm` : "--"
    );
    setText(
      "ntcCalSamplesText",
      Number.isFinite(data.samples) ? String(data.samples) : "0"
    );
    setText(
      "ntcCalElapsedText",
      formatElapsedMs(Number(data.elapsed_ms) || 0)
    );
    setText(
      "ntcCalShAText",
      Number.isFinite(data.sh_a) ? data.sh_a.toExponential(3) : "--"
    );
    setText(
      "ntcCalShBText",
      Number.isFinite(data.sh_b) ? data.sh_b.toExponential(3) : "--"
    );
    setText(
      "ntcCalShCText",
      Number.isFinite(data.sh_c) ? data.sh_c.toExponential(3) : "--"
    );
    setText(
      "ntcCalWireText",
      Number.isFinite(data.wire_index) ? `#${data.wire_index}` : "--"
    );
  }

  
  async function startWireTest() {
    if (
      guardUnsafeAction("starting the wire test", {
        blockAuto: true,
        blockCalib: true,
      })
    ) {
      return;
    }
    const target = parseFloat(
      (document.getElementById("wireTestTargetC") || {}).value
    );

    if (!isFinite(target) || target <= 0) {
      openAlert("Wire Test", "Enter a valid target temperature.", "warning");
      return;
    }

    const payload = { target_c: target };

    try {
      const res = await fetch("/wire_test_start", {
        method: "POST",
        headers: cborHeaders(),
        body: encodeCbor(payload),
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
        headers: cborHeaders(),
        body: encodeCbor({}),
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
        headers: cborHeaders(),
        body: encodeCbor({}),
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
      refreshCalibHistoryList(true);
    } catch (err) {
      console.error("Calibration clear error:", err);
    }
  }

  async function pollWireTestOnce() {
    try {
      const res = await fetch("/wire_test_status", {
        cache: "no-store",
        headers: cborHeaders(),
      });
      if (!res.ok) return;
      const data = await readCbor(res, {});

      const running = !!data.running;
      const stateEl = document.getElementById("wireTestState");
      if (stateEl) stateEl.textContent = running ? "Running" : "Idle";

      const modeEl = document.getElementById("wireTestMode");
      if (modeEl) {
        modeEl.textContent = data.mode || "--";
      }

      const purposeEl = document.getElementById("wireTestPurpose");
      if (purposeEl) {
        const p = data.purpose || "none";
        const label =
          p === "wire_test"
            ? "Wire Test"
            : p === "model_cal"
            ? "Model Cal"
            : "--";
        purposeEl.textContent = label;
      }

      const activeEl = document.getElementById("wireTestActiveWire");
      if (activeEl) {
        activeEl.textContent = data.active_wire ? `#${data.active_wire}` : "--";
      }

      const ntcEl = document.getElementById("wireTestNtcTemp");
      if (ntcEl) {
        ntcEl.textContent = Number.isFinite(data.ntc_temp_c)
          ? `${data.ntc_temp_c.toFixed(1)} C`
          : "--";
      }

      const modelEl = document.getElementById("wireTestModelTemp");
      if (modelEl) {
        modelEl.textContent = Number.isFinite(data.active_temp_c)
          ? `${data.active_temp_c.toFixed(1)} C`
          : "--";
      }

      const packetEl = document.getElementById("wireTestPacket");
      if (packetEl) {
        packetEl.textContent = Number.isFinite(data.packet_ms)
          ? `${Math.round(data.packet_ms)} ms`
          : "--";
      }

      const frameEl = document.getElementById("wireTestFrame");
      if (frameEl) {
        frameEl.textContent = Number.isFinite(data.frame_ms)
          ? `${Math.round(data.frame_ms)} ms`
          : "--";
      }

      const tgtInput = document.getElementById("wireTestTargetC");
      if (tgtInput && isFinite(data.target_c)) {
        tgtInput.value = data.target_c;
      }

      const startBtn = document.getElementById("wireTestStartBtn");
      const stopBtn = document.getElementById("wireTestStopBtn");
      if (startBtn) startBtn.disabled = running;
      if (stopBtn)
        stopBtn.disabled = !(running && data.purpose === "wire_test");

      updateTestModeFromWireStatus(data);
    } catch (err) {
      console.warn("Wire test status error:", err);
    }
  }

  function updateTestModeFromWireStatus(data) {
    if (!data || typeof data !== "object") {
      testModeState = { active: false, label: "--", targetC: NaN };
      lastWireTestStatus = null;
      updateTestModeBadge();
      return;
    }

    const running = !!data.running;
    const purpose = data.purpose || "none";
    const label =
      purpose === "wire_test"
        ? "Wire Test"
        : purpose === "model_cal"
        ? "Model Cal"
        : purpose === "ntc_cal"
        ? "NTC Cal"
        : purpose === "floor_cal"
        ? "Floor Cal"
        : "--";

    const active = running && purpose !== "none";
    const target = Number(data.target_c);

    testModeState.active = active;
    testModeState.label = active ? label : "--";
    testModeState.targetC = Number.isFinite(target) ? target : NaN;
    lastWireTestStatus = data;

    updateTestModeBadge();
  }

  function updateTestModeBadge() {
    const badge = document.getElementById("testModeBadge");
    if (!badge) return;

    const modeEl = badge.querySelector ? badge.querySelector(".tm-mode") : null;

    if (testModeState.active) {
      badge.classList.add("active");
      badge.title = `Test mode active: ${testModeState.label}`;
      if (modeEl) modeEl.textContent = testModeState.label;
    } else {
      badge.classList.remove("active");
      badge.title = "Test mode active";
      if (modeEl) modeEl.textContent = "--";
    }
    updateStatusBarState();
  }

  function updateAmbientWaitFromMonitor(wait) {
    if (!wait || typeof wait !== "object") {
      ambientWaitState = { active: false, label: "--", tolC: NaN, sinceMs: 0 };
      updateAmbientWaitBadge();
      return;
    }

    const active = !!wait.active;
    const reason = String(wait.reason || "");
    const label =
      reason === "model_cal"
        ? "Model Cal"
        : reason === "ntc_cal"
        ? "NTC Cal"
        : reason === "floor_cal"
        ? "Floor Cal"
        : "Run";

    ambientWaitState.active = active;
    ambientWaitState.label = active ? label : "--";
    const tol = Number(wait.tol_c);
    ambientWaitState.tolC = Number.isFinite(tol) ? tol : NaN;
    ambientWaitState.sinceMs = Number(wait.since_ms) || 0;

    updateAmbientWaitBadge();
  }

  function updateAmbientWaitBadge() {
    const badge = document.getElementById("ambientWaitBadge");
    if (!badge) return;

    const modeEl = badge.querySelector ? badge.querySelector(".cw-mode") : null;

    if (ambientWaitState.active) {
      badge.classList.add("active");
      const tolText = Number.isFinite(ambientWaitState.tolC)
        ? `+/-${ambientWaitState.tolC.toFixed(1)} C`
        : "";
      const label =
        ambientWaitState.label && ambientWaitState.label !== "--"
          ? ` (${ambientWaitState.label})`
          : "";
      badge.title = `Cooling to ambient${label}${tolText ? " " + tolText : ""}`;
      if (modeEl) modeEl.textContent = ambientWaitState.label;
    } else {
      badge.classList.remove("active");
      badge.title = "Cooling to ambient";
      if (modeEl) modeEl.textContent = "--";
    }

    updateStatusBarState();
  }

  function updateStatusBarState() {
    const bar = document.querySelector(".user-status-global");
    if (bar) {
      bar.classList.toggle("testmode-active", testModeState.active);
      bar.classList.toggle("cooldown-active", ambientWaitState.active);
      bar.classList.toggle("run-active", lastState === "Running");
      const wireActive = !!(lastWireTestStatus && lastWireTestStatus.running);
      const calibRunning = !!(calibLastMeta && calibLastMeta.running);
      const calibActive = wireActive || calibRunning;
      bar.classList.toggle("calib-active", calibActive);
    }

    const stopBtn = document.getElementById("topStopTestBtn");
    if (stopBtn) {
      const calibRunning = !!(calibLastMeta && calibLastMeta.running);
      const canStop = testModeState.active || calibRunning;
      const label = testModeState.active
        ? testModeState.label
        : calibRunning
        ? "Calibration"
        : "";
      const title = label ? `Stop ${label}` : "Stop the active test";
      stopBtn.title = title;
      stopBtn.setAttribute("aria-label", title);
      const textEl = stopBtn.querySelector
        ? stopBtn.querySelector(".status-action-text")
        : null;
      if (textEl) textEl.textContent = "Stop";
      stopBtn.disabled = !canStop;
    }

    applySafetyLocks();
  }

  async function stopActiveTestMode() {
    const stopBtn = document.getElementById("topStopTestBtn");
    if (stopBtn) stopBtn.disabled = true;

    const purpose =
      (lastWireTestStatus && lastWireTestStatus.purpose) || "none";
    try {
      if (purpose === "wire_test") {
        await stopWireTest();
      } else if (purpose === "model_cal") {
        await stopCalibration();
        await stopWireTest();
      } else if (purpose === "floor_cal") {
        await stopCalibration();
        await stopWireTest();
      } else if (purpose === "ntc_cal") {
        await stopNtcCalibration();
        await stopWireTest();
      } else if (calibLastMeta && calibLastMeta.running) {
        await stopCalibration();
      }
    } finally {
      if (stopBtn) stopBtn.disabled = false;
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

  async function fetchCalibrationStatus() {
    try {
      const res = await fetch("/calib_status", {
        cache: "no-store",
        headers: cborHeaders(),
      });
      if (!res.ok) return null;
      return await readCbor(res, null);
    } catch (err) {
      console.warn("Calibration status error:", err);
      return null;
    }
  }

  async function fetchCalibrationPage(offset, count) {
    const url = `/calib_data?offset=${offset}&count=${count}`;
    const res = await fetch(url, { cache: "no-store", headers: cborHeaders() });
    if (!res.ok) return null;
    return await readCbor(res, null);
  }

  async function fetchCalibrationHistoryList() {
    try {
      const res = await fetch("/calib_history_list", {
        cache: "no-store",
        headers: cborHeaders(),
      });
      if (!res.ok) return null;
      return await readCbor(res, null);
    } catch (err) {
      console.warn("History list error:", err);
      return null;
    }
  }

  function formatEpochLocal(epochSec) {
    if (!epochSec) return "--";
    const d = new Date(epochSec * 1000);
    if (Number.isNaN(d.getTime())) return "--";
    return d.toLocaleString();
  }

  function formatElapsedMs(ms) {
    if (!Number.isFinite(ms) || ms <= 0) return "--";
    const sec = ms / 1000;
    if (sec >= 60) {
      const min = Math.floor(sec / 60);
      const rem = sec - min * 60;
      return `${min}m ${rem.toFixed(1)}s`;
    }
    return `${sec.toFixed(1)} s`;
  }

  function populateCalibHistorySelect(items, selectNewest) {
    const sel = document.getElementById("calibHistorySelect");
    if (!sel) return;
    const prev = sel.value;
    sel.innerHTML = "";

    if (!items || !items.length) {
      const opt = document.createElement("option");
      opt.value = "";
      opt.textContent = "No saved history";
      sel.appendChild(opt);
      sel.disabled = true;
      return;
    }

    const sorted = [...items].sort((a, b) => {
      const ea = Number(a.start_epoch || 0);
      const eb = Number(b.start_epoch || 0);
      return eb - ea;
    });

    for (const item of sorted) {
      const opt = document.createElement("option");
      opt.value = item.name || "";
      const label = item.start_epoch
        ? formatEpochLocal(item.start_epoch)
        : item.name;
      opt.textContent = label || item.name || "Unknown";
      sel.appendChild(opt);
    }

    sel.disabled = false;
    if (selectNewest) {
      sel.value = sel.options.length ? sel.options[0].value : "";
    } else if (prev) {
      sel.value = prev;
    }
  }

  async function refreshCalibHistoryList(selectNewest = false) {
    const data = await fetchCalibrationHistoryList();
    const items = data && data.items ? data.items : [];
    populateCalibHistorySelect(items, selectNewest);
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
    if (meta && Number.isFinite(meta.target_c)) {
      setCalibText("calibTargetText", `${meta.target_c.toFixed(1)} C`);
    }
  }

  async function loadSavedCalibration() {
    const sel = document.getElementById("calibHistorySelect");
    const name = sel ? sel.value : "";
    if (!name) {
      openAlert("Calibration", "Select a saved history first.", "warning");
      return;
    }
    try {
      const res = await fetch(
        `/calib_history_file?name=${encodeURIComponent(name)}`,
        {
          cache: "no-store",
          headers: cborHeaders(),
        }
      );
      if (!res.ok) {
        openAlert("Calibration", "Failed to load saved history.", "danger");
        return;
      }
      const data = await readCbor(res, {});
      calibViewMode = "saved";
      calibSamples = Array.isArray(data.samples) ? data.samples : [];
      calibLastMeta = data.meta || null;
      renderCalibrationChart();
      scrollCalibToLatest();

      const last = [...calibSamples].reverse().find((s) => isFinite(s.temp_c));
      setCalibText(
        "calibTempText",
        last && isFinite(last.temp_c) ? `${last.temp_c.toFixed(1)} C` : "--"
      );

      if (calibLastMeta) {
        setCalibText("calibStatusText", "Saved");
        setCalibText("calibModeText", calibLastMeta.mode || "--");
        setCalibText(
          "calibCountText",
          calibLastMeta.count != null ? String(calibLastMeta.count) : "0"
        );
        setCalibText(
          "calibIntervalText",
          calibLastMeta.interval_ms ? `${calibLastMeta.interval_ms} ms` : "--"
        );
        if (Number.isFinite(calibLastMeta.target_c)) {
          setCalibText(
            "calibTargetText",
            `${calibLastMeta.target_c.toFixed(1)} C`
          );
        } else {
          setCalibText("calibTargetText", "--");
        }
      }
      const historyBtn = document.getElementById("calibHistoryBtn");
      if (historyBtn) historyBtn.textContent = "Resume Live";
    } catch (err) {
      console.error("Saved history load error:", err);
      openAlert("Calibration", "Saved history load failed.", "danger");
    }
  }

  async function toggleCalibrationHistory() {
    const btn = document.getElementById("calibHistoryBtn");
    if (calibViewMode !== "live") {
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

    const prevMeta = calibLastMeta;
    calibLastMeta = meta;
    const modeLabel =
      meta.mode === "model"
        ? "Model"
        : meta.mode === "floor"
        ? "Floor"
        : meta.mode === "ntc"
        ? "NTC"
        : "--";
    const statusLabel = meta.running
      ? meta.count
        ? "Running"
        : "Starting"
      : "Idle";
    setCalibText("calibStatusText", statusLabel);
    setCalibText("calibModeText", modeLabel);
    setCalibText(
      "calibCountText",
      meta.count != null ? String(meta.count) : "0"
    );
    setCalibText(
      "calibIntervalText",
      meta.interval_ms ? `${meta.interval_ms} ms` : "--"
    );
    const wireIdx = meta.wire_index;
    setCalibText("calibWireText", wireIdx ? `#${wireIdx}` : "--");
    if (Number.isFinite(meta.target_c)) {
      setCalibText("calibTargetText", `${meta.target_c.toFixed(1)} C`);
    } else {
      setCalibText("calibTargetText", "--");
    }
    const intervalMs = Number(meta.interval_ms);
    const elapsedMs =
      Number.isFinite(intervalMs) && meta.count ? meta.count * intervalMs : NaN;
    setCalibText("calibElapsedText", formatElapsedMs(elapsedMs));

    const desiredMs = meta.running
      ? Math.max(
          250,
          Math.min(1000, Number.isFinite(intervalMs) ? intervalMs : 500)
        )
      : 1000;
    setCalibPollInterval(desiredMs);

    updateStatusBarState();


    if (calibViewMode !== "live") return;
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
      if (last && Number.isFinite(last.t_ms)) {
        setCalibText("calibElapsedText", formatElapsedMs(last.t_ms));
      }
    }
  }

  function startCalibrationPoll() {
    stopCalibrationPoll();
    calibPollIntervalMs = 1000;
    pollCalibrationOnce();
    calibPollTimer = setInterval(pollCalibrationOnce, calibPollIntervalMs);
  }

  function stopCalibrationPoll() {
    if (calibPollTimer) {
      clearInterval(calibPollTimer);
      calibPollTimer = null;
    }
  }

  function setCalibPollInterval(ms) {
    const clamped = Math.max(250, Math.min(2000, ms));
    if (clamped === calibPollIntervalMs) return;
    calibPollIntervalMs = clamped;
    if (calibPollTimer) {
      clearInterval(calibPollTimer);
      calibPollTimer = setInterval(pollCalibrationOnce, calibPollIntervalMs);
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
    if (hh > 0)
      return `${hh}:${String(mm).padStart(2, "0")}:${String(ss).padStart(
        2,
        "0"
      )}`;
    return `${mm}:${String(ss).padStart(2, "0")}`;
  }

  function fmtEpochTime(epochSec) {
    if (!epochSec) return "--:--";
    const d = new Date(epochSec * 1000);
    if (Number.isNaN(d.getTime())) return "--:--";
    const hh = String(d.getHours()).padStart(2, "0");
    const mm = String(d.getMinutes()).padStart(2, "0");
    const ss = String(d.getSeconds()).padStart(2, "0");
    return `${hh}:${mm}:${ss}`;
  }

  function fmtCalibTime(msFromStart, startEpochSec, startMs) {
    if (startEpochSec) {
      const epoch = startEpochSec + Math.round((msFromStart || 0) / 1000);
      return fmtEpochTime(epoch);
    }
    return fmtUptime((startMs || 0) + (msFromStart || 0));
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
    return (
      CALIB_Y0 -
      ((t - CALIB_T_MIN) * (CALIB_Y0 - CALIB_Y1)) / (CALIB_T_MAX - CALIB_T_MIN)
    );
  }

  function tag(x, y, text) {
    const padX = 6;
    const charW = 7;
    const w = Math.max(34, String(text).length * charW + padX * 2);
    const h = 20;
    return `
      <g>
        <rect class="tag" x="${x}" y="${
      y - h / 2
    }" width="${w}" height="${h}" rx="9"></rect>
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
      <line class="axisLine" x1="${W - 1}" y1="${CALIB_Y1}" x2="${
      W - 1
    }" y2="${CALIB_Y0}"></line>
      <text class="label" x="16" y="${
        (CALIB_Y1 + CALIB_Y0) / 2
      }" transform="rotate(-90 16 ${(CALIB_Y1 + CALIB_Y0) / 2})">C</text>
    `;

    for (let t = CALIB_T_MIN; t <= CALIB_T_MAX; t += CALIB_Y_TICK_STEP) {
      const y = yFromTemp(t);
      s += `<line class="yTick" x1="${W - 8}" y1="${y}" x2="${
        W - 1
      }" y2="${y}"></line>`;
      s += `<text class="yLabel" x="${W - 12}" y="${
        y + 4
      }" text-anchor="end">${t}</text>`;
    }
    yAxisSvg.innerHTML = s;
  }

  function buildCalibGrid(xMax, intervalMs, pointCount) {
    let s = `<g class="grid">`;
    for (let t = CALIB_T_MIN; t <= CALIB_T_MAX; t += CALIB_Y_TICK_STEP) {
      const y = yFromTemp(t);
      s += `<line class="gridLine" x1="0" y1="${y}" x2="${xMax}" y2="${y}"></line>`;
    }
    const vEverySamples = Math.max(
      1,
      Math.round(1000 / Math.max(50, intervalMs || 500))
    );
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
      <text class="label" x="${Math.max(40, xMax / 2 - 20)}" y="${
      CALIB_H - 10
    }">Time</text>
    `;
  }

  function buildCalibTimeTicks(samples, intervalMs, startMs, startEpochSec) {
    let s = `<g>`;
    const tickEverySamples = Math.max(
      1,
      Math.round(
        (CALIB_TICK_EVERY_SECONDS * 1000) / Math.max(50, intervalMs || 500)
      )
    );
    for (let i = 0; i < samples.length; i += tickEverySamples) {
      const x = CALIB_PLOT_PAD_LEFT + i * CALIB_DX;
      const tMs = samples[i].t_ms || 0;
      const label = fmtCalibTime(tMs, startEpochSec, startMs);
      s += `<line class="tick" x1="${x}" y1="${CALIB_Y0}" x2="${x}" y2="${
        CALIB_Y0 + 6
      }"></line>`;
      s += `<text class="subtext" x="${x - 18}" y="${
        CALIB_Y0 + 22
      }">${label}</text>`;
    }
    s += `</g>`;
    return s;
  }

  function buildCalibPolyline(samples) {
    if (samples.length === 0) return "";
    const pts = samples
      .map(
        (p, i) => `${CALIB_PLOT_PAD_LEFT + i * CALIB_DX},${yFromTemp(p.temp_c)}`
      )
      .join(" ");
    return `<polyline class="temp-line" points="${pts}"></polyline>`;
  }

  function buildCalibLatestMarker(samples, xMax, startMs, startEpochSec) {
    if (samples.length === 0) return "";
    const i = samples.length - 1;
    const p = samples[i];
    const x = CALIB_PLOT_PAD_LEFT + i * CALIB_DX;
    const y = yFromTemp(p.temp_c);

    const timeLabel = fmtCalibTime(p.t_ms || 0, startEpochSec, startMs);
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
      (calibLastMeta && (calibLastMeta.start_ms || calibLastMeta.startMs)) || 0;
    const startEpochSec =
      (calibLastMeta &&
        (calibLastMeta.start_epoch || calibLastMeta.startEpoch)) ||
      0;

    const xMax =
      CALIB_PLOT_PAD_LEFT +
      Math.max(1, samples.length - 1) * CALIB_DX +
      CALIB_RIGHT_PAD;
    const nearEnd =
      scrollWrap.scrollLeft + scrollWrap.clientWidth >=
      scrollWrap.scrollWidth - 30;

    plotSvg.setAttribute("width", xMax);
    plotSvg.setAttribute("height", CALIB_H);
    plotSvg.setAttribute("viewBox", `0 0 ${xMax} ${CALIB_H}`);
    plotSvg.innerHTML = `
      ${buildCalibGrid(xMax, intervalMs, samples.length)}
      ${buildCalibXAxis(xMax)}
      ${buildCalibTimeTicks(samples, intervalMs, startMs, startEpochSec)}
      <text class="subtext" x="8" y="18">Latest point: dot + dotted guides</text>
      ${buildCalibPolyline(samples)}
      ${buildCalibLatestMarker(samples, xMax, startMs, startEpochSec)}
    `;

    const last = samples[samples.length - 1];
    const tEl = document.getElementById("calibNowTempPill");
    const tsEl = document.getElementById("calibNowTimePill");
    if (tEl) tEl.textContent = Number(last.temp_c).toFixed(1);
    if (tsEl)
      tsEl.textContent = fmtCalibTime(last.t_ms || 0, startEpochSec, startMs);

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

  // ========================================================
  // ===============      LIVE CONTROL VIEW     =============
  // ========================================================

  const LIVE_CTRL_COLORS = [
    "#ffb347",
    "#00c2ff",
    "#00ff9d",
    "#ff6b6b",
    "#9b6bff",
    "#ffd166",
    "#4cc9f0",
    "#f72585",
    "#b8f2e6",
    "#a3be8c",
  ];
  const LIVE_CTRL_SETPOINT_COLOR = "#ff2d6f";

  const LIVE_CTRL_TICK_EVERY_SECONDS = 5;

  function isLiveControlOpen() {
    const modal = document.getElementById("liveControlModal");
    if (modal) return modal.classList.contains("show");
    return liveControlModalOpen;
  }

  function getLiveControlSetpoint() {
    if (testModeState.active && Number.isFinite(testModeState.targetC)) {
      return testModeState.targetC;
    }
    const monitorTarget = lastMonitor ? Number(lastMonitor.wireTargetC) : NaN;
    if (Number.isFinite(monitorTarget)) return monitorTarget;
    const input = document.getElementById("wireTestTargetC");
    const fallback = input ? Number(input.value) : NaN;
    if (Number.isFinite(fallback)) return fallback;
    return NaN;
  }

  function getLiveControlModeLabel() {
    if (testModeState.active) return testModeState.label;
    if (lastState === "Running") return "Run";
    if (lastState === "Shutdown") return "Off";
    return "Idle";
  }

  function getLiveControlAllowedWires() {
    const access =
      (lastLoadedControls && lastLoadedControls.outputAccess) || {};
    const accessKeys = Object.keys(access || {});
    const hasAccessMap = accessKeys.some((k) => k.startsWith("output"));
    const allowed = [];
    for (let i = 1; i <= 10; i++) {
      const key = "output" + i;
      if (hasAccessMap && !access[key]) continue;
      allowed.push(i);
    }
    return allowed;
  }

  function getLiveControlPresentWires(samples, allowed) {
    if (!samples.length) return [];
    const temps = samples[samples.length - 1].wireTemps || [];
    return allowed.filter((idx) => {
      const t = Number(temps[idx - 1]);
      return Number.isFinite(t) && t > -100;
    });
  }

  function renderLiveControlLegend(wires, setpointC) {
    const el = document.getElementById("liveControlLegend");
    if (!el) return;
    const items = [];

    if (Number.isFinite(setpointC)) {
      items.push(
        `<div class="legend-item"><span class="legend-swatch" style="background:${LIVE_CTRL_SETPOINT_COLOR}"></span><span class="legend-label">Setpoint</span></div>`
      );
    }

    for (const wire of wires) {
      const color = LIVE_CTRL_COLORS[(wire - 1) % LIVE_CTRL_COLORS.length];
      items.push(
        `<div class="legend-item"><span class="legend-swatch" style="background:${color}"></span><span class="legend-label">Wire ${wire}</span></div>`
      );
    }

    if (!items.length) {
      items.push(
        '<div class="legend-item"><span class="legend-label">No allowed outputs</span></div>'
      );
    }

    el.innerHTML = items.join("");
  }

  function drawLiveControlYAxis() {
    const yAxisSvg = document.getElementById("liveControlYAxis");
    if (!yAxisSvg) return;

    const W = 72;
    yAxisSvg.setAttribute("width", W);
    yAxisSvg.setAttribute("height", CALIB_H);
    yAxisSvg.setAttribute("viewBox", `0 0 ${W} ${CALIB_H}`);

    let s = `
      <line class="axisLine" x1="${W - 1}" y1="${CALIB_Y1}" x2="${
      W - 1
    }" y2="${CALIB_Y0}"></line>
      <text class="label" x="16" y="${
        (CALIB_Y1 + CALIB_Y0) / 2
      }" transform="rotate(-90 16 ${(CALIB_Y1 + CALIB_Y0) / 2})">C</text>
    `;

    for (let t = CALIB_T_MIN; t <= CALIB_T_MAX; t += CALIB_Y_TICK_STEP) {
      const y = yFromTemp(t);
      s += `<line class="yTick" x1="${W - 8}" y1="${y}" x2="${
        W - 1
      }" y2="${y}"></line>`;
      s += `<text class="yLabel" x="${W - 12}" y="${
        y + 4
      }" text-anchor="end">${t}</text>`;
    }
    yAxisSvg.innerHTML = s;
  }

  function buildLiveControlGrid(xMax, intervalMs, pointCount) {
    let s = `<g class="grid">`;
    for (let t = CALIB_T_MIN; t <= CALIB_T_MAX; t += CALIB_Y_TICK_STEP) {
      const y = yFromTemp(t);
      s += `<line class="gridLine" x1="0" y1="${y}" x2="${xMax}" y2="${y}"></line>`;
    }
    const vEverySamples = Math.max(
      1,
      Math.round(1000 / Math.max(50, intervalMs || 500))
    );
    for (let i = 0; i < pointCount; i += vEverySamples) {
      const x = CALIB_PLOT_PAD_LEFT + i * CALIB_DX;
      s += `<line x1="${x}" y1="${CALIB_Y1}" x2="${x}" y2="${CALIB_Y0}"></line>`;
    }
    s += `</g>`;
    return s;
  }

  function buildLiveControlXAxis(xMax) {
    return `
      <line class="xAxis" x1="0" y1="${CALIB_Y0}" x2="${xMax}" y2="${CALIB_Y0}"></line>
      <text class="label" x="${Math.max(40, xMax / 2 - 20)}" y="${
      CALIB_H - 10
    }">Time</text>
    `;
  }

  function buildLiveControlTimeTicks(samples, intervalMs) {
    let s = `<g>`;
    const tickEverySamples = Math.max(
      1,
      Math.round(
        (LIVE_CTRL_TICK_EVERY_SECONDS * 1000) / Math.max(50, intervalMs || 500)
      )
    );
    for (let i = 0; i < samples.length; i += tickEverySamples) {
      const x = CALIB_PLOT_PAD_LEFT + i * CALIB_DX;
      const tMs = samples[i].t_ms || 0;
      const label = fmtUptime(tMs);
      s += `<line class="tick" x1="${x}" y1="${CALIB_Y0}" x2="${x}" y2="${
        CALIB_Y0 + 6
      }"></line>`;
      s += `<text class="subtext" x="${x - 18}" y="${
        CALIB_Y0 + 22
      }">${label}</text>`;
    }
    s += `</g>`;
    return s;
  }

  function buildLiveControlWirePath(samples, wireIndex) {
    let d = "";
    let started = false;
    for (let i = 0; i < samples.length; i++) {
      const temps = samples[i].wireTemps || [];
      const raw = Number(temps[wireIndex - 1]);
      if (!Number.isFinite(raw) || raw <= -100) {
        started = false;
        continue;
      }
      const x = CALIB_PLOT_PAD_LEFT + i * CALIB_DX;
      const y = yFromTemp(raw);
      d += `${started ? " L" : " M"} ${x} ${y}`;
      started = true;
    }
    return d;
  }

  function buildLiveControlWirePaths(samples, wires) {
    let s = "";
    for (const wire of wires) {
      const path = buildLiveControlWirePath(samples, wire);
      if (!path) continue;
      const color = LIVE_CTRL_COLORS[(wire - 1) % LIVE_CTRL_COLORS.length];
      s += `<path class="live-line" stroke="${color}" d="${path}"></path>`;
    }
    return s;
  }

  function buildLiveControlSetpointLine(xMax, setpointC) {
    if (!Number.isFinite(setpointC)) return "";
    const y = yFromTemp(setpointC);
    return `<line class="setpoint-line" stroke="${LIVE_CTRL_SETPOINT_COLOR}" x1="0" y1="${y}" x2="${xMax}" y2="${y}"></line>`;
  }

  function scrollLiveControlToLatest() {
    const wrap = document.getElementById("liveControlScrollWrap");
    if (!wrap) return;
    wrap.scrollLeft = wrap.scrollWidth;
  }

  function renderLiveControlChart() {
    const plotSvg = document.getElementById("liveControlPlot");
    const scrollWrap = document.getElementById("liveControlScrollWrap");
    if (!plotSvg || !scrollWrap) return;

    drawLiveControlYAxis();

    const samples = (liveControlSamples || []).filter((s) =>
      Array.isArray(s.wireTemps)
    );
    const setpoint = getLiveControlSetpoint();
    const modeLabel = getLiveControlModeLabel();

    if (!samples.length) {
      plotSvg.setAttribute("width", 600);
      plotSvg.setAttribute("height", CALIB_H);
      plotSvg.setAttribute("viewBox", `0 0 600 ${CALIB_H}`);
      plotSvg.innerHTML = `<text class="subtext" x="8" y="18">No live data yet.</text>`;
      const tEl = document.getElementById("liveControlNowTimePill");
      const sEl = document.getElementById("liveControlSetpointPill");
      const mEl = document.getElementById("liveControlModePill");
      if (tEl) tEl.textContent = "--:--";
      if (sEl) sEl.textContent = "--";
      if (mEl) mEl.textContent = modeLabel;
      renderLiveControlLegend([], setpoint);
      return;
    }

    const allowed = getLiveControlAllowedWires();
    const present = getLiveControlPresentWires(samples, allowed);
    renderLiveControlLegend(present, setpoint);

    const intervalMs = Math.max(50, liveControlLastIntervalMs || 500);
    const xMax =
      CALIB_PLOT_PAD_LEFT +
      Math.max(1, samples.length - 1) * CALIB_DX +
      CALIB_RIGHT_PAD;
    const nearEnd =
      scrollWrap.scrollLeft + scrollWrap.clientWidth >=
      scrollWrap.scrollWidth - 30;

    plotSvg.setAttribute("width", xMax);
    plotSvg.setAttribute("height", CALIB_H);
    plotSvg.setAttribute("viewBox", `0 0 ${xMax} ${CALIB_H}`);
    plotSvg.innerHTML = `
      ${buildLiveControlGrid(xMax, intervalMs, samples.length)}
      ${buildLiveControlXAxis(xMax)}
      ${buildLiveControlTimeTicks(samples, intervalMs)}
      ${buildLiveControlSetpointLine(xMax, setpoint)}
      ${buildLiveControlWirePaths(samples, present)}
    `;

    const last = samples[samples.length - 1];
    const tEl = document.getElementById("liveControlNowTimePill");
    const sEl = document.getElementById("liveControlSetpointPill");
    const mEl = document.getElementById("liveControlModePill");
    if (tEl) tEl.textContent = fmtUptime(last.t_ms || 0);
    if (sEl)
      sEl.textContent = Number.isFinite(setpoint) ? setpoint.toFixed(1) : "--";
    if (mEl) mEl.textContent = modeLabel;

    if (!liveControlChartPaused && nearEnd) {
      scrollLiveControlToLatest();
    }
  }

  function bindLiveControlChartDrag() {
    const scrollWrap = document.getElementById("liveControlScrollWrap");
    if (!scrollWrap || scrollWrap.__dragBound) return;
    scrollWrap.__dragBound = true;

    const dragStart = (clientX) => {
      liveControlDrag.dragging = true;
      scrollWrap.classList.add("dragging");
      liveControlDrag.startX = clientX;
      liveControlDrag.startScrollLeft = scrollWrap.scrollLeft;
    };
    const dragMove = (clientX) => {
      if (!liveControlDrag.dragging) return;
      const dx = clientX - liveControlDrag.startX;
      scrollWrap.scrollLeft = liveControlDrag.startScrollLeft - dx;
    };
    const dragEnd = () => {
      liveControlDrag.dragging = false;
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

  function toggleLiveControlPause() {
    liveControlChartPaused = !liveControlChartPaused;
    const pauseBtn = document.getElementById("liveControlPauseBtn");
    if (pauseBtn)
      pauseBtn.textContent = liveControlChartPaused ? "Resume" : "Pause";
    if (!liveControlChartPaused) {
      renderLiveControlChart();
    }
  }

  function clearLiveControlSamples() {
    liveControlSamples = [];
    liveControlStartPerf = null;
    liveControlLastIntervalMs = 1000;
    renderLiveControlChart();
  }

  function pushLiveControlSample(mon) {
    if (!mon || !Array.isArray(mon.wireTemps)) return;
    const now = performance.now();
    if (liveControlStartPerf === null) liveControlStartPerf = now;
    const tMs = Math.round(now - liveControlStartPerf);
    const temps = mon.wireTemps.map((t) => Number(t));
    const setpoint = getLiveControlSetpoint();

    liveControlSamples.push({
      t_ms: tMs,
      wireTemps: temps,
      target_c: setpoint,
    });
    if (liveControlSamples.length > liveControlMaxSamples) {
      liveControlSamples.shift();
    }
    if (liveControlSamples.length >= 2) {
      const prev = liveControlSamples[liveControlSamples.length - 2].t_ms;
      const dt = tMs - prev;
      if (dt > 0 && dt < 60000) liveControlLastIntervalMs = dt;
    }
  }

  function openLiveControlModal() {
    const m = document.getElementById("liveControlModal");
    if (!m) return;
    m.classList.add("show");
    liveControlModalOpen = true;
    liveControlChartPaused = false;
    const pauseBtn = document.getElementById("liveControlPauseBtn");
    if (pauseBtn) pauseBtn.textContent = "Pause";
    bindLiveControlChartDrag();
    renderLiveControlChart();
    applyTooltips(m);
  }

  function closeLiveControlModal() {
    const m = document.getElementById("liveControlModal");
    if (m) m.classList.remove("show");
    liveControlModalOpen = false;
    liveControlChartPaused = false;
  }

  function bindLiveControlButton() {
    const btn = document.getElementById("liveControlBtn");
    if (btn) btn.addEventListener("click", openLiveControlModal);
    const topBtn = document.getElementById("topLiveControlBtn");
    if (topBtn) topBtn.addEventListener("click", openLiveControlModal);
    const stopBtn = document.getElementById("topStopTestBtn");
    if (stopBtn) stopBtn.addEventListener("click", stopActiveTestMode);

    const latestBtn = document.getElementById("liveControlLatestBtn");
    if (latestBtn) {
      latestBtn.addEventListener("click", () => scrollLiveControlToLatest());
    }
    const pauseBtn = document.getElementById("liveControlPauseBtn");
    if (pauseBtn) pauseBtn.addEventListener("click", toggleLiveControlPause);

    const clearBtn = document.getElementById("liveControlClearBtn");
    if (clearBtn) clearBtn.addEventListener("click", clearLiveControlSamples);
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

  function setGaugeUnknown(id) {
    const display = document.getElementById(id);
    if (!display) return;
    const svg = display.closest("svg");
    const stroke = svg ? svg.querySelector("path.gauge-fg") : null;
    if (stroke) {
      stroke.setAttribute("stroke-dasharray", "0, 100");
    }
    display.textContent = "--";
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
        const res = await fetch("/monitor", {
          cache: "no-store",
          headers: cborHeaders(),
        });
        if (res.status === 401) {
          noteAuthFailure();
          return;
        }
        if (!res.ok) return;
        resetAuthFailures();
        const data = await readCbor(res, {});

        // Relay sync
        const serverRelay = data.relay === true;
        setDot("relay", serverRelay);
        const relayToggle = document.getElementById("relayToggle");
        if (relayToggle) relayToggle.checked = serverRelay;

        // AC presence
        const ac = data.ac === true;
        setDot("ac", ac);

        // Voltage gauge (always show measured bus voltage)
        const v = Number(data.capVoltage);
        if (Number.isFinite(v)) {
          updateGauge("voltageValue", v, "V", 400);
        } else {
          setGaugeUnknown("voltageValue");
        }

        // Raw ADC display (scaled /100, e.g., 4095 -> 40.95)
        const adcEl = document.getElementById("adcRawValue");
        if (adcEl) {
          const rawScaled = parseFloat(data.capAdcRaw);
          adcEl.textContent = Number.isFinite(rawScaled)
            ? rawScaled.toFixed(2)
            : "--";
        }

        // Current gauge
        let rawCurrent = readAcsCurrent(data);
        if (!ac || !Number.isFinite(rawCurrent)) rawCurrent = 0;
        rawCurrent = Math.max(0, Math.min(100, rawCurrent));
        updateGauge("currentValue", rawCurrent, "A", 100);

        // Temperatures (up to 12 sensors)
        const temps = data.temperatures || [];
        for (let i = 0; i < 12; i++) {
          const id = "temp" + (i + 1) + "Value";
          const t = temps[i];
          const num = Number(t);
          if (
            t === undefined ||
            t === null ||
            Number.isNaN(num) ||
            num === -127
          ) {
            updateGauge(id, "Off", "\u00B0C", 150);
          } else {
            updateGauge(id, num, "\u00B0C", 150);
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
          setFanSpeedValue(data.fanSpeed);
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
      const payload = new Blob([encodeCbor({ action: "disconnect" })], {
        type: "application/cbor",
      });
      navigator.sendBeacon("/disconnect", payload);
    } catch (e) {
      fetch("/disconnect", {
        method: "POST",
        headers: cborHeaders(),
        body: encodeCbor({ action: "disconnect" }),
      });
    }
    try {
      redirectToLogin();
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

    initPowerButton();
    bindDeviceSettingsSubtabs();
    initDashboardClock();
    liveRender();
    scheduleLiveInterval();

    // Keep session alive with backend heartbeat
    startHeartbeat(4000);
    loadControls();
    loadDeviceInfo();
    bindSessionHistoryButton();
    bindErrorButton();
    bindLogButton();
    bindEventBadge();
    bindEventToast();
    bindCalibrationButton();
    bindSetupWizardControls();
    bindWizardControls();
    bindLiveControlButton();
    updateStatusBarState();
    updateModePills();
    startWireTestPoll();
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
        const desired = relayToggle.checked;
        relayToggle.disabled = true;

        if (
          guardUnsafeAction("toggling the relay", {
            blockAuto: true,
            blockCalib: true,
          })
        ) {
          relayToggle.checked = !desired;
          relayToggle.disabled = false;
          return;
        }

        const pre = lastMonitor || (await fetchMonitorSnapshot());
        if (pre) applyMonitorSnapshot(pre);

        const resp = await sendControlCommand("set", "relay", desired);
        if (resp && resp.error) {
          relayToggle.disabled = false;
          await pollLiveOnce();
          return;
        }

        const mon = await waitForMonitorMatch((m) => m && m.relay === desired);
        if (mon) applyMonitorSnapshot(mon);
        else await pollLiveOnce();

        relayToggle.disabled = false;
      });
    }

    // Fan slider manual control
    const fanSlider = document.getElementById("fanSlider");
    if (fanSlider) {
      fanSlider.addEventListener("input", async () => {
        setFanSpeedValue(fanSlider.value);
        if (
          guardUnsafeAction("adjusting fan speed", {
            blockAuto: true,
            blockCalib: true,
          })
        ) {
          return;
        }
        const speed = parseInt(fanSlider.value, 10) || 0;
        sendControlCommand("set", "fanSpeed", speed);
      });
      setFanSpeedValue(fanSlider.value);
    }

    const confirmBtn = document.getElementById("wiresCoolConfirmBtn");
    if (confirmBtn) {
      confirmBtn.addEventListener("click", confirmWiresCool);
    }

    const modelTarget = document.getElementById("modelParamTarget");
    if (modelTarget) {
      modelTarget.addEventListener("change", () => {
        updateModelParamFields(lastLoadedControls);
      });
    }

    fetchSetupStatus();

    // Ensure tooltips exist for all visible UI elements.
    applyTooltips(document);
  });

  // ========================================================
  // ===============      EXPORT GLOBAL API     =============
  // ========================================================

  window.switchTab = switchTab;
  window.toggleUserMenu = toggleUserMenu;
  window.toggleLT = toggleLT;
  window.toggleMute = toggleMute;
  window.handleOutputToggle = handleOutputToggle;
  window.updateOutputAccess = updateOutputAccess;
  window.openConfirm = openConfirm;
  window.saveUserSettings = saveUserSettings;
  window.resetUserSettings = resetUserSettings;
  window.saveAdminSettings = saveAdminSettings;
  window.resetAdminSettings = resetAdminSettings;
  window.saveApSettings = saveApSettings;
  window.resetApSettings = resetApSettings;
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
  window.closeErrorModal = closeErrorModal;
  window.openCalibrationModal = openCalibrationModal;
  window.closeCalibrationModal = closeCalibrationModal;
  window.openLiveControlModal = openLiveControlModal;
  window.closeLiveControlModal = closeLiveControlModal;
  window.updateSessionStatsUI = updateSessionStatsUI; // for backend to call later
})();
