import { initCalibrationOverlay } from "../features/calibration/index.js";

function rand(min, max, decimals = 0) {
  const value = Math.random() * (max - min) + min;
  return Number(value.toFixed(decimals));
}

function clamp(value, min, max) {
  return Math.min(Math.max(value, min), max);
}

function nowLabel() {
  return new Date().toLocaleTimeString("en-US", {
    hour12: false,
    hour: "2-digit",
    minute: "2-digit",
    second: "2-digit",
  });
}

function formatTemp(value) {
  if (!Number.isFinite(value)) return "--";
  return `${value.toFixed(1)}\u00b0C`;
}

function formatGateLabel(gate) {
  return `Gate ${gate}`;
}

function formatWireLabel(gate) {
  return `Wire ${String(gate).padStart(2, "0")}`;
}

function parseGate(value, fallback = 1) {
  const gate = Number.parseInt(value, 10);
  if (!Number.isFinite(gate)) return fallback;
  return clamp(gate, 1, 10);
}

function parseTarget(value, fallback) {
  const target = Number.parseFloat(value);
  return Number.isFinite(target) ? target : fallback;
}

const X_POINTS = [0, 120, 240, 360, 480, 600, 720, 840, 960, 1080, 1200];
const CHART_TOP = 10;
const CHART_BOTTOM = 265;
const CHART_HEIGHT = CHART_BOTTOM - CHART_TOP;

function yForTemp(temp) {
  const safeTemp = clamp(temp, 0, 150);
  return CHART_TOP + (150 - safeTemp) * (CHART_HEIGHT / 150);
}

function buildLinePoints(endTemp) {
  const startTemp = clamp(endTemp - rand(3, 8, 1), 0, 150);
  return X_POINTS.map((x, index) => {
    const t = index / (X_POINTS.length - 1);
    const drift = Math.sin(index * 0.7) * 0.6;
    const temp = startTemp + (endTemp - startTemp) * t + drift;
    return `${x},${yForTemp(temp)}`;
  }).join(" ");
}

function getChartNodes(root) {
  if (!root) return null;
  return {
    root,
    setpointValue: root.querySelector("[data-live-setpoint]"),
    setpointLine: root.querySelector(".chart-line.setpoint"),
    wireTemps: Array.from(root.querySelectorAll("[data-live-wire-temp]")),
    wireLines: Array.from(root.querySelectorAll(".wire-line[data-wire]")),
    wireDots: Array.from(root.querySelectorAll(".wire-dot[data-wire]")),
    wireTargets: Array.from(
      root.querySelectorAll(
        ".wire-line[data-wire], .wire-dot[data-wire], .chart-pill.wire[data-wire]",
      ),
    ),
  };
}

