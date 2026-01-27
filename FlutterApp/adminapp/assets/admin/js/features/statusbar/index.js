import { qs } from "../../core/dom.js";

const MODE_MAP = {
  running: "running",
  "wire calibration": "wire-calibration",
  "floor calibration": "floor-calibration",
};

function normalizeMode(mode) {
  const key = String(mode || "").trim().toLowerCase();
  if (!key) return "";
  if (MODE_MAP[key]) return MODE_MAP[key];
  return key.replace(/[^a-z0-9]+/g, "-").replace(/^-|-$/g, "");
}

function setText(node, value) {
  if (!node) return;
  node.textContent = value;
}

function setState(node, state) {
  if (!node) return;
  node.dataset.state = state;
}

export function initStatusbar() {
  const root = qs("[data-statusbar]");
  if (!root) return null;

  let lastWarn = 0;
  let lastErr = 0;
  let alertTimer = null;

  const contentRoot = root.closest(".content");
  const updateStatusbarHeightVar = () => {
    if (!contentRoot) return;
    contentRoot.style.setProperty("--statusbar-h", `${root.offsetHeight}px`);
  };

  const nodes = {
    indicator: qs("[data-sb='indicator']", root),
    linkPill: qs("[data-sb='link-pill']", root),
    boardTemp: qs("[data-sb='board-temp']", root),
    sinkTemp: qs("[data-sb='sink-temp']", root),
    dcVoltage: qs("[data-sb='dc-voltage']", root),
    dcCurrent: qs("[data-sb='dc-current']", root),
    warnPill: qs("[data-sb='warn-pill']", root),
    warnCount: qs("[data-sb='warn-count']", root),
    errPill: qs("[data-sb='err-pill']", root),
    errCount: qs("[data-sb='err-count']", root),
    userRole: qs("[data-sb='user-role']", root),
    modePill: qs("[data-sb='mode-pill']", root),
    modeLabel: qs("[data-sb='mode-label']", root),
    liveBtn: qs("[data-sb='live-btn']", root),
    alert: qs("[data-sb='alert']", contentRoot),
    alertText: qs("[data-sb='alert-text']", contentRoot),
    sessionToggle: qs("[data-sb='session-toggle']", root),
    sessionMenu: qs("[data-sb='session-menu']", contentRoot),
    credBtn: qs("[data-sb='cred-btn']", contentRoot),
    logoutBtn: qs("[data-sb='logout-btn']", contentRoot),
  };

  const showAlert = (message, state) => {
    if (!nodes.alert || !nodes.alertText) return;
    nodes.alertText.textContent = message;
    nodes.alert.dataset.state = state;
    nodes.alert.classList.add("is-visible");
    nodes.alert.setAttribute("aria-hidden", "false");
    if (alertTimer) {
      clearTimeout(alertTimer);
    }
    alertTimer = setTimeout(() => {
      nodes.alert.classList.remove("is-visible");
      nodes.alert.setAttribute("aria-hidden", "true");
    }, 2200);
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

  const notify = (message, state = "ok") => {
    showAlert(String(message || ""), normalizeAlertState(state));
  };

  const t = (key, vars, fallback) => {
    if (window.__i18n?.t) {
      const value = window.__i18n.t(key, vars);
      if (value && value !== key) return value;
    }
    return fallback ?? key;
  };

  updateStatusbarHeightVar();
  window.addEventListener("resize", updateStatusbarHeightVar);

  if (nodes.sessionToggle && nodes.sessionMenu) {
    const setOpen = (open) => {
      nodes.sessionToggle.setAttribute("aria-expanded", String(open));
      nodes.sessionMenu.classList.toggle("is-open", open);
      nodes.sessionMenu.setAttribute("aria-hidden", String(!open));

      if (!open) return;
      if (!contentRoot) return;

      const toggleRect = nodes.sessionToggle.getBoundingClientRect();
      const contentRect = contentRoot.getBoundingClientRect();
      const top = toggleRect.bottom - contentRect.top + 8;
      const right = contentRect.right - toggleRect.right;
      nodes.sessionMenu.style.top = `${Math.max(0, top)}px`;
      nodes.sessionMenu.style.right = `${Math.max(0, right)}px`;
    };

    nodes.sessionToggle.addEventListener("click", (event) => {
      event.stopPropagation();
      const isOpen = nodes.sessionMenu.classList.contains("is-open");
      setOpen(!isOpen);
    });

    document.addEventListener("click", (event) => {
      if (
        nodes.sessionMenu.contains(event.target) ||
        nodes.sessionToggle.contains(event.target)
      ) {
        return;
      }
      setOpen(false);
    });

    nodes.sessionToggle.addEventListener("keydown", (event) => {
      if (event.key === "Escape") {
        setOpen(false);
      }
    });

    nodes.sessionMenu.addEventListener("click", () => {
      setOpen(false);
    });
  }

  if (nodes.credBtn) {
    nodes.credBtn.addEventListener("click", () => {
      const adminTab = qs('.tab[data-tab="admin"]');
      if (adminTab) {
        adminTab.click();
      }
    });
  }

  if (nodes.logoutBtn) {
    nodes.logoutBtn.addEventListener("click", async () => {
      if (window.pbDisconnect) {
        try {
          await window.pbDisconnect();
        } catch (error) {
          console.warn("Disconnect failed:", error);
        }
      }
      if (window.pbClearToken) window.pbClearToken();
      const base = new URLSearchParams(window.location.search).get("base");
      const next = "login.html" + (base ? "?base=" + encodeURIComponent(base) : "");
      window.location.href = next;
    });
  }

  function setConnection(state, label) {
    setState(nodes.indicator, state);
    setState(nodes.linkPill, state);
    if (nodes.linkPill && label) {
      const mode = String(label || "").trim().toLowerCase();
      nodes.linkPill.dataset.link = mode.includes("ap") ? "ap" : "station";
      nodes.linkPill.setAttribute(
        "aria-label",
        nodes.linkPill.dataset.link === "ap"
          ? t("statusbar.connection.ap", null, "Connected (AP Hotspot)")
          : t("statusbar.connection.station", null, "Connected (Station)"),
      );
    }
  }

  function setBoardTemp(value) {
    setText(nodes.boardTemp, value);
  }

  function setSinkTemp(value) {
    setText(nodes.sinkTemp, value);
  }

  function setVoltage(value) {
    setText(nodes.dcVoltage, value);
  }

  function setCurrent(value) {
    setText(nodes.dcCurrent, value);
  }

  function setWarnings(count) {
    const value = Number.isFinite(count) ? count : 0;
    setText(nodes.warnCount, String(value));
    setState(nodes.warnPill, value > 0 ? "warn" : "ok");
    if (value > lastWarn) {
      showAlert(t("statusbar.alert.warn", null, "New warning"), "warn");
    }
    lastWarn = value;
  }

  function setErrors(count) {
    const value = Number.isFinite(count) ? count : 0;
    setText(nodes.errCount, String(value));
    setState(nodes.errPill, value > 0 ? "err" : "ok");
    if (value > lastErr) {
      showAlert(t("statusbar.alert.error", null, "New error"), "err");
    }
    lastErr = value;
  }

  function setUserRole(value) {
    const raw = String(value || "").trim();
    const lower = raw.toLowerCase();
    if (lower === "admin" || lower === "user") {
      setText(nodes.userRole, t(`statusbar.role.${lower}`, null, raw));
      return;
    }
    setText(nodes.userRole, raw);
  }

  function setMode(value) {
    const modeValue = String(value || "").trim();
    const lower = modeValue.toLowerCase();
    const isActive =
      modeValue.length > 0 &&
      !lower.includes("idle") &&
      !lower.includes("stopped") &&
      !lower.includes("off");
    let label = modeValue;
    if (lower === "running") {
      label = t("statusbar.mode.running", null, "Running");
    } else if (lower === "wire calibration") {
      label = t("statusbar.mode.wireCalibration", null, "Wire calibration");
    } else if (lower === "floor calibration") {
      label = t("statusbar.mode.floorCalibration", null, "Floor calibration");
    } else if (lower === "idle") {
      label = t("statusbar.mode.idle", null, "Idle");
    } else if (lower === "off") {
      label = t("statusbar.mode.off", null, "Off");
    }
    setText(nodes.modeLabel, label);
    if (nodes.modePill) {
      nodes.modePill.dataset.mode = normalizeMode(modeValue);
      nodes.modePill.classList.toggle("is-active", isActive);
      nodes.modePill.classList.toggle("is-hidden", !isActive);
    }
    if (nodes.liveBtn) {
      nodes.liveBtn.classList.toggle("is-hidden", !isActive);
    }
  }

  return {
    setConnection,
    setBoardTemp,
    setSinkTemp,
    setVoltage,
    setCurrent,
    setWarnings,
    setErrors,
    setUserRole,
    setMode,
    notify,
  };
}
