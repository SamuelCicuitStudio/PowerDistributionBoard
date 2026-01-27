function clamp(value, min, max) {
  return Math.min(Math.max(value, min), max);
}

const DEG_C = "\u00B0C";

function getFieldValue(name) {
  const node = document.querySelector(`[data-setup-field="${name}"]`);
  if (!node) return "";
  return String(node.value ?? "");
}

function getFieldTrimmed(name) {
  return getFieldValue(name).trim();
}

function getAccessNodes() {
  return Array.from(document.querySelectorAll("[data-setup-access]"));
}

function getActionNode(name) {
  return document.querySelector(`[data-setup-action="${name}"]`);
}

function getFieldNode(name) {
  return document.querySelector(`[data-setup-field="${name}"]`);
}

function setFieldValue(name, value) {
  const node = getFieldNode(name);
  if (!node) return;
  const next = value === null || value === undefined ? "" : String(value);
  if (node.tagName === "SELECT") {
    node.value = next;
  } else {
    node.value = next;
  }
}

function setNumberField(name, value) {
  const node = getFieldNode(name);
  if (!node) return;
  if (value === null || value === undefined || !Number.isFinite(Number(value))) {
    node.value = "";
    return;
  }
  node.value = String(value);
}

function readNumberField(name) {
  const raw = getFieldTrimmed(name);
  if (!raw) return null;
  const num = Number(raw);
  if (!Number.isFinite(num)) return null;
  return num;
}

function getModelTarget() {
  const node = getFieldNode("modelTarget");
  return node ? String(node.value || "wire1") : "wire1";
}

function setCalText(name, value) {
  const node = document.querySelector(
    `[data-cal-field="${name}"][data-cal-scope="wizard"]`,
  );
  if (!node) return;
  node.textContent = value;
}

function getCalInputNode(name) {
  return document.querySelector(
    `[data-cal-input="${name}"][data-cal-scope="wizard"]`,
  );
}

function getCalInputValue(name) {
  const node = getCalInputNode(name);
  if (!node) return "";
  return String(node.value ?? "");
}

function readCalInputNumber(name) {
  const raw = getCalInputValue(name).trim();
  if (!raw) return null;
  const num = Number(raw);
  if (!Number.isFinite(num)) return null;
  return num;
}

function setCalInputValue(name, value) {
  const node = getCalInputNode(name);
  if (!node) return;
  const next = value === null || value === undefined ? "" : String(value);
  node.value = next;
}

function formatNumber(value, digits = 2) {
  const num = Number(value);
  if (!Number.isFinite(num)) return "--";
  return num.toFixed(digits);
}

function formatPercent(value) {
  const num = Number(value);
  if (!Number.isFinite(num)) return "--";
  return `${Math.round(num)}%`;
}

function formatTemp(value, digits = 1) {
  const formatted = formatNumber(value, digits);
  if (formatted === "--") return `--${DEG_C}`;
  return `${formatted}${DEG_C}`;
}

function getPresenceItems() {
  return Array.from(document.querySelectorAll("[data-presence-wire]"));
}

function updatePresenceGrid(presentList) {
  if (!Array.isArray(presentList)) return;
  const labelOk = "Connected";
  const labelBad = "Not connected";
  getPresenceItems().forEach((node) => {
    const wire = Number(node.dataset.presenceWire || "");
    if (!Number.isFinite(wire) || wire <= 0) return;
    const span = node.querySelector("span");
    if (!span) return;
    const present = presentList[wire - 1];
    if (typeof present === "boolean") {
      span.textContent = present ? labelOk : labelBad;
    } else {
      span.textContent = "--";
    }
  });
}

function toInt(value, fallback = 0) {
  const num = Number(value);
  return Number.isFinite(num) ? Math.trunc(num) : fallback;
}

async function fetchCbor(path, options = {}) {
  const doFetch = window.pbFetch || fetch;
  const headers = window.pbCborHeaders
    ? window.pbCborHeaders(options.cbor || {})
    : options.headers;
  const response = await doFetch(path, { ...options, headers });
  const payload = window.pbReadCbor ? await window.pbReadCbor(response) : null;
  return { response, payload: payload || null };
}

