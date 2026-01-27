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
import { initSetupWizard } from "./features/setup/index.js";
import { initLanguage } from "./services/language.js";
import { requireSession } from "./services/session.js";
import { initSetupWizardLinkage } from "./services/setup_wizard_linkage.js";

if (window.chrome?.webview) {
  document.documentElement.classList.add("is-webview");
}

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
  const panelByKey = new Map(
    panels.map((panel) => [panel.dataset.tabPanel, panel]),
  );
  let activeIndex = tabs.findIndex((tab) =>
    tab.classList.contains("is-active"),
  );
  if (activeIndex < 0) activeIndex = 0;
  let isAnimating = false;
  const ANIM_MS = 420;

  tabs.forEach((tab) =>
    tab.addEventListener("click", () => {
      if (tab.classList.contains("is-active") || isAnimating) return;
      const nextIndex = tabs.indexOf(tab);
      const currentTab = tabs[activeIndex];
      const currentPanel = panelByKey.get(currentTab?.dataset.tab || "");
      const nextPanel = panelByKey.get(tab.dataset.tab || "");
      if (!currentPanel || !nextPanel) return;

      const dir = nextIndex > activeIndex ? "up" : "down";
      isAnimating = true;

      tabs.forEach((t) => t.classList.remove("is-active"));
      tab.classList.add("is-active");

      currentPanel.classList.add("is-leaving");
      currentPanel.dataset.dir = dir;

      nextPanel.classList.add("is-active", "is-entering");
      nextPanel.dataset.dir = dir;

      requestAnimationFrame(() => {
        requestAnimationFrame(() => {
          currentPanel.classList.add("is-animating");
          nextPanel.classList.add("is-animating");
        });
      });

      window.setTimeout(() => {
        currentPanel.classList.remove("is-active", "is-leaving", "is-animating");
        currentPanel.removeAttribute("data-dir");
        nextPanel.classList.remove("is-entering", "is-animating");
        nextPanel.removeAttribute("data-dir");
        activeIndex = nextIndex;
        isAnimating = false;
      }, ANIM_MS);
    }),
  );
}

if (requireSession()) {
  document.addEventListener("DOMContentLoaded", () => {
    window.__uiReady = loadIncludes().then(() => {
      initTabs();
      const language = initLanguage();
      window.__i18n = language;
      window.__language = language;
      const wizardLinkage = initSetupWizardLinkage();
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
      window.__wizard = initSetupWizard({
        onStepChange: wizardLinkage.onStepChange,
        onOpen: wizardLinkage.onOpen,
        onClose: wizardLinkage.onClose,
      });
      wizardLinkage.attach(window.__wizard);
      initSidebarActions();
      window.dispatchEvent(new Event("ui:ready"));
    });
  });
}
