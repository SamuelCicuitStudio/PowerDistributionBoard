import { qs, qsa } from "../../core/dom.js";

const ACTION_MESSAGES = {
  startModelCalibBtn: "Temp model calibration queued",
  stopCalibBtn: "Calibration stop requested",
  calibHistoryBtn: "Loading calibration history",
  calibClearBtn: "Calibration data cleared",
  calibHistoryLoadBtn: "Saved history loaded",
  calibHistoryRefreshBtn: "History list refreshed",
  wireTestStartBtn: "Wire test started",
  wireTestStopBtn: "Wire test stopped",
  startFloorCalibBtn: "Floor calibration started",
  presenceProbeBtn: "Presence probe started",
  logRefreshBtn: "Calibration log refreshed",
  logClearBtn: "Calibration log cleared",
  capCurrentCalBtn: "Capacitance + current sensor zero queued",
};

const ACTION_STATES = {
  stopCalibBtn: "warn",
  calibClearBtn: "warn",
  wireTestStopBtn: "warn",
  logClearBtn: "warn",
};

export function initCalibrationOverlay() {
  const overlay = qs("[data-calibration-overlay]");
  if (!overlay) return null;

  const contentRoot = overlay.closest(".content");
  const openButtons = qsa("[data-calibration-open]");
  const closeBtn = qs("[data-calibration-close]", overlay);
  const infoPanel = qs("[data-cal-info]", overlay);
  const infoOpen = qs("[data-cal-info-open]", overlay);
  const infoClose = qs("[data-cal-info-close]", overlay);
  const actionButtons = qsa("[data-cal-action]", overlay);
  const fieldNodes = qsa("[data-cal-field]", overlay);
  const inputNodes = qsa("[data-cal-input]", overlay);
  const historySelect = qs("[data-cal-input='calibHistorySelect']", overlay);
  const presenceGrid = qs("[data-cal-grid='presenceProbeGrid']", overlay);
  const logBody = qs("[data-cal-log]", overlay);
  const alertBar = qs("[data-cal-alert]", overlay);
  const alertText = qs("[data-cal-alert-text]", overlay);

  const fields = new Map();
  fieldNodes.forEach((node) => {
    const key = node.getAttribute("data-cal-field");
    if (!key) return;
    const list = fields.get(key) || [];
    list.push(node);
    fields.set(key, list);
  });

  let calAlertTimer = null;

  const setOpen = (open) => {
    overlay.classList.toggle("is-open", open);
    overlay.setAttribute("aria-hidden", String(!open));
    if (contentRoot) {
      contentRoot.classList.toggle("is-calibration", open);
    }
    if (!open && infoPanel) {
      infoPanel.classList.remove("is-open");
      infoPanel.setAttribute("aria-hidden", "true");
    }
    if (!open && alertBar) {
      alertBar.classList.remove("is-visible");
      alertBar.setAttribute("aria-hidden", "true");
    }
  };

  const close = () => setOpen(false);

  openButtons.forEach((btn) => {
    btn.addEventListener("click", () => setOpen(true));
  });

  if (closeBtn) {
    closeBtn.addEventListener("click", close);
  }

  overlay.addEventListener("click", (event) => {
    if (event.target === overlay) close();
  });

  document.addEventListener("keydown", (event) => {
    if (event.key === "Escape") close();
  });

  document.addEventListener("click", (event) => {
    const tabButton = event.target.closest(".tab");
    if (tabButton) close();
  });

  if (infoOpen && infoPanel) {
    infoOpen.addEventListener("click", () => {
      infoPanel.classList.add("is-open");
      infoPanel.setAttribute("aria-hidden", "false");
    });
  }

  if (infoClose && infoPanel) {
    infoClose.addEventListener("click", () => {
      infoPanel.classList.remove("is-open");
      infoPanel.setAttribute("aria-hidden", "true");
    });
  }

  if (infoPanel) {
    infoPanel.addEventListener("click", (event) => {
      if (event.target === infoPanel) {
        infoPanel.classList.remove("is-open");
        infoPanel.setAttribute("aria-hidden", "true");
      }
    });
  }

  const getInputs = () => {
    const inputs = {};
    inputNodes.forEach((node) => {
      const key = node.getAttribute("data-cal-input");
      if (!key) return;
      inputs[key] = node.value;
    });
    return inputs;
  };

  const setField = (key, value) => {
    const nodes = fields.get(key);
    if (!nodes?.length) return;
    const text = value === null || value === undefined || value === "" ? "--" : String(value);
    nodes.forEach((node) => {
      node.textContent = text;
    });
  };

  const setFields = (values = {}) => {
    Object.entries(values).forEach(([key, value]) => setField(key, value));
  };

  const setHistoryOptions = (options = []) => {
    if (!historySelect) return;
    historySelect.innerHTML = "";
    const normalized = options.length ? options : ["Session 2026-01-21", "Session 2026-01-20"];
    normalized.forEach((item) => {
      const option = document.createElement("option");
      if (typeof item === "string") {
        option.value = item;
        option.textContent = item;
      } else {
        option.value = item.value ?? item.label ?? "";
        option.textContent = item.label ?? item.value ?? "";
      }
      historySelect.appendChild(option);
    });
  };

  const setPresence = (items = []) => {
    if (!presenceGrid) return;
    const rows = qsa(".presence-item", presenceGrid);
    const byLabel = new Map();
    rows.forEach((row) => {
      const spans = qsa("span", row);
      const label = spans[0]?.textContent?.trim() || "";
      byLabel.set(label, row);
    });
    items.forEach((item) => {
      const row = byLabel.get(item.label);
      if (!row) return;
      const spans = qsa("span", row);
      if (spans[1]) spans[1].textContent = item.value ?? "--";
      if (item.state) row.dataset.state = item.state;
    });
  };

  const setLogLines = (lines = []) => {
    if (!logBody) return;
    logBody.innerHTML = "";
    if (!lines.length) {
      const empty = document.createElement("div");
      empty.className = "calibration-log-entry";
      empty.textContent = "[--:--:--] No log data";
      logBody.appendChild(empty);
      return;
    }
    lines.forEach((line) => {
      const entry = document.createElement("div");
      entry.className = "calibration-log-entry";
      entry.textContent = line;
      logBody.appendChild(entry);
    });
  };

  const normalizeAlertState = (state) => {
    const key = String(state || "").trim().toLowerCase();
    if (!key) return "ok";
    if (key === "warning") return "warn";
    if (key === "error") return "err";
    if (key === "success") return "ok";
    if (key === "info") return "ok";
    if (key === "ok" || key === "warn" || key === "err") return key;
    return "ok";
  };

  const showCalAlert = (message, state = "ok") => {
    if (!alertBar || !alertText) return;
    alertText.textContent = String(message || "");
    alertBar.dataset.state = normalizeAlertState(state);
    alertBar.classList.add("is-visible");
    alertBar.setAttribute("aria-hidden", "false");
    if (calAlertTimer) {
      clearTimeout(calAlertTimer);
    }
    calAlertTimer = setTimeout(() => {
      alertBar.classList.remove("is-visible");
      alertBar.setAttribute("aria-hidden", "true");
    }, 2200);
  };

  const notify = (message, state = "ok") => {
    if (overlay.classList.contains("is-open")) {
      showCalAlert(message, state);
      return;
    }
    if (window.__statusbar?.notify) {
      window.__statusbar.notify(message, state);
      return;
    }
    window.__toast?.show?.(String(message || ""), state === "err" ? "error" : "success");
  };

  actionButtons.forEach((btn) => {
    btn.addEventListener("click", () => {
      const key = btn.getAttribute("data-cal-action");
      const message = ACTION_MESSAGES[key] || "Calibration action queued";
      notify(message, ACTION_STATES[key] || "ok");
      window.dispatchEvent(
        new CustomEvent("calibration:action", {
          detail: { action: key, inputs: getInputs() },
        }),
      );
    });
  });

  return {
    open: () => setOpen(true),
    close,
    isOpen: () => overlay.classList.contains("is-open"),
    getInputs,
    setField,
    setFields,
    setHistoryOptions,
    setPresence,
    setLogLines,
    notify,
  };
}