async function postControl(target, value) {
  if (!window.pbEncodeCbor) return null;
  const body = window.pbEncodeCbor({ action: "set", target, value });
  return fetchCbor("/control", { method: "POST", body });
}

async function postSetupUpdate(payload) {
  if (!window.pbEncodeCbor) return null;
  const body = window.pbEncodeCbor(payload);
  return fetchCbor("/setup_update", { method: "POST", body });
}

async function postApConfig(payload) {
  if (!window.pbEncodeCbor) return null;
  const body = window.pbEncodeCbor(payload);
  return fetchCbor("/ap_config", { method: "POST", body });
}

async function postCbor(path, payload = {}) {
  if (!window.pbEncodeCbor) return null;
  const body = window.pbEncodeCbor(payload);
  return fetchCbor(path, { method: "POST", body });
}

export function initSetupWizardLinkage() {
  let wizard = null;
  let lastStage = 0;
  let lastIndex = 0;
  let maxIndex = 9;
  let controlsCache = null;
  let monitorTimer = null;
  let monitorBusy = false;
  let wireStepRoot = null;
  let calibTimer = null;
  let calibBusy = false;
  let wireTestBusy = false;
  let calibDataBusy = false;
  let lastCalibStatus = null;
  let lastWireTestStatus = null;
  let lastCalibCount = 0;
  let lastCalibMode = "";
  let calListenerBound = false;
  let floorCalFloorAmb = null;
  let floorCalRoomAmb = null;
  let finishBusy = false;

  const applyModelFields = () => {
    if (!controlsCache) return;
    const target = getModelTarget();
    if (target === "floor") {
      setNumberField("modelTau", controlsCache.floorTau);
      setNumberField("modelK", controlsCache.floorK);
      setNumberField("modelC", controlsCache.floorC);
      return;
    }
    const match = target.match(/wire(\d+)/);
    const index = match ? match[1] : "1";
    const tau = controlsCache.wireTau?.[index];
    const k = controlsCache.wireK?.[index];
    const c = controlsCache.wireC?.[index];
    setNumberField("modelTau", tau);
    setNumberField("modelK", k);
    setNumberField("modelC", c);
  };

  const applyFloorModelFields = () => {
    if (!controlsCache) return;
    setCalText("floorCalTauText", formatNumber(controlsCache.floorTau, 3));
    setCalText("floorCalKText", formatNumber(controlsCache.floorK, 3));
    setCalText("floorCalCText", formatNumber(controlsCache.floorC, 3));
  };

  const getWireStepRoot = () =>
    wireStepRoot || document.querySelector('[data-setup-step="8"]');

  const toGateLabel = (gate) => {
    if (!Number.isFinite(Number(gate)) || Number(gate) <= 0) return "--";
    return String(Math.trunc(Number(gate)));
  };

  const getWizardWireGate = () => {
    const raw = readCalInputNumber("wizardWireGate");
    if (raw === null) return null;
    const gate = Math.round(raw);
    return clamp(gate, 1, 10);
  };

  const getSelectedWireGate = () => {
    const gate = getWizardWireGate();
    if (gate) return gate;
    const cached = toInt(controlsCache?.ntcGateIndex, 0);
    return cached > 0 ? cached : null;
  };

  const updateWireR0 = (wireIndex) => {
    if (!wireIndex || !controlsCache?.wireRes) {
      setCalText("wizardWireR0", "--");
      return;
    }
    const key = String(wireIndex);
    const value = controlsCache.wireRes?.[key];
    setCalText("wizardWireR0", formatNumber(value, 2));
  };

  const setWireLinkedGate = (wireIndex) => {
    setCalText("wizardWireLinked", toGateLabel(wireIndex));
    updateWireR0(wireIndex);
  };

  const setWireActiveGate = (wireIndex) => {
    setCalText("wizardWireGateActive", toGateLabel(wireIndex));
    if (wireIndex) updateWireR0(wireIndex);
  };

  const setWireSetpoint = (value) => {
    const root = getWireStepRoot();
    if (!root) return;
    const node = root.querySelector("[data-live-setpoint]");
    if (!node) return;
    node.textContent = formatTemp(value, 1);
  };

  const setWireTemp = (wireIndex, value) => {
    const root = getWireStepRoot();
    if (!root || !wireIndex) return;
    const node = root.querySelector(
      `[data-live-wire-temp="${wireIndex}"]`,
    );
    if (!node) return;
    node.textContent = formatTemp(value, 1);
  };

  const applyStatus = (status) => {
    if (!wizard || !status) return;
    const setupDone = Boolean(status.setupDone);
    const stage = clamp(toInt(status.stage, 0), 0, maxIndex);
    lastStage = Math.max(lastStage, stage);
    lastIndex = stage;

    if (!setupDone) {
      wizard.setLocked(true);
      wizard.open(stage);
      return;
    }
    wizard.setLocked(false);
    wizard.close();
  };

  const applyControls = (controls) => {
    if (!controls) return;
    controlsCache = controls;
    const uiLanguage = String(controls.uiLanguage || "").trim();
    if (uiLanguage && window.__i18n?.setLang) {
      window.__i18n.setLang(uiLanguage);
    }
    if (controls.outputAccess && typeof controls.outputAccess === "object") {
      const access = controls.outputAccess;
      getAccessNodes().forEach((node) => {
        const index = String(node.dataset.setupAccess || "").trim();
        if (!index) return;
        const key = `output${index}`;
        if (Object.prototype.hasOwnProperty.call(access, key)) {
          node.checked = Boolean(access[key]);
        }
      });
    }

    setNumberField("acFrequency", controls.acFrequency);
    setNumberField("chargeResistor", controls.chargeResistor);
    setNumberField("currLimit", controls.currLimit);
    if (controls.currentSource !== undefined) {
      const source =
        String(controls.currentSource).toLowerCase().includes("acs") ||
        Number(controls.currentSource) === 1
          ? "acs"
          : "estimate";
      setFieldValue("currentSource", source);
    }
    setNumberField("tempWarnC", controls.tempWarnC);
    setNumberField("tempTripC", controls.tempTripC);
    setNumberField("floorThicknessMm", controls.floorThicknessMm);
    setNumberField("floorMaxC", controls.floorMaxC);
    setNumberField("floorSwitchMarginC", controls.floorSwitchMarginC);
    setNumberField("nichromeFinalTempC", controls.nichromeFinalTempC);
    setNumberField("ntcGateIndex", controls.ntcGateIndex);
    setNumberField("presenceMinRatioPct", controls.presenceMinRatioPct);
    if (controls.floorMaterial) {
      setFieldValue("floorMaterial", controls.floorMaterial);
    }
    setNumberField("ntcBeta", controls.ntcBeta);
    setNumberField("ntcT0C", controls.ntcT0C);
    setNumberField("ntcR0", controls.ntcR0);
    setNumberField("ntcFixedRes", controls.ntcFixedRes);
    setNumberField("wireOhmPerM", controls.wireOhmPerM);
    setNumberField("wireGauge", controls.wireGauge);
    if (controls.wireRes && typeof controls.wireRes === "object") {
      for (let i = 1; i <= 10; i += 1) {
        const key = String(i);
        if (Object.prototype.hasOwnProperty.call(controls.wireRes, key)) {
          setNumberField(`wireRes${i}`, controls.wireRes[key]);
        }
      }
    }

    const setupWireIndex = toInt(controls.setupWireIndex, 0);
    const gate = setupWireIndex > 0 ? setupWireIndex : toInt(controls.ntcGateIndex, 0);
    if (gate > 0) {
      setCalInputValue("wizardWireGate", gate);
      setWireLinkedGate(gate);
      setWireActiveGate(gate);
    } else {
      updateWireR0(getSelectedWireGate());
    }

    applyModelFields();
    applyFloorModelFields();
    document.dispatchEvent(new Event("setup:controls-loaded"));
  };

  const loadSetupStatus = async () => {
    try {
      const { response, payload } = await fetchCbor("/setup_status", {
        method: "GET",
        cbor: { body: false },
      });
      if (response.ok && payload) {
        applyStatus(payload);
        return payload;
      }
    } catch (error) {
      console.warn("setup_status failed", error);
    }
    return null;
  };

  const loadControls = async () => {
    try {
      const { response, payload } = await fetchCbor("/load_controls", {
        method: "GET",
        cbor: { body: false },
      });
      if (response.ok && payload) {
        applyControls(payload);
        return payload;
      }
    } catch (error) {
      console.warn("load_controls failed", error);
    }
    return null;
  };

  const applyCalibStatus = (status) => {
    if (!status) return;
    lastCalibStatus = status;
    const mode = String(status.mode || "").toLowerCase();
    const running = Boolean(status.running);
    if (mode !== "model") {
      if (!running) {
        setCalText("wizardWireStatus", "Idle");
        setCalText("wizardWireState", "Idle");
        setCalText("wizardWireProgress", "--");
      }
      return;
    }

    setCalText("wizardWireStatus", running ? "Running" : "Idle");
    setCalText("wizardWireState", running ? "Running" : "Idle");
    setCalText("wizardWireProgress", formatPercent(status.progress_pct));

    const activeGate = toInt(
      status.progress_wire ?? status.wire_index ?? status.result_wire,
      0,
    );
    if (activeGate > 0) {
      setWireActiveGate(activeGate);
    } else {
      setWireActiveGate(getSelectedWireGate());
    }

    setCalText("wizardWireTau", formatNumber(status.result_tau, 3));
    setCalText("wizardWireK", formatNumber(status.result_k, 3));
    setCalText("wizardWireC", formatNumber(status.result_c, 3));

    if (status.target_c !== undefined) {
      setWireSetpoint(status.target_c);
    }
  };

  const applyWireTestStatus = (status) => {
    if (!status) return;
    lastWireTestStatus = status;
    const purpose = String(status.purpose || "").toLowerCase();
    const running = Boolean(status.running);
    if (!running || purpose !== "wire_test") return;

    setCalText("wizardWireStatus", "Wire test");
    setCalText("wizardWireState", "Running");
    setCalText("wizardWireProgress", "--");

    const activeGate = toInt(status.active_wire, 0);
    if (activeGate > 0) {
      setWireActiveGate(activeGate);
      if (status.active_temp_c !== undefined) {
        setWireTemp(activeGate, status.active_temp_c);
      }
    }

    if (status.target_c !== undefined) {
      setWireSetpoint(status.target_c);
    }
    if (status.ntc_temp_c !== undefined) {
      setCalText("wizardNtcTempText", formatNumber(status.ntc_temp_c, 1));
    }
  };

  const loadCalibData = async (status) => {
    if (calibDataBusy || !status) return;
    const mode = String(status.mode || "").toLowerCase();
    if (mode !== "model" && mode !== "floor") return;
    const count = toInt(status.count, 0);
    if (count <= 0) return;
    if (count < lastCalibCount) {
      lastCalibCount = 0;
      floorCalFloorAmb = null;
      floorCalRoomAmb = null;
    }
    if (count === lastCalibCount && mode === lastCalibMode) return;
    calibDataBusy = true;
    lastCalibCount = count;
    lastCalibMode = mode;
    const offset = count > 200 ? count - 200 : 0;
    try {
      const { response, payload } = await fetchCbor(
        `/calib_data?offset=${offset}&count=200`,
        { method: "GET", cbor: { body: false } },
      );
      if (response.ok && payload) {
        const samples = Array.isArray(payload.samples) ? payload.samples : [];
        if (samples.length) {
          const first = samples[0];
          const last = samples[samples.length - 1];
          if (mode === "model") {
            const wireIndex = toInt(
              payload.meta?.wire_index ?? status.wire_index ?? status.progress_wire,
              0,
            );
            if (wireIndex > 0) {
              setWireTemp(wireIndex, last.temp_c);
            }
            if (payload.meta?.target_c !== undefined) {
              setWireSetpoint(payload.meta.target_c);
            }
          } else if (mode === "floor") {
            if (floorCalFloorAmb === null && first.temp_c !== undefined) {
              floorCalFloorAmb = first.temp_c;
            }
            if (floorCalRoomAmb === null && first.room_c !== undefined) {
              floorCalRoomAmb = first.room_c;
            }
            setCalText("floorCalFloorTempText", formatNumber(last.temp_c, 1));
            setCalText("floorCalRoomTempText", formatNumber(last.room_c, 1));
            setCalText("floorCalBusVoltText", formatNumber(last.v, 2));
            setCalText(
              "floorCalFloorAmbText",
              formatNumber(floorCalFloorAmb, 1),
            );
            setCalText(
              "floorCalRoomAmbText",
              formatNumber(floorCalRoomAmb, 1),
            );
          }
        }
      }
    } catch (error) {
      console.warn("calib_data failed", error);
    } finally {
      calibDataBusy = false;
    }
  };

  const updateCalibStatus = async () => {
    if (calibBusy) return;
    if (!wizard || !wizard.isOpen() || (lastIndex !== 7 && lastIndex !== 8)) return;
    calibBusy = true;
    try {
      const { response, payload } = await fetchCbor("/calib_status", {
        method: "GET",
        cbor: { body: false },
      });
      if (response.ok && payload) {
        if (lastIndex === 7) {
          applyCalibStatus(payload);
        }
        await loadCalibData(payload);
      }
    } catch (error) {
      console.warn("calib_status failed", error);
    } finally {
      calibBusy = false;
    }
  };

  const updateWireTestStatus = async () => {
    if (wireTestBusy) return;
    if (!wizard || !wizard.isOpen() || lastIndex !== 7) return;
    wireTestBusy = true;
    try {
      const { response, payload } = await fetchCbor("/wire_test_status", {
        method: "GET",
        cbor: { body: false },
      });
      if (response.ok && payload) {
        applyWireTestStatus(payload);
      }
    } catch (error) {
      console.warn("wire_test_status failed", error);
    } finally {
      wireTestBusy = false;
    }
  };

  const pollWizardCalibration = () => {
    updateCalibStatus();
    updateWireTestStatus();
  };

  const startCalibPoll = () => {
    if (calibTimer) return;
    calibTimer = setInterval(pollWizardCalibration, 1500);
    pollWizardCalibration();
  };

  const stopCalibPoll = () => {
    if (!calibTimer) return;
    clearInterval(calibTimer);
    calibTimer = null;
    lastCalibCount = 0;
    lastCalibMode = "";
  };

  const updateMonitorReadouts = async () => {
    if (monitorBusy) return;
    if (!wizard || !wizard.isOpen()) return;
    monitorBusy = true;
    try {
      const { response, payload } = await fetchCbor("/monitor", {
        method: "GET",
        cbor: { body: false },
      });
      if (response.ok && payload) {
        if (payload.currentAcs !== undefined) {
          const currentAcs = Number(payload.currentAcs);
          setCalText(
            "wizardAdcZeroText",
            Number.isFinite(currentAcs) ? currentAcs.toFixed(3) : "--",
          );
        }
        if (payload.capacitanceF !== undefined) {
          const cap = Number(payload.capacitanceF);
          setCalText(
            "wizardCapacitanceText",
            Number.isFinite(cap) ? cap.toFixed(6) : "--",
          );
        }
        const floorTemp = payload.floor?.temp_c;
        if (floorTemp !== undefined) {
          const temp = Number(floorTemp);
          setCalText(
            "wizardNtcTempText",
            Number.isFinite(temp) ? temp.toFixed(1) : "--",
          );
        }
      }
    } catch (error) {
      console.warn("monitor fetch failed", error);
    } finally {
      monitorBusy = false;
    }
  };

  const startMonitor = () => {
    if (monitorTimer) return;
    monitorTimer = setInterval(updateMonitorReadouts, 1500);
    updateMonitorReadouts();
    startCalibPoll();
  };

  const stopMonitor = () => {
    if (!monitorTimer) return;
    clearInterval(monitorTimer);
    monitorTimer = null;
    stopCalibPoll();
  };

  const refresh = async () => {
    await loadControls();
    await loadSetupStatus();
  };

  const onStepChange = async (index) => {
    const nextIndex = clamp(toInt(index, 0), 0, maxIndex);
    const prevIndex = lastIndex;
    const forward = nextIndex > prevIndex;
    lastIndex = nextIndex;
    if (!forward) return;

    if (prevIndex === 1) {
      const userCurrent = getFieldValue("userCurrent");
      const userNewPass = getFieldValue("userNewPass");
      const userNewId = getFieldTrimmed("userNewId");
      if (userCurrent || userNewPass || userNewId) {
        await postControl("userCredentials", {
          current: userCurrent,
          newPass: userNewPass,
          newId: userNewId,
        });
      }

      const adminCurrent = getFieldValue("adminCurrent");
      const adminUsername = getFieldTrimmed("adminUsername");
      const adminPassword = getFieldValue("adminPassword");
      if (adminCurrent || adminUsername || adminPassword) {
        await postControl("adminCredentials", {
          current: adminCurrent,
          username: adminUsername,
          password: adminPassword,
        });
      }
    }

    if (prevIndex === 2) {
      const stationSsid = getFieldTrimmed("wifiStationSsid");
      const stationPass = getFieldValue("wifiStationPass");
      if (stationSsid) {
        await postControl("wifiSSID", stationSsid);
      }
      if (stationPass) {
        await postControl("wifiPassword", stationPass);
      }

      const apSsid = getFieldTrimmed("wifiApSsid");
      const apPass = getFieldValue("wifiApPass");
      if (apSsid || apPass) {
        await postApConfig({
          apSSID: apSsid,
          apPassword: apPass,
        });
      }
    }

    if (prevIndex === 3) {
      for (const node of getAccessNodes()) {
        const index = String(node.dataset.setupAccess || "").trim();
        if (!index) continue;
        await postControl(`Access${index}`, Boolean(node.checked));
      }
    }

    if (prevIndex === 4) {
      const acFrequency = readNumberField("acFrequency");
      if (acFrequency !== null) await postControl("acFrequency", acFrequency);
      const chargeResistor = readNumberField("chargeResistor");
      if (chargeResistor !== null) await postControl("chargeResistor", chargeResistor);
      const currLimit = readNumberField("currLimit");
      if (currLimit !== null) await postControl("currLimit", currLimit);
      const currentSource = getFieldTrimmed("currentSource");
      if (currentSource) await postControl("currentSource", currentSource);
      const tempWarnC = readNumberField("tempWarnC");
      if (tempWarnC !== null) await postControl("tempWarnC", tempWarnC);
      const tempTripC = readNumberField("tempTripC");
      if (tempTripC !== null) await postControl("tempTripC", tempTripC);

      const floorThicknessMm = readNumberField("floorThicknessMm");
      if (floorThicknessMm !== null) await postControl("floorThicknessMm", floorThicknessMm);
      const floorMaxC = readNumberField("floorMaxC");
      if (floorMaxC !== null) await postControl("floorMaxC", floorMaxC);
      const floorSwitchMarginC = readNumberField("floorSwitchMarginC");
      if (floorSwitchMarginC !== null) {
        await postControl("floorSwitchMarginC", floorSwitchMarginC);
      }
      const nichromeFinalTempC = readNumberField("nichromeFinalTempC");
      if (nichromeFinalTempC !== null) {
        await postControl("nichromeFinalTempC", nichromeFinalTempC);
      }
      const ntcGateIndex = readNumberField("ntcGateIndex");
      if (ntcGateIndex !== null) await postControl("ntcGateIndex", ntcGateIndex);
      const floorMaterial = getFieldTrimmed("floorMaterial");
      if (floorMaterial) await postControl("floorMaterial", floorMaterial);

      const wireOhmPerM = readNumberField("wireOhmPerM");
      if (wireOhmPerM !== null) await postControl("wireOhmPerM", wireOhmPerM);
      for (let i = 1; i <= 10; i += 1) {
        const wireRes = readNumberField(`wireRes${i}`);
        if (wireRes !== null) await postControl(`wireRes${i}`, wireRes);
      }
      const wireGauge = readNumberField("wireGauge");
      if (wireGauge !== null) await postControl("wireGauge", wireGauge);

      const modelTau = readNumberField("modelTau");
      const modelK = readNumberField("modelK");
      const modelC = readNumberField("modelC");
      if (modelTau !== null || modelK !== null || modelC !== null) {
        const target = getModelTarget();
        if (target === "floor") {
          if (modelTau !== null) await postControl("floorTau", modelTau);
          if (modelK !== null) await postControl("floorK", modelK);
          if (modelC !== null) await postControl("floorC", modelC);
        } else {
          const match = target.match(/wire(\d+)/);
          const index = match ? match[1] : "1";
          if (modelTau !== null) await postControl(`wireTau${index}`, modelTau);
          if (modelK !== null) await postControl(`wireK${index}`, modelK);
          if (modelC !== null) await postControl(`wireC${index}`, modelC);
        }
      }
    }

    if (prevIndex === 6) {
      const ntcBeta = readNumberField("ntcBeta");
      if (ntcBeta !== null) await postControl("ntcBeta", ntcBeta);
      const ntcT0C = readNumberField("ntcT0C");
      if (ntcT0C !== null) await postControl("ntcT0C", ntcT0C);
      const ntcR0 = readNumberField("ntcR0");
      if (ntcR0 !== null) await postControl("ntcR0", ntcR0);
      const ntcFixedRes = readNumberField("ntcFixedRes");
      if (ntcFixedRes !== null) await postControl("ntcFixedRes", ntcFixedRes);
    }

    const nextStage = Math.max(lastStage, nextIndex);
    if (nextStage === lastStage) return;
    lastStage = nextStage;
    await postSetupUpdate({ stage: nextStage });
  };

  const onLanguageChange = async () => {
    const lang = window.__i18n?.getLang?.() || document.documentElement.lang;
    if (!lang) return;
    await postControl("uiLanguage", lang);
  };

  const attemptFinish = async () => {
    if (finishBusy) return;
    finishBusy = true;
    try {
      const { response, payload } = await fetchCbor("/setup_status", {
        method: "GET",
        cbor: { body: false },
      });
      if (!response.ok || !payload) return;
      if (payload.configOk && payload.calibOk) {
        await postSetupUpdate({ setup_done: true });
      }
      await loadSetupStatus();
    } catch (error) {
      console.warn("setup finish failed", error);
    } finally {
      finishBusy = false;
    }
  };

  const handleCalibrationAction = async (event) => {
    if (!wizard || !wizard.isOpen()) return;
    const detail = event.detail || {};
    const action = String(detail.action || "");
    if (!action) return;
    const inputs = detail.inputs || {};

    const isWireStep = lastIndex === 7;
    const isFloorStep = lastIndex === 8;
    if (!isWireStep && !isFloorStep) return;

    const gate = getWizardWireGate();

    const updateSetupGate = async (wireIndex) => {
      if (!wireIndex) return;
      await postSetupUpdate({
        stage: Math.max(lastStage, lastIndex),
        wire_index: wireIndex,
      });
    };

    if (action === "wizardWireLinkBtn") {
      if (!isWireStep) return;
      if (!gate) return;
      await postControl("ntcGateIndex", gate);
      setWireLinkedGate(gate);
      setWireActiveGate(gate);
      await updateSetupGate(gate);
      return;
    }

    if (action === "wizardWireStartBtn") {
      if (!isWireStep) return;
      if (!gate) return;
      const targetRaw =
        inputs.wizardWireTargetC ?? getCalInputValue("wizardWireTargetC");
      const targetC = Number(targetRaw);
      if (!Number.isFinite(targetC) || targetC <= 0) return;
      await postControl("ntcGateIndex", gate);
      await postCbor("/calib_start", {
        mode: "model",
        wire_index: gate,
        target_c: targetC,
      });
      lastCalibCount = 0;
      lastCalibMode = "";
      setWireSetpoint(targetC);
      await updateSetupGate(gate);
      return;
    }

    if (action === "wizardWireStopBtn") {
      if (!isWireStep) return;
      await postCbor("/calib_stop", {});
      return;
    }

    if (action === "wizardWireSaveBtn") {
      if (!isWireStep) return;
      const resultWire = toInt(lastCalibStatus?.result_wire, 0);
      const wireIndex = resultWire > 0 ? resultWire : gate;
      if (!wireIndex) return;
      const tau = Number(lastCalibStatus?.result_tau);
      const k = Number(lastCalibStatus?.result_k);
      const c = Number(lastCalibStatus?.result_c);
      if (!Number.isFinite(tau) || !Number.isFinite(k) || !Number.isFinite(c)) {
        return;
      }
      await postControl(`wireTau${wireIndex}`, tau);
      await postControl(`wireK${wireIndex}`, k);
      await postControl(`wireC${wireIndex}`, c);
      await postControl(`wireCalibrated${wireIndex}`, true);
      return;
    }

    if (action === "wizardWireDiscardBtn") {
      if (!isWireStep) return;
      setCalText("wizardWireTau", "--");
      setCalText("wizardWireK", "--");
      setCalText("wizardWireC", "--");
      setCalText("wizardWireProgress", "--");
      setCalText("wizardWireStatus", "Idle");
      setCalText("wizardWireState", "Idle");
      lastCalibStatus = null;
      lastCalibCount = 0;
      lastCalibMode = "";
      return;
    }

    if (action === "wizardWireTestStartBtn") {
      if (!isWireStep) return;
      const targetRaw =
        inputs.wizardWireTestTargetC ?? getCalInputValue("wizardWireTestTargetC");
      const targetC = Number(targetRaw);
      if (!Number.isFinite(targetC) || targetC <= 0) return;
      if (gate) {
        await postControl("ntcGateIndex", gate);
      }
      await postCbor("/wire_test_start", { target_c: targetC });
      setWireSetpoint(targetC);
      if (gate) await updateSetupGate(gate);
      return;
    }

    if (action === "wizardWireTestStopBtn") {
      if (!isWireStep) return;
      await postCbor("/wire_test_stop", {});
      return;
    }

    if (action === "presenceProbeBtn") {
      if (!isWireStep) return;
      const ratioRaw =
        inputs.wizardPresenceRatio ?? getCalInputValue("wizardPresenceRatio");
      const ratio = Number(ratioRaw);
      const payload = {};
      if (Number.isFinite(ratio) && ratio > 0) {
        payload.presenceMinRatioPct = ratio;
        await postControl("presenceMinRatioPct", ratio);
      }
      const result = await postCbor("/presence_probe", payload);
      if (result?.response?.ok && result.payload?.wirePresent) {
        updatePresenceGrid(result.payload.wirePresent);
      }
      return;
    }

    if (action === "startFloorCalibBtn") {
      if (!isFloorStep) return;
      floorCalFloorAmb = null;
      floorCalRoomAmb = null;
      lastCalibCount = 0;
      lastCalibMode = "";
      await postCbor("/calib_start", { mode: "floor" });
      return;
    }

    if (action === "floorCalSaveBtn") {
      if (!isFloorStep) return;
      await postControl("floorCalibrated", true);
      await loadControls();
      applyFloorModelFields();
      return;
    }
  };

  const attach = (instance) => {
    wizard = instance;
    const steps = document.querySelectorAll("[data-setup-step]");
    if (steps.length) {
      maxIndex = Math.max(0, steps.length - 1);
    }
    wireStepRoot = document.querySelector('[data-setup-step="8"]');
    const modelTarget = getFieldNode("modelTarget");
    if (modelTarget) {
      modelTarget.addEventListener("change", applyModelFields);
    }
    const capBtn = getActionNode("sensorCap");
    if (capBtn) {
      capBtn.addEventListener("click", () => {
        postControl("calibrate", true);
      });
    }
    const acsBtn = getActionNode("sensorAcs");
    if (acsBtn) {
      acsBtn.addEventListener("click", () => {
        postControl("calibrate", true);
      });
    }
    const wireGate = getCalInputNode("wizardWireGate");
    if (wireGate) {
      wireGate.addEventListener("change", () => {
        const gate = getWizardWireGate();
        if (!gate) return;
        setWireLinkedGate(gate);
        setWireActiveGate(gate);
        if (wizard?.isOpen() && lastIndex === 7) {
          postSetupUpdate({
            stage: Math.max(lastStage, lastIndex),
            wire_index: gate,
          });
        }
      });
    }
    if (!calListenerBound) {
      window.addEventListener("calibration:action", handleCalibrationAction);
      calListenerBound = true;
    }
    const finishBtn = document.querySelector("[data-setup-next]");
    if (finishBtn) {
      finishBtn.addEventListener("click", () => {
        if (!wizard?.isOpen()) return;
        if (lastIndex !== maxIndex) return;
        attemptFinish();
      });
    }
    refresh();
  };

  document.addEventListener("language:change", onLanguageChange);

  return {
    attach,
    onStepChange,
    refresh,
    onOpen: startMonitor,
    onClose: stopMonitor,
  };
}
