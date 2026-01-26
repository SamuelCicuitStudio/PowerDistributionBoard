import { qs } from "../../core/dom.js";
import { renderNotifications } from "../notifications/index.js";

const MAX_VOLT = 325;
const MAX_CURR = 50;
const MAX_CAP_MF = 20;
const GAUGE_CIRC = 239;

function clamp(value, min, max) {
  return Math.min(Math.max(value, min), max);
}

function setGaugeArc(circle, value, min, max) {
  if (!circle) return;
  const t = clamp((value - min) / (max - min), 0, 1);
  circle.setAttribute("stroke-dasharray", GAUGE_CIRC);
  circle.setAttribute("stroke-dashoffset", (GAUGE_CIRC * (1 - t)).toFixed(1));
}

function pad2(n) {
  return String(n).padStart(2, "0");
}

function smallArcDash(value, min, max) {
  const r = 30;
  const circ = 2 * Math.PI * r;
  const t = clamp((value - min) / (max - min), 0, 1);
  const filled = circ * t;
  return { circ, dashoffset: circ - filled };
}

export function initDashboardTab() {
  const panel = qs('[data-tab-panel="dashboard"]');
  if (!panel) return null;

  const root = qs(".dashboard-tab", panel);
  if (!root) return null;

  const t = (key, vars, fallback) => {
    if (window.__i18n?.t) {
      const value = window.__i18n.t(key, vars);
      if (value && value !== key) return value;
    }
    return fallback ?? key;
  };

  const showToast = (message, state = "success") => {
    window.__toast?.show?.(message, state);
  };

  // -----------------------------
  // Clock
  // -----------------------------
  const dowEl = qs("[data-dashboard-dow]", root);
  const dateEl = qs("[data-dashboard-date]", root);
  const timeEl = qs("[data-dashboard-time]", root);
  const hHand = qs('[data-dashboard-hand="h"]', root);
  const mHand = qs('[data-dashboard-hand="m"]', root);
  const sHand = qs('[data-dashboard-hand="s"]', root);
  const localeMap = { en: "en-US", it: "it-IT", fr: "fr-FR" };

  const getClockLocale = () => {
    const lang = window.__i18n?.getLang?.() || "en";
    return localeMap[lang] || "en-US";
  };

  function tickClock() {
    const d = new Date();
    const locale = getClockLocale();
    if (dowEl) {
      dowEl.textContent = d.toLocaleDateString(locale, { weekday: "long" });
    }
    if (dateEl) {
      dateEl.textContent = d.toLocaleDateString(locale, {
        year: "numeric",
        month: "short",
        day: "2-digit",
      });
    }
    if (timeEl) {
      timeEl.textContent = `${pad2(d.getHours())}:${pad2(d.getMinutes())}:${pad2(d.getSeconds())}`;
    }

    const h = (d.getHours() % 12) + d.getMinutes() / 60;
    const m = d.getMinutes() + d.getSeconds() / 60;
    const s = d.getSeconds();
    hHand?.setAttribute("transform", `rotate(${h * 30} 60 60)`);
    mHand?.setAttribute("transform", `rotate(${m * 6} 60 60)`);
    sHand?.setAttribute("transform", `rotate(${s * 6} 60 60)`);
  }

  tickClock();
  setInterval(tickClock, 1000);

  // -----------------------------
  // System state (mock)
  // -----------------------------
  const systemState = {
    ready: true,
    mode: "Off",
    calibrationPending: true,
    ledFeedback: true,
  };

  const ledReady = qs('[data-dashboard-led="ready"]', root);
  const ledMode = qs('[data-dashboard-led="mode"]', root);
  const ledCal = qs('[data-dashboard-led="cal"]', root);

  const readyTxt = qs('[data-dashboard-text="ready"]', root);
  const modeTxt = qs('[data-dashboard-text="mode"]', root);
  const calTxt = qs('[data-dashboard-text="cal"]', root);

  const ledFeedback = qs("[data-dashboard-led-feedback]", root);

  function renderSystem() {
    if (readyTxt) {
      readyTxt.textContent = systemState.ready
        ? t("dashboard.system.ready", null, "Ready")
        : t("dashboard.system.notReady", null, "Not Ready");
    }
    if (modeTxt) {
      const modeValue = String(systemState.mode || "");
      const lower = modeValue.toLowerCase();
      if (lower === "running") {
        modeTxt.textContent = t("statusbar.mode.running", null, "Running");
      } else if (lower === "off") {
        modeTxt.textContent = t("dashboard.system.off", null, "Off");
      } else {
        modeTxt.textContent = modeValue;
      }
    }
    if (calTxt) {
      calTxt.textContent = systemState.calibrationPending
        ? t("dashboard.system.calPending", null, "Cal Pending")
        : t("dashboard.system.calOk", null, "Cal OK");
    }

    if (ledReady) ledReady.className = `led ${systemState.ready ? "ok" : "err"}`;
    if (ledMode) {
      const cls = systemState.mode === "Running" ? "ok" : systemState.mode === "Off" ? "err" : "err";
      ledMode.className = `led ${cls}`;
    }
    if (ledCal) ledCal.className = `led ${systemState.calibrationPending ? "warn" : "ok"}`;

    if (ledFeedback) ledFeedback.checked = !!systemState.ledFeedback;
  }

  renderSystem();

  ledFeedback?.addEventListener("change", (e) => {
    systemState.ledFeedback = e.target.checked;
    showToast(
      systemState.ledFeedback
        ? t("dashboard.toast.ledOn", null, "LED feedback enabled.")
        : t("dashboard.toast.ledOff", null, "LED feedback disabled."),
    );
    renderSystem();
  });

  // -----------------------------
  // System notifications (module + mock data)
  // -----------------------------
  const noticeList = qs("[data-dashboard-notice-list]", root);
  const templateEl = qs("[data-notification-item-template]", root);

  const buildNotices = () => [
    {
      level: "warn",
      title: t("dashboard.notice.calPending.title", null, "Calibration pending"),
      message: t(
        "dashboard.notice.calPending.message",
        null,
        "Device needs calibration before running the loop.",
      ),
    },
    {
      level: "ok",
      title: t("dashboard.notice.wifi.title", null, "Wi-Fi connected"),
      message: t(
        "dashboard.notice.wifi.message",
        null,
        "Station mode active. Signal stable.",
      ),
    },
    {
      level: "err",
      title: t("dashboard.notice.output.title", null, "Output 4 not detected"),
      message: t(
        "dashboard.notice.output.message",
        null,
        "No wire / open circuit detected on output 4.",
      ),
    },
  ];

  const renderNoticeList = () => {
    renderNotifications(noticeList, buildNotices(), templateEl);
  };

  renderNoticeList();

  // -----------------------------
  // Quick actions
  // -----------------------------
  const setupBtn = qs("[data-dashboard-setup]", root);
  const calBtn = qs("[data-dashboard-calibrate]", root);
  const logsBtn = qs("[data-dashboard-logs]", root);

  setupBtn?.addEventListener("click", () => {
    window.__wizard?.open?.();
  });
  calBtn?.addEventListener("click", () => {
    window.__calibration?.open?.();
  });
  logsBtn?.addEventListener("click", () => {
    window.__log?.open?.();
  });

  // -----------------------------
  // Electrical (mock)
  // -----------------------------
  const vArc = qs('[data-dashboard-arc="v"]', root);
  const iArc = qs('[data-dashboard-arc="i"]', root);
  const cArc = qs('[data-dashboard-arc="c"]', root);
  const vVal = qs('[data-dashboard-val="v"]', root);
  const iVal = qs('[data-dashboard-val="i"]', root);
  const cVal = qs('[data-dashboard-val="c"]', root);

  function mockElectrical() {
    const v = clamp(280 + Math.random() * 45, 0, MAX_VOLT);
    const i = clamp(Math.random() * 12, 0, MAX_CURR);
    const c = clamp(5 + Math.random() * 8, 0, MAX_CAP_MF);

    if (vVal) vVal.textContent = v.toFixed(1);
    if (iVal) iVal.textContent = i.toFixed(2);
    if (cVal) cVal.textContent = c.toFixed(2);

    setGaugeArc(vArc, v, 0, MAX_VOLT);
    setGaugeArc(iArc, i, 0, MAX_CURR);
    setGaugeArc(cArc, c, 0, MAX_CAP_MF);
  }

  mockElectrical();
  setInterval(mockElectrical, 1200);

  // -----------------------------
  // Thermals (mock)
  // -----------------------------
  const thermGrid = qs("[data-dashboard-therm-grid]", root);
  const therms = [
    { type: "board", index: "01", value: 21.69 },
    { type: "board", index: "02", value: 21.0 },
    { type: "heatsink", value: 20.94 },
    { type: "temp", index: "4", value: null },
  ];

  function renderThermals() {
    if (!thermGrid) return;
    thermGrid.innerHTML = "";
    therms.forEach((item) => {
      const off = item.value === null;
      const valTxt = off
        ? t("dashboard.therm.off", null, "Off")
        : `${item.value.toFixed(2)}\u00b0C`;
      const arc = off
        ? { circ: 2 * Math.PI * 30, dashoffset: 2 * Math.PI * 30 }
        : smallArcDash(item.value, 0, 150);
      let label = "";
      if (item.type === "board") {
        label = t(
          "dashboard.therm.board",
          { index: item.index },
          `Board ${item.index}`,
        );
      } else if (item.type === "heatsink") {
        label = t("dashboard.therm.heatsink", null, "Heatsink");
      } else if (item.type === "temp") {
        label = t(
          "dashboard.therm.temp",
          { index: item.index },
          `Temp ${item.index}`,
        );
      }

      const box = document.createElement("div");
      box.className = "therm";
      box.innerHTML = `
        <div class="tRing" aria-hidden="true">
          <svg viewBox="0 0 100 100">
            <circle class="base rot" cx="50" cy="50" r="30" stroke-width="8" fill="none"></circle>
            <circle class="arc rot" cx="50" cy="50" r="30"
              stroke-width="8"
              fill="none"
              stroke-linecap="round"
              stroke-dasharray="${arc.circ.toFixed(1)}"
              stroke-dashoffset="${arc.dashoffset.toFixed(1)}"></circle>
          </svg>
          <span class="tDot" style="opacity:${off ? 0.35 : 0.9}"></span>
        </div>
        <div class="tval">${valTxt}</div>
        <div class="label">${label}</div>
      `;
      thermGrid.appendChild(box);
    });
  }

  renderThermals();
  setInterval(() => {
    therms.forEach((item) => {
      if (item.value === null) return;
      item.value = clamp(item.value + (Math.random() * 1.2 - 0.5), 0, 150);
    });
    renderThermals();
  }, 1400);

  document.addEventListener("language:change", () => {
    renderSystem();
    renderNoticeList();
    renderThermals();
  });

  return { renderSystem };
}

