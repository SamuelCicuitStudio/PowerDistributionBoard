import { qs } from "../../core/dom.js";

const THERM_CIRC = 2 * Math.PI * 30;
const EMPTY_VALUE = "--";

function pad2(n) {
  return String(n).padStart(2, "0");
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
  // Empty placeholders
  // -----------------------------
  const thermGrid = qs("[data-dashboard-therm-grid]", root);
  const noticeList = qs("[data-dashboard-notice-list]", root);

  const renderEmptyThermals = () => {
    if (!thermGrid) return;
    thermGrid.innerHTML = "";
    const dash = THERM_CIRC.toFixed(1);
    const items = [
      {
        label: t("dashboard.therm.board", { index: "01" }, "Board 01"),
      },
      {
        label: t("dashboard.therm.board", { index: "02" }, "Board 02"),
      },
      {
        label: t("dashboard.therm.heatsink", null, "Heatsink"),
      },
      {
        label: t("dashboard.therm.temp", { index: "4" }, "Temp 4"),
      },
    ];

    items.forEach((item) => {
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
              stroke-dasharray="${dash}"
              stroke-dashoffset="${dash}"></circle>
          </svg>
          <span class="tDot" style="opacity:0.35"></span>
        </div>
        <div class="tval">${EMPTY_VALUE}</div>
        <div class="label">${item.label}</div>
      `;
      thermGrid.appendChild(box);
    });
  };

  const renderEmptyNotices = () => {
    if (!noticeList) return;
    noticeList.innerHTML = "";
    for (let i = 0; i < 3; i += 1) {
      const item = document.createElement("div");
      item.className = "noticeItem";
      item.dataset.noticeLevel = "info";
      item.innerHTML = `
        <div class="noticeMain">
          <div class="noticeTitle">&nbsp;</div>
          <div class="noticeDesc">&nbsp;</div>
        </div>
        <div class="noticeTag">&nbsp;</div>
      `;
      noticeList.appendChild(item);
    }
  };

  renderEmptyThermals();
  renderEmptyNotices();

  document.addEventListener("language:change", () => {
    renderEmptyThermals();
  });

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

  return {};
}
