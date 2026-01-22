import { initStatusbar } from "../features/statusbar/index.js";
import { initAlerts } from "../features/alerts/index.js";

function startMock() {
  const statusbar = window.__statusbar || initStatusbar();
  const alerts = window.__alerts || initAlerts();
  if (!statusbar) return;

  const liveContexts = [
    {
      mode: "Wire calibration",
      wires: "3",
      target: "Wire 03",
      setpoint: 65,
    },
    {
      mode: "Floor calibration",
      wires: "1,2,3,4,5,6,7,8,9,10",
      target: "Floor",
      setpoint: 38,
    },
    {
      mode: "Running",
      wires: "1,2,3,4,5,6,7,8,9,10",
      target: "Profile",
      setpoint: 55,
    },
  ];
  const modes = liveContexts.map((context) => context.mode);
  const roles = ["Admin", "User"];
  const linkModes = ["Station", "AP Hotspot"];
  const warningMessages = [
    "Board temperature high",
    "Heatsink temperature high",
    "Voltage drift detected",
    "Calibration recommended",
    "Sensor noise detected",
  ];
  const errorMessages = [
    "Overcurrent detected",
    "Wire open circuit",
    "Control fault",
    "EEPROM write failed",
    "Power stage fault",
  ];
  const warningLog = [];
  const errorLog = [];
  let tick = 0;
  const liveShell = document.querySelector("[data-live-overlay] [data-live-wires]");
  const liveMode = document.querySelector("[data-live='mode']");
  const liveTarget = document.querySelector("[data-live='target']");
  const liveSetpoint = document.querySelector("[data-live='setpoint']");
  const liveSetpointValue = document.querySelector("[data-live-setpoint]");
  const setpointLine = document.querySelector(
    "[data-live-overlay] .chart-line.setpoint",
  );
  const liveWireLines = Array.from(
    document.querySelectorAll("[data-live-overlay] .wire-line[data-wire]"),
  );
  const liveWireDots = Array.from(
    document.querySelectorAll("[data-live-overlay] .wire-dot[data-wire]"),
  );
  const liveWireTemps = Array.from(
    document.querySelectorAll("[data-live-wire-temp]"),
  );
  const liveControlBtn = document.querySelector("[data-live-control]");
  const liveErrorBtn = document.querySelector("[data-live-error]");
  const historyOverlay = window.__history || null;
  const logOverlay = window.__log || null;
  const muteBtn = document.querySelector(".round-button.mute");
  const powerBtn = document.querySelector(".power-button");
  const powerLabel = powerBtn?.querySelector(".label");
  const livePortDots = Array.from(
    document.querySelectorAll(".live-port-row .live-state-dot"),
  );
  const livePortRows = Array.from(
    document.querySelectorAll(".live-port-row"),
  );
  const liveRelayDot = document.querySelector(".live-bottom-dot.live-relay");
  const liveWireTargets = Array.from(
    document.querySelectorAll(
      "[data-live-overlay] .wire-line[data-wire], [data-live-overlay] .wire-dot[data-wire], [data-live-overlay] .chart-pill.wire[data-wire]",
    ),
  );
  const X_POINTS = [0, 120, 240, 360, 480, 600, 720, 840, 960, 1080, 1200];
  const CHART_TOP = 10;
  const CHART_BOTTOM = 265;
  const CHART_HEIGHT = CHART_BOTTOM - CHART_TOP;

  function rand(min, max, decimals = 0) {
    const value = Math.random() * (max - min) + min;
    return Number(value.toFixed(decimals));
  }

  function clamp(value, min, max) {
    return Math.min(Math.max(value, min), max);
  }

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

  function nowLabel() {
    return new Date().toLocaleTimeString("en-US", {
      hour12: false,
      hour: "2-digit",
      minute: "2-digit",
      second: "2-digit",
    });
  }

  function pushLog(list, message) {
    list.unshift({ message, time: nowLabel() });
    if (list.length > 11) {
      list.length = 11;
    }
  }

  function seedLogs() {
    pushLog(warningLog, "Sensor noise detected");
    pushLog(warningLog, "Heatsink temperature high");
    pushLog(errorLog, "Control fault");
  }

  function buildHistoryRows() {
    const baseTime = Date.now();
    const rows = [];
    for (let i = 0; i < 8; i += 1) {
      const time = new Date(baseTime - i * 3600 * 1000).toLocaleTimeString(
        "en-US",
        {
          hour12: false,
          hour: "2-digit",
          minute: "2-digit",
        },
      );
      rows.push({
        time,
        duration: `${30 + i * 5}s`,
        energy: `${(12.4 + i * 2.1).toFixed(1)}`,
        peakP: `${220 + i * 18}`,
        peakI: `${(3.2 + i * 0.4).toFixed(1)}`,
      });
    }
    return rows;
  }

  function buildLogLines() {
    const lines = [];
    for (let i = 0; i < 12; i += 1) {
      const stamp = new Date(Date.now() - i * 15000).toLocaleTimeString(
        "en-US",
        { hour12: false, hour: "2-digit", minute: "2-digit", second: "2-digit" },
      );
      lines.push(`[${stamp}] Output ${i % 10 + 1} -> ${rand(18, 60, 0)}C`);
    }
    return lines;
  }

  function showToast(message, state = "success") {
    window.__toast?.show?.(message, state);
  }

  function setOverlay(open) {
    const overlay = document.querySelector("[data-live-overlay]");
    if (!overlay) return;
    const contentRoot = overlay.closest(".content");
    overlay.classList.toggle("is-open", open);
    overlay.setAttribute("aria-hidden", String(!open));
    if (contentRoot) {
      contentRoot.classList.toggle("is-live", open);
    }
  }

  if (liveControlBtn) {
    liveControlBtn.addEventListener("click", () => setOverlay(true));
  }

  if (liveErrorBtn) {
    liveErrorBtn.addEventListener("click", () => {
      window.__alerts?.openErrors?.();
    });
  }

  function update() {
    const context = liveContexts[tick % liveContexts.length];
    statusbar.setConnection("ok", linkModes[tick % linkModes.length]);
    statusbar.setBoardTemp(rand(24, 150));
    statusbar.setSinkTemp(rand(24, 150));
    statusbar.setVoltage(rand(220, 325, 1));
    statusbar.setCurrent(rand(0, 50, 1));

    const warnHit = Math.random() > 0.7;
    const errHit = Math.random() > 0.9;
    if (warnHit) {
      const msg = warningMessages[tick % warningMessages.length];
      pushLog(warningLog, msg);
    }
    if (errHit) {
      const msg = errorMessages[tick % errorMessages.length];
      pushLog(errorLog, msg);
    }
    statusbar.setWarnings(warningLog.length);
    statusbar.setErrors(errorLog.length);
    if (alerts) {
      alerts.setWarnings(warningLog);
      alerts.setErrors(errorLog);
    }

    statusbar.setUserRole(roles[tick % roles.length]);
    statusbar.setMode(modes[tick % modes.length]);
    if (liveShell) {
      liveShell.dataset.liveWires = context.wires;
    }
    const selected = context.wires
      .split(",")
      .map((value) => value.trim())
      .filter(Boolean);
    if (liveWireTargets.length) {
      liveWireTargets.forEach((target) => {
        const id = target.dataset.wire;
        target.classList.toggle("is-selected", selected.includes(id));
      });
    }
    if (liveMode) {
      liveMode.textContent = `Mode: ${context.mode}`;
    }
    if (liveTarget) {
      liveTarget.textContent = `Target: ${context.target}`;
    }
    if (liveSetpoint) {
      liveSetpoint.textContent = `Setpoint: ${context.setpoint}\u00b0C`;
    }
    if (liveSetpointValue) {
      liveSetpointValue.textContent = `${context.setpoint}\u00b0C`;
    }
    if (setpointLine) {
      const setpointY = yForTemp(context.setpoint);
      setpointLine.setAttribute(
        "points",
        X_POINTS.map((x) => `${x},${setpointY}`).join(" "),
      );
    }
    const tempsByWire = {};
    selected.forEach((id, index) => {
      const spread = (index - (selected.length - 1) / 2) * 1.4;
      const base = context.setpoint + spread;
      const temp = clamp(base + rand(-3, 3, 1), 0, 150);
      tempsByWire[id] = temp;
    });
    liveWireTemps.forEach((node) => {
      const id = node.dataset.liveWireTemp;
      if (!id || !selected.includes(id)) {
        node.textContent = "--\u00b0C";
        return;
      }
      node.textContent = `${tempsByWire[id].toFixed(1)}\u00b0C`;
    });
    liveWireLines.forEach((line) => {
      const id = line.dataset.wire;
      const temp = tempsByWire[id];
      if (!Number.isFinite(temp)) return;
      line.setAttribute("points", buildLinePoints(temp));
    });
    liveWireDots.forEach((dot) => {
      const id = dot.dataset.wire;
      const temp = tempsByWire[id];
      if (!Number.isFinite(temp)) return;
      dot.setAttribute("cx", X_POINTS[X_POINTS.length - 1]);
      dot.setAttribute("cy", yForTemp(temp));
    });

    if (livePortDots.length) {
      const cycle = ["off", "on", "nowire"];
      livePortDots.forEach((dot, index) => {
        if (index % 2 === tick % 2) {
          const current = dot.dataset.state || "off";
          const next = cycle[(cycle.indexOf(current) + 1) % cycle.length];
          dot.dataset.state = next;
        }
      });
    }

    if (livePortRows.length) {
      livePortRows.forEach((row) => {
        const dot = row.querySelector(".live-state-dot");
        const port = row.querySelector(".live-port");
        if (!port) return;
        const state = dot?.dataset.state || "off";
        if (state === "nowire") {
          port.textContent = "--";
          return;
        }
        port.textContent = `${rand(18, 60, 0)}`;
      });
    }

    if (liveRelayDot) {
      liveRelayDot.classList.toggle("is-on", tick % 2 === 0);
    }
    tick += 1;
  }

  seedLogs();
  if (historyOverlay?.setRows) {
    historyOverlay.setRows(buildHistoryRows());
  }
  if (logOverlay?.setLogs) {
    logOverlay.setLogs(buildLogLines());
    logOverlay.refresh = () => logOverlay.setLogs(buildLogLines());
  }

  if (muteBtn) {
    muteBtn.addEventListener("click", () => {
      const isMuted = muteBtn.classList.toggle("is-muted");
      muteBtn.setAttribute("aria-pressed", String(isMuted));
      muteBtn.setAttribute("aria-label", isMuted ? "Unmute" : "Mute");
      showToast(isMuted ? "Muted" : "Unmuted");
    });
  }

  if (powerBtn) {
    const states = ["off", "idle", "ready"];
    const labels = { off: "OFF", idle: "IDLE", ready: "RUN" };
    let index = states.findIndex((state) =>
      powerBtn.classList.contains(`state-${state}`),
    );
    if (index < 0) index = 0;
    powerBtn.addEventListener("click", () => {
      powerBtn.classList.remove(
        `state-${states[index]}`,
        `state-${states[(index + 1) % states.length]}`,
        `state-${states[(index + 2) % states.length]}`,
      );
      index = (index + 1) % states.length;
      powerBtn.classList.add(`state-${states[index]}`);
      if (powerLabel) powerLabel.textContent = labels[states[index]];
      showToast(`Power ${labels[states[index]]}`);
    });
  }

  update();
  setInterval(update, 4000);
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
