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