function startMock() {
  const cal = window.__calibration || initCalibrationOverlay();
  if (!cal) return;

  const state = {
    scenario: "idle",
    startedAt: Date.now(),
    tick: 0,
    log: [],
    timer: null,
    activeWire: 3,
  };

  const chartRoots = {
    wire: document.querySelector('[data-setup-chart="wire"]'),
    floor: document.querySelector('[data-setup-chart="floor"]'),
  };
  const chartNodes = {
    wire: getChartNodes(chartRoots.wire),
    floor: getChartNodes(chartRoots.floor),
  };
  const wizardRoot = document.querySelector("[data-setup-wizard]");
  const wizardFieldNodes = wizardRoot
    ? Array.from(wizardRoot.querySelectorAll("[data-cal-field]"))
    : [];
  const wizardFields = new Map();

  wizardFieldNodes.forEach((node) => {
    const key = node.getAttribute("data-cal-field");
    if (!key) return;
    const list = wizardFields.get(key) || [];
    list.push(node);
    wizardFields.set(key, list);
  });

  const pushLog = (message) => {
    state.log.unshift(`[${nowLabel()}] ${message}`);
    if (state.log.length > 80) state.log.length = 80;
    cal.setLogLines(state.log);
  };

  const updateChart = (nodes, context) => {
  if (!nodes || !context) return;
  const root = nodes.root;
  if (!root) return;
  const wiresValue = context.wires || "";
  root.dataset.liveWires = wiresValue;
  const selected = wiresValue
    .split(",")
    .map((value) => value.trim())
    .filter(Boolean);
  const setpoint = Number.isFinite(context.setpoint) ? context.setpoint : null;
  const baseSetpoint = Number.isFinite(setpoint) ? setpoint : 0;

  if (nodes.wireTargets.length) {
    nodes.wireTargets.forEach((target) => {
      const id = target.dataset.wire;
      target.classList.toggle("is-selected", selected.includes(id));
      });
    }

  if (nodes.setpointValue) {
    nodes.setpointValue.textContent = Number.isFinite(setpoint)
      ? `${setpoint.toFixed(1)}\u00b0C`
      : "--\u00b0C";
  }
  if (nodes.setpointLine) {
    const setpointY = yForTemp(baseSetpoint);
    nodes.setpointLine.setAttribute(
      "points",
      X_POINTS.map((x) => `${x},${setpointY}`).join(" "),
    );
  }

    const tempsByWire = {};
    selected.forEach((id, index) => {
      const spread = (index - (selected.length - 1) / 2) * 1.6;
      const base = baseSetpoint + spread;
      const temp = clamp(base + rand(-3, 3, 1), 0, 150);
      tempsByWire[id] = temp;
    });

    nodes.wireTemps.forEach((node) => {
      const id = node.dataset.liveWireTemp;
      if (!id || !selected.includes(id)) {
        node.textContent = "--\u00b0C";
        return;
      }
      node.textContent = `${tempsByWire[id].toFixed(1)}\u00b0C`;
    });

    nodes.wireLines.forEach((line) => {
      const id = line.dataset.wire;
      const temp = tempsByWire[id];
      if (!Number.isFinite(temp)) return;
      line.setAttribute("points", buildLinePoints(temp));
    });

    nodes.wireDots.forEach((dot) => {
      const id = dot.dataset.wire;
      const temp = tempsByWire[id];
      if (!Number.isFinite(temp)) return;
      dot.setAttribute("cx", X_POINTS[X_POINTS.length - 1]);
      dot.setAttribute("cy", yForTemp(temp));
    });
  };

  const notify = (message, kind = "ok") => {
    cal.notify?.(message, kind);
  };

  const setWizardFields = (values = {}) => {
    if (!wizardFields.size) return;
    Object.entries(values).forEach(([key, value]) => {
      const nodes = wizardFields.get(key);
      if (!nodes?.length) return;
      const text =
        value === null || value === undefined || value === "" ? "--" : String(value);
      nodes.forEach((node) => {
        node.textContent = text;
      });
    });
  };

  const setPresenceAll = (builder) => {
    const items = Array.from({ length: 10 }, (_, index) => {
      const wire = index + 1;
      const label = `Wire ${String(wire).padStart(2, "0")}`;
      return builder(label, wire);
    });
    cal.setPresence(items);
  };

  const resetWizardFields = () => {
    cal.setFields({
      wizardWireStatus: "Idle",
      wizardWireState: "Idle",
      wizardWireGateActive: "--",
      wizardWireLinked: "--",
      wizardWireProgress: "0%",
      wizardWireR0: "--",
      wizardWireTau: "--",
      wizardWireK: "--",
      wizardWireC: "--",
      wizardNtcTempText: "--",
      wizardAdcZeroText: "--",
      wizardCapacitanceText: "--",
    });
    updateChart(chartNodes.wire, { wires: "", setpoint: null });
  };

  const resetFields = () => {
    cal.setFields({
      calibStatusText: "Idle",
      calibModeText: "--",
      calibCountText: "--",
      calibIntervalText: "1s",
      calibTempText: "--",
      calibWireText: "--",
      calibTargetText: "--",
      calibElapsedText: "--",
      wireTestState: "Idle",
      wireTestMode: "--",
      wireTestPurpose: "--",
      wireTestActiveWire: "--",
      wireTestNtcTemp: "--",
      wireTestModelTemp: "--",
      wireTestPacket: "--",
      wireTestFrame: "--",
      floorCalStageText: "--",
      floorCalRunningText: "--",
      floorCalDoneText: "--",
      floorCalTauText: "--",
      floorCalKText: "--",
      floorCalCText: "--",
      presenceProbeStatusText: "Idle",
    });

    resetWizardFields();
    setPresenceAll((label) => ({ label, value: "...", state: "off" }));
    updateChart(chartNodes.floor, { wires: "", setpoint: null });
  };

  const seedWizardPreview = () => {
    if (!wizardRoot) return;
    const previewGate = 4;
    const previewTarget = 38.5;
    setWizardFields({
      wizardWireStatus: "Running",
      wizardWireState: "Heating",
      wizardWireGateActive: formatGateLabel(previewGate),
      wizardWireLinked: formatGateLabel(previewGate),
      wizardWireProgress: "52%",
      wizardWireR0: "12.40",
      wizardWireTau: "96.0",
      wizardWireK: "0.78",
      wizardWireC: "1.86",
      floorCalStageText: "Heat",
      floorCalRunningText: "Yes",
      floorCalDoneText: "45%",
      floorCalTauText: "88.4",
      floorCalKText: "0.69",
      floorCalCText: "1.92",
    });
    updateChart(chartNodes.wire, {
      wires: String(previewGate),
      setpoint: previewTarget,
    });
    updateChart(chartNodes.floor, {
      wires: "1,2,3,4,5,6,7,8,9,10",
      setpoint: 42.0,
    });
  };

  const stopTimer = () => {
    if (state.timer) {
      clearInterval(state.timer);
      state.timer = null;
    }
  };

  const setWizardStatus = (status, stateLabel) => {
    cal.setFields({
      wizardWireStatus: status,
      wizardWireState: stateLabel ?? status,
    });
  };

  const setScenario = (scenario, inputs = {}) => {
    state.scenario = scenario;
    state.startedAt = Date.now();
    state.tick = 0;
    if (state.timer) {
      clearInterval(state.timer);
      state.timer = null;
    }

    if (scenario === "idle") {
      resetFields();
      pushLog("Calibration idle");
      return;
    }

    if (scenario === "sensors") {
      cal.setFields({
        calibStatusText: "Running",
        calibModeText: "Sensor zero",
        calibCountText: "1",
        calibIntervalText: "1s",
        calibTempText: "--",
        calibWireText: "--",
        calibTargetText: "--",
        calibElapsedText: "0s",
        wizardAdcZeroText: "--",
        wizardCapacitanceText: "--",
        wizardNtcTempText: formatTemp(rand(20, 28, 1)),
      });
      pushLog("Capacitance measurement started");
      pushLog("Current sensor zero calibration started");
      state.timer = setInterval(() => {
        state.tick += 1;
        cal.setFields({
          calibElapsedText: `${state.tick}s`,
          wizardNtcTempText: formatTemp(rand(20, 28, 1)),
        });
        if (state.tick === 2 && Math.random() > 0.88) {
          clearInterval(state.timer);
          state.timer = null;
          cal.setFields({
            calibStatusText: "Failed",
            calibModeText: "Sensor zero",
          });
          pushLog("Sensor zero failed: timeout");
          notify("Sensor zero failed", "err");
          return;
        }
        if (state.tick === 3) {
          const cap = rand(8, 16, 2);
          cal.setField("wizardCapacitanceText", `${cap.toFixed(2)} mF`);
          pushLog(`Capacitance measured: ${cap.toFixed(2)} mF`);
        }
        if (state.tick === 4) {
          const adcZero = rand(1200, 1800, 0);
          cal.setField("wizardAdcZeroText", String(adcZero));
          pushLog("Current sensor zero OK");
        }
        if (state.tick >= 6) {
          clearInterval(state.timer);
          state.timer = null;
          cal.setFields({
            calibStatusText: "Done",
            calibModeText: "Sensor zero",
          });
          pushLog("Sensor zero cycle complete");
          notify("Sensor zero complete", "ok");
        }
      }, 1000);
      return;
    }

    if (scenario === "model") {
      cal.setFields({
        calibStatusText: "Running",
        calibModeText: "Model calibration",
        calibIntervalText: "1s",
        calibCountText: "0",
        calibTempText: formatTemp(rand(20, 35, 1)),
        calibElapsedText: "0s",
      });
      pushLog("Temp model calibration started");
      state.timer = setInterval(() => {
        state.tick += 1;
        const count = state.tick * 2;
        const temp = clamp(rand(22, 150, 1), 0, 150);
        cal.setFields({
          calibCountText: String(count),
          calibTempText: formatTemp(temp),
          calibElapsedText: `${state.tick}s`,
        });
        if (state.tick % 4 === 0) pushLog(`Sample captured (T=${temp.toFixed(1)}C)`);
      }, 1000);
      return;
    }

    if (scenario === "wizard-wire-cal") {
      const gate = parseGate(inputs.wizardWireGate, state.activeWire || 1);
      const target = parseTarget(inputs.wizardWireTargetC, 38.5);
      state.activeWire = gate;

      cal.setFields({
        wizardWireStatus: "Running",
        wizardWireState: "Heating",
        wizardWireGateActive: formatGateLabel(gate),
        wizardWireLinked: formatGateLabel(gate),
        wizardWireProgress: "0%",
        wizardWireR0: "--",
        wizardWireTau: "--",
        wizardWireK: "--",
        wizardWireC: "--",
      });
      updateChart(chartNodes.wire, { wires: String(gate), setpoint: target });
      pushLog(
        `Wire calibration started (gate ${gate}, target ${target.toFixed(1)}C)`,
      );

      state.timer = setInterval(() => {
        state.tick += 1;
        const pct = clamp(Math.round((state.tick / 18) * 100), 0, 100);
        const phase = pct < 65 ? "Heating" : pct < 90 ? "Cooling" : "Settling";
        cal.setFields({
          wizardWireProgress: `${pct}%`,
          wizardWireState: pct >= 100 ? "Complete" : phase,
          wizardWireStatus: pct >= 100 ? "Done" : "Running",
        });
        updateChart(chartNodes.wire, { wires: String(gate), setpoint: target });
        if (pct >= 100) {
          const r0 = rand(8, 22, 2);
          const tau = rand(80, 140, 1);
          const k = rand(0.6, 1.4, 2);
          const c = rand(1.2, 2.4, 2);
          cal.setFields({
            wizardWireR0: r0.toFixed(2),
            wizardWireTau: tau.toFixed(1),
            wizardWireK: k.toFixed(2),
            wizardWireC: c.toFixed(2),
          });
          clearInterval(state.timer);
          state.timer = null;
        }
      }, 1000);
      return;
    }

    if (scenario === "wizard-wire-test") {
      const gate = parseGate(inputs.wizardWireGate, state.activeWire || 1);
      const target = parseTarget(inputs.wizardWireTestTargetC, 45);
      state.activeWire = gate;

      cal.setFields({
        wizardWireStatus: "Wire test",
        wizardWireState: "Holding",
        wizardWireGateActive: formatGateLabel(gate),
        wizardWireLinked: formatGateLabel(gate),
        wizardWireProgress: "Hold",
      });
      updateChart(chartNodes.wire, { wires: String(gate), setpoint: target });
      pushLog(`Wire test started (gate ${gate}, target ${target.toFixed(1)}C)`);

      state.timer = setInterval(() => {
        state.tick += 1;
        updateChart(chartNodes.wire, { wires: String(gate), setpoint: target });
        if (state.tick % 6 === 0) {
          pushLog(`Wire test stable at ${target.toFixed(1)}C`);
        }
      }, 1000);
      return;
    }

    if (scenario === "wire") {
      const target = parseTarget(inputs.wireTestTargetC, 38.5);
      state.activeWire = parseGate(inputs.wireTestWireIndex, state.activeWire || 3);

      cal.setFields({
        calibStatusText: "Running",
        calibModeText: "Wire test",
        calibIntervalText: "1s",
        calibCountText: "0",
        calibWireText: formatWireLabel(state.activeWire),
        calibTargetText: formatTemp(target),
        calibElapsedText: "0s",
      });
      cal.setFields({
        wireTestState: "Running",
        wireTestMode: "Manual",
        wireTestPurpose: "Calibration",
        wireTestActiveWire: formatWireLabel(state.activeWire),
        wireTestPacket: "0",
        wireTestFrame: "0",
      });
      pushLog(`Wire test started (target ${target.toFixed(1)}C)`);

      state.timer = setInterval(() => {
        state.tick += 1;
        const ntc = clamp(target - 12 + state.tick * 1.4 + rand(-0.6, 0.6, 1), 0, 150);
        const model = clamp(ntc + rand(-1.4, 1.4, 1), 0, 150);
        const packet = state.tick * 4;
        const frame = state.tick * 2;
        cal.setFields({
          calibCountText: String(packet),
          calibTempText: formatTemp(ntc),
          calibElapsedText: `${state.tick}s`,
          wireTestNtcTemp: formatTemp(ntc),
          wireTestModelTemp: formatTemp(model),
          wireTestPacket: String(packet),
          wireTestFrame: String(frame),
        });
      }, 1000);
      return;
    }

    if (scenario === "floor") {
      const targetC = Number.parseFloat(inputs.floorCalTargetC);
      const target = Number.isFinite(targetC) ? targetC : 42.0;
      const duty = Number.parseInt(inputs.floorCalDutyPct, 10);
      const dutyPct = Number.isFinite(duty) ? clamp(duty, 5, 100) : 50;
      cal.setFields({
        calibStatusText: "Running",
        calibModeText: "Floor calibration",
        calibTargetText: formatTemp(target),
        calibElapsedText: "0s",
      });
      cal.setFields({
        floorCalStageText: "Ambient",
        floorCalRunningText: "Yes",
        floorCalDoneText: "0%",
        floorCalTauText: "--",
        floorCalKText: "--",
        floorCalCText: "--",
      });
      pushLog(`Floor calibration started (target ${target.toFixed(1)}C, duty ${dutyPct}%)`);
      updateChart(chartNodes.floor, {
        wires: "1,2,3,4,5,6,7,8,9,10",
        setpoint: target,
      });

      state.timer = setInterval(() => {
        state.tick += 1;
        const pct = clamp(Math.round((state.tick / 24) * 100), 0, 100);
        const stage = pct < 20 ? "Ambient" : pct < 65 ? "Heat" : "Cool";
        const tau = 85 + rand(-8, 8, 1);
        const k = 0.72 + rand(-0.05, 0.05, 2);
        const c = 1.92 + rand(-0.12, 0.12, 2);
        cal.setFields({
          calibElapsedText: `${state.tick}s`,
          floorCalStageText: stage,
          floorCalDoneText: `${pct}%`,
          floorCalTauText: `${tau.toFixed(1)}`,
          floorCalKText: `${k.toFixed(2)}`,
          floorCalCText: `${c.toFixed(2)}`,
        });
        updateChart(chartNodes.floor, {
          wires: "1,2,3,4,5,6,7,8,9,10",
          setpoint: target,
        });
        if (pct > 35 && pct < 60 && state.tick % 7 === 0 && Math.random() > 0.9) {
          cal.setFields({
            calibStatusText: "Failed",
            floorCalRunningText: "No",
          });
          pushLog("Floor calibration failed: unstable readings");
          notify("Floor calibration failed", "err");
          clearInterval(state.timer);
          state.timer = null;
          return;
        }
        if (pct >= 100) {
          cal.setFields({
            calibStatusText: "Done",
            floorCalRunningText: "No",
          });
          pushLog("Floor calibration complete");
          notify("Floor calibration complete", "ok");
          clearInterval(state.timer);
          state.timer = null;
        }
      }, 1000);
      return;
    }

    if (scenario === "presence") {
      cal.setField("presenceProbeStatusText", "Scanning...");
      pushLog("Presence probe started");
      setPresenceAll((label) => ({ label, value: "...", state: "off" }));
      setTimeout(() => {
        setPresenceAll((label, wire) => {
          const r = wire % 3;
          if (r === 0) return { label, value: "NO WIRE", state: "nowire" };
          if (r === 1) return { label, value: "OFF", state: "off" };
          return { label, value: "ON", state: "on" };
        });
        cal.setField("presenceProbeStatusText", "Done");
        pushLog("Presence probe complete");
        notify("Presence probe complete", "ok");
      }, 800);
      return;
    }
  };

  const handleAction = (action, inputs) => {
    if (!action) return;

    if (action === "startModelCalibBtn") return setScenario("model", inputs);
    if (action === "stopCalibBtn") return setScenario("idle", inputs);
    if (action === "wireTestStartBtn") return setScenario("wire", inputs);
    if (action === "wireTestStopBtn") return setScenario("idle", inputs);
    if (action === "wizardWireLinkBtn") {
      const gate = parseGate(inputs.wizardWireGate, state.activeWire || 1);
      state.activeWire = gate;
      cal.setFields({
        wizardWireGateActive: formatGateLabel(gate),
        wizardWireLinked: formatGateLabel(gate),
      });
      setWizardStatus("Linked", "Idle");
      updateChart(chartNodes.wire, {
        wires: String(gate),
        setpoint: parseTarget(inputs.wizardWireTargetC, 38.5),
      });
      pushLog(`NTC linked to gate ${gate}`);
      return;
    }
    if (action === "wizardWireStartBtn") return setScenario("wizard-wire-cal", inputs);
    if (action === "wizardWireStopBtn") {
      stopTimer();
      setWizardStatus("Stopped");
      pushLog("Wire calibration stopped");
      return;
    }
    if (action === "wizardWireSaveBtn") {
      stopTimer();
      setWizardStatus("Saved");
      pushLog("Wire calibration saved");
      return;
    }
    if (action === "wizardWireDiscardBtn") {
      stopTimer();
      resetWizardFields();
      pushLog("Wire calibration discarded");
      return;
    }
    if (action === "wizardWireTestStartBtn") return setScenario("wizard-wire-test", inputs);
    if (action === "wizardWireTestStopBtn") {
      stopTimer();
      setWizardStatus("Stopped");
      pushLog("Wire test stopped");
      return;
    }
    if (action === "startFloorCalibBtn") return setScenario("floor", inputs);
    if (action === "presenceProbeBtn") return setScenario("presence", inputs);
    if (action === "capCurrentCalBtn") return setScenario("sensors", inputs);

    if (action === "logRefreshBtn") {
      pushLog("Log refreshed");
      pushLog(`Model status: ${state.scenario}`);
      return;
    }

    if (action === "logClearBtn") {
      state.log = [];
      cal.setLogLines(state.log);
      pushLog("Log cleared");
      return;
    }

    if (action === "calibHistoryRefreshBtn") {
      const base = Date.now();
      const options = Array.from({ length: 6 }, (_, idx) => {
        const d = new Date(base - idx * 86400000);
        return `Session ${d.getFullYear()}-${String(d.getMonth() + 1).padStart(2, "0")}-${String(d.getDate()).padStart(2, "0")}`;
      });
      cal.setHistoryOptions(options);
      pushLog("History list refreshed");
      return;
    }

    if (action === "calibHistoryLoadBtn") {
      pushLog(`Loaded ${inputs?.calibHistorySelect || "session"}`);
      return;
    }

    if (action === "calibClearBtn") {
      pushLog("Calibration data cleared");
      return;
    }

    if (action === "calibHistoryBtn") {
      pushLog("History loaded");
    }
  };

  window.addEventListener("calibration:action", (event) => {
    handleAction(event.detail?.action, event.detail?.inputs);
  });

  cal.setHistoryOptions([
    "Session 2026-01-21",
    "Session 2026-01-20",
    "Session 2026-01-19",
  ]);
  resetFields();
  seedWizardPreview();
  pushLog("Calibration overlay mock ready");
  notify("Calibration mock ready", "ok");

  window.__calMock = {
    setScenario,
    getScenario: () => state.scenario,
  };
}

function boot() {
  const ready = window.__uiReady;
  if (ready && typeof ready.then === "function") {
    ready.then(startMock);
    return;
  }
  window.addEventListener("ui:ready", startMock, { once: true });
}

if (document.readyState === "loading") {
  document.addEventListener("DOMContentLoaded", boot);
} else {
  boot();
}
