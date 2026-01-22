import { qsa } from "./core/dom.js";
import { initStatusbar } from "./features/statusbar/index.js";
import { initLiveOverlay } from "./features/live/index.js";
import { initAlerts } from "./features/alerts/index.js";
import { initHistoryOverlay } from "./features/history/index.js";
import { initLogOverlay } from "./features/log/index.js";
import { initCalibrationOverlay } from "./features/calibration/index.js";
import { initToast } from "./features/toast/index.js";
import { initUserTab } from "./features/user/index.js";
import { initConfirm } from "./features/confirm/index.js";
import { initSidebarActions } from "./features/sidebar/index.js";
import { initControlTab } from "./features/control/index.js";
import { initAdminTab } from "./features/admin/index.js";
import { initDashboardTab } from "./features/dashboard/index.js";
import { initDeviceTab } from "./features/device/index.js";

async function loadIncludes() {
  let includeNodes = qsa("[data-include]");
  while (includeNodes.length) {
    await Promise.all(
      includeNodes.map(async (node) => {
        const path = node.getAttribute("data-include");
        if (!path) {
          node.removeAttribute("data-include");
          return;
        }
        try {
          const response = await fetch(path, { cache: "no-store" });
          if (!response.ok) return;
          const html = await response.text();
          node.innerHTML = html;
        } catch (error) {
          console.warn("Include failed:", path, error);
        } finally {
          node.removeAttribute("data-include");
        }
      }),
    );
    includeNodes = qsa("[data-include]");
  }
}

function initTabs() {
  const tabs = qsa(".tab");
  if (!tabs.length) return;
  const panels = qsa("[data-tab-panel]");

  tabs.forEach((tab) =>
    tab.addEventListener("click", () => {
      tabs.forEach((t) => t.classList.remove("is-active"));
      tab.classList.add("is-active");
      if (!panels.length) return;
      const key = tab.dataset.tab;
      panels.forEach((panel) => {
        panel.classList.toggle("is-active", panel.dataset.tabPanel === key);
      });
    }),
  );
}

document.addEventListener("DOMContentLoaded", () => {
  window.__uiReady = loadIncludes().then(() => {
    initTabs();
    window.__toast = initToast();
    window.__confirm = initConfirm();
    window.__statusbar = initStatusbar();
    window.__alerts = initAlerts();
    window.__history = initHistoryOverlay();
    window.__log = initLogOverlay();
    window.__calibration = initCalibrationOverlay();
    initLiveOverlay();
    initDashboardTab();
    initUserTab();
    initControlTab();
    initAdminTab();
    initDeviceTab();
    initSidebarActions();
    window.dispatchEvent(new Event("ui:ready"));
  });
});
