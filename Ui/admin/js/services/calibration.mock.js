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

  const pushLog = (message) => {
    state.log.unshift(`[${nowLabel()}] ${message}`);
    if (state.log.length > 80) state.log.length = 80;
    cal.setLogLines(state.log);
  };

  const notify = (message, kind = "ok") => {
    cal.notify?.(message, kind);
  };

  const setPresenceAll = (builder) => {
    const items = Array.from({ length: 10 }, (_, index) => {
      const wire = index + 1;
      const label = `Wire ${String(wire).padStart(2, "0")}`;
      return builder(label, wire);
    });
    cal.setPresence(items);
  };

  const resetFields = () => {
    cal.setFields({
      calibStatusText: "Idle",
      calibModeText: "--",
      calibCountText: "--",
      calibIntervalText: "--",
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

    setPresenceAll((label) => ({ label, value: "--", state: "off" }));
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
        calibIntervalText: "—",
        calibTempText: "--",
        calibWireText: "--",
        calibTargetText: "--",
        calibElapsedText: "0s",
      });
      pushLog("Capacitance measurement started");
      pushLog("Current sensor zero calibration started");
      state.timer = setInterval(() => {
        state.tick += 1;
        cal.setField("calibElapsedText", `${state.tick}s`);
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
        if (state.tick === 3) pushLog("Capacitance measured: 12.4 mF");
        if (state.tick === 4) pushLog("Current sensor zero OK");
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
        calibIntervalText: "500 ms",
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

    if (scenario === "wire") {
      const targetC = Number.parseFloat(inputs.wireTestTargetC);
      const target = Number.isFinite(targetC) ? targetC : 38.5;
      state.activeWire = clamp(Number(inputs.floorCalWireIndex) || 3, 1, 10);

      cal.setFields({
        calibStatusText: "Running",
        calibModeText: "Wire test",
        calibIntervalText: "250 ms",
        calibCountText: "0",
        calibWireText: `Wire ${String(state.activeWire).padStart(2, "0")}`,
        calibTargetText: formatTemp(target),
        calibElapsedText: "0s",
      });
      cal.setFields({
        wireTestState: "Running",
        wireTestMode: "Manual",
        wireTestPurpose: "Calibration",
        wireTestActiveWire: `Wire ${String(state.activeWire).padStart(2, "0")}`,
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
      setPresenceAll((label) => ({ label, value: "…", state: "off" }));
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
