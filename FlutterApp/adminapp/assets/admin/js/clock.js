  // ========================================================
  // ===============       DASHBOARD CLOCK      =============
  // ========================================================

  function initDashboardClock() {
    const hourHand = document.getElementById("dashboardClockHourHand");
    const minuteHand = document.getElementById("dashboardClockMinuteHand");
    const secondHand = document.getElementById("dashboardClockSecondHand");
    const dayEl = document.getElementById("dashboardClockDay");
    const dateEl = document.getElementById("dashboardClockDate");
    const timeEl = document.getElementById("dashboardClockTime");

    if (!hourHand || !minuteHand || !secondHand) return;

    const pad2 = (n) => String(n).padStart(2, "0");
    const cX = 60;
    const cY = 60;

    function render() {
      const now = new Date();
      const seconds = now.getSeconds() + now.getMilliseconds() / 1000;
      const minutes = now.getMinutes() + seconds / 60;
      const hours = (now.getHours() % 12) + minutes / 60;

      hourHand.setAttribute("transform", `rotate(${hours * 30} ${cX} ${cY})`);
      minuteHand.setAttribute(
        "transform",
        `rotate(${minutes * 6} ${cX} ${cY})`
      );
      secondHand.setAttribute(
        "transform",
        `rotate(${seconds * 6} ${cX} ${cY})`
      );

      if (dayEl) {
        try {
          dayEl.textContent = now.toLocaleDateString(undefined, {
            weekday: "long",
          });
        } catch (e) {
          dayEl.textContent = now.toDateString();
        }
      }

      if (dateEl) {
        try {
          dateEl.textContent = now.toLocaleDateString(undefined, {
            day: "2-digit",
            month: "short",
            year: "numeric",
          });
        } catch (e) {
          dateEl.textContent = now.toDateString();
        }
      }

      if (timeEl) {
        timeEl.textContent =
          pad2(now.getHours()) +
          ":" +
          pad2(now.getMinutes()) +
          ":" +
          pad2(now.getSeconds());
      }
    }

    render();
    if (dashboardClockTimer) clearInterval(dashboardClockTimer);
    dashboardClockTimer = setInterval(render, 250);
  }

  function sleep(ms) {
    return new Promise((resolve) => setTimeout(resolve, ms));
  }

  function approxEqual(a, b, eps = 0.05) {
    const na = Number(a);
    const nb = Number(b);
    if (!Number.isFinite(na) || !Number.isFinite(nb)) return false;
    return Math.abs(na - nb) <= eps;
  }

  function updateModePills() {
    const ltToggle = document.getElementById("ltToggle");
    const ltPill = document.getElementById("ltPill");

    const ltOn = !!(ltToggle && ltToggle.checked);
    if (ltPill) ltPill.classList.toggle("is-active", ltOn);
  }

  function setFanSpeedValue(value) {
    const el = document.getElementById("fanSpeedValue");
    if (!el) return;
    const v = Number(value);
    el.textContent = Number.isFinite(v) ? `${Math.round(v)}%` : "--%";
  }

  function setField(id, val) {
    const el = document.getElementById(id);
    if (!el) return;
    if (val === undefined || val === null || Number.isNaN(val)) {
      el.value = "";
    } else {
      el.value = val;
    }
  }

  function setText(id, val) {
    const el = document.getElementById(id);
    if (!el) return;
    if (val === undefined || val === null || val === "") {
      el.textContent = "--";
    } else {
      el.textContent = val;
    }
  }

  function floorMaterialFromCode(code) {
    switch (code) {
      case 0:
        return "wood";
      case 1:
        return "epoxy";
      case 2:
        return "concrete";
      case 3:
        return "slate";
      case 4:
        return "marble";
      case 5:
        return "granite";
      default:
        return "wood";
    }
  }

  function getFloat(id) {
    const el = document.getElementById(id);
    if (!el) return undefined;
    const v = parseFloat(el.value);
    return Number.isFinite(v) ? v : undefined;
  }

  function getInt(id) {
    const el = document.getElementById(id);
    if (!el) return undefined;
    const v = parseInt(el.value, 10);
    return Number.isFinite(v) ? v : undefined;
  }

  function getBool(id) {
    const el = document.getElementById(id);
    if (!el) return false;
    return !!el.checked;
  }

  function formatValue(val, digits = 2, unit = "") {
    const n = Number(val);
    if (!Number.isFinite(n)) return "--";
    const base = n.toFixed(digits);
    return unit ? `${base} ${unit}` : base;
  }

  const SETUP_KEY_LABELS = {
    DEVID: "Device ID",
    ADMID: "Admin username",
    ADMPW: "Admin password",
    USRID: "User username",
    USRPW: "User password",
    WIFSSD: "Wi-Fi SSID",
    WIFPASS: "Wi-Fi password",
    APNAM: "AP SSID",
    APPSS: "AP password",
    TMPTH: "Temp trip",
    TPWRN: "Temp warn",
    FLMAX: "Floor max temp",
    NCFIN: "Nichrome final temp",
    FLMRG: "Floor switch margin",
    CURRLT: "Current limit",
    CSRC: "Current source",
    ACFRQ: "AC frequency",
    ACVLT: "AC voltage",
    CHRES: "Charge resistor",
    WOPERM: "Wire ohm/m",
    WIREG: "Wire gauge",
    NTCGT: "NTC linked channel",
    NTCBT: "NTC beta",
    NTCT0: "NTC T0",
    NTCR0: "NTC R0 / R25",
    NTCFR: "NTC fixed resistor",
    PMIND: "Presence min voltage drop",
    outputs: "Output access flags",
    CALCAP: "Cap calibration",
    CPCAPF: "Capacitance",
    CALFLR: "Floor model calibration",
    CALPRS: "Presence calibration",
  };

  for (let i = 1; i <= 10; i++) {
    const rKey = "R" + String(i).padStart(2, "0") + "OHM";
    SETUP_KEY_LABELS[rKey] = `Wire ${i} resistance`;
    SETUP_KEY_LABELS["CALW" + String(i)] = `Wire ${i} calibration`;
  }

  function setupLabelForKey(key) {
    if (!key) return "--";
    return SETUP_KEY_LABELS[key] || key;
  }

  function filterMissingForAccess(keys) {
    if (!Array.isArray(keys)) return [];
    const access =
      (lastLoadedControls && lastLoadedControls.outputAccess) || null;
    if (!access) return keys.slice();

    return keys.filter((raw) => {
      const key = String(raw || "");
      let match = /^CALW(\d+)$/.exec(key);
      if (!match) match = /^R(\d{2})OHM$/.exec(key);
      if (!match) return true;
      const idx = parseInt(match[1], 10);
      if (!idx) return true;
      const flag = access["output" + idx];
      return flag !== false;
    });
  }

  function renderSetupMissingList(listId, keys) {
    const ul = document.getElementById(listId);
    if (!ul) return;
    ul.innerHTML = "";
    const filtered = filterMissingForAccess(keys);
    if (!Array.isArray(filtered) || filtered.length === 0) {
      const li = document.createElement("li");
      li.textContent = "None";
      ul.appendChild(li);
      return;
    }
    filtered.forEach((k) => {
      const li = document.createElement("li");
      li.textContent = setupLabelForKey(k);
      ul.appendChild(li);
    });
  }

  function setSetupPill(id, label, state) {
    const el = document.getElementById(id);
    if (!el) return;
    el.textContent = label;
    el.classList.remove("ok", "warn", "bad");
    if (state === "ok") el.classList.add("ok");
    else if (state === "bad") el.classList.add("bad");
    else el.classList.add("warn");
  }

  function setUserTabEnabled(enabled) {
    const tab = document.getElementById("userTabBtn");
    if (!tab) return;
    tab.classList.toggle("tab-disabled", !enabled);
    tab.title = enabled ? "" : "User mode locked until setup is complete.";
    if (!enabled && tab.classList.contains("active")) {
      switchTab(0);
    }
  }

  function renderSetupWireGrid(status) {
    const grid = document.getElementById("setupWireGrid");
    if (!grid) return;
    grid.innerHTML = "";
    const stageMap = (status && status.wireStage) || {};
    const runMap = (status && status.wireRunning) || {};
    const doneMap = (status && status.wireCalibrated) || {};
    const access =
      (lastLoadedControls && lastLoadedControls.outputAccess) || null;

    for (let i = 1; i <= 10; i++) {
      if (access && access["output" + i] === false) {
        continue;
      }
      const key = String(i);
      const stage = Number(stageMap[key] || 0);
      const running = !!runMap[key];
      const done = !!doneMap[key];
      const chip = document.createElement("div");
      chip.className = "setup-wire-chip";
      if (done) chip.classList.add("is-done");
      else if (running) chip.classList.add("is-running");
      let label = `W${i}`;
      if (running) label += ` - stage ${stage || 0}`;
      else if (done) label += " - done";
      else if (stage > 0) label += ` - stage ${stage}`;
      else label += " - pending";
      chip.textContent = label;
      grid.appendChild(chip);
    }
  }

  function updateSetupBanner(status) {
    const banner = document.getElementById("setupBanner");
    if (!banner) return;
    const setupDone = !!(status && status.setupDone);
    const configOk = !!(status && status.configOk);
    const calibOk = !!(status && status.calibOk);
    const needsConfig = !setupDone || !configOk;
    const needsCalib = setupDone && configOk && !calibOk;
    const show = needsConfig || needsCalib;
    banner.classList.toggle("show", show);
    const dashboard = document.getElementById("dashboardTab");
    if (dashboard) dashboard.classList.toggle("banner-active", show);

    const titleEl = document.getElementById("setupBannerTitle");
    const mainTextEl = document.getElementById("setupBannerMainText");
    const detailEl = document.getElementById("setupBannerDetail");

    if (titleEl) {
      titleEl.textContent = needsCalib ? "Calibration pending" : "Setup required";
    }
    if (mainTextEl) {
      mainTextEl.textContent = needsCalib
        ? "Calibration is not finished yet. You can complete it later."
        : "Complete the setup wizard before running the device.";
    }

    const missingRaw = needsConfig
      ? (status && status.missingConfig) || []
      : (status && status.missingCalib) || [];
    const missing = filterMissingForAccess(missingRaw);
    const detail = missing.slice(0, 5).map(setupLabelForKey).join(", ");
    if (detailEl) {
      if (missing.length) {
        detailEl.textContent = `Missing: ${detail}`;
      } else if (needsCalib) {
        detailEl.textContent = "All required calibration steps are done.";
      } else {
        detailEl.textContent = "All required items complete.";
      }
    }
  }

  function updateSetupUiFromStatus(status) {
    if (!status) return;
    lastSetupStatus = status;
    setupConfigOk = !!status.configOk;
    setupCalibOk = !!status.calibOk;
    setupRunAllowed = !!status.runAllowed && setupCalibOk;
    syncWizardStepFromStatus(status);

    const setupDone = !!status.setupDone;
    setSetupPill(
      "setupDonePill",
      `Setup: ${setupDone ? "Done" : "Pending"}`,
      setupDone ? "ok" : "warn"
    );
    setSetupPill(
      "setupConfigPill",
      `Config: ${setupConfigOk ? "OK" : "Missing"}`,
      setupConfigOk ? "ok" : "warn"
    );
    setSetupPill(
      "setupCalibPill",
      `Calibration: ${setupCalibOk ? "OK" : "Missing"}`,
      setupCalibOk ? "ok" : "warn"
    );
    setSetupPill(
      "setupRunPill",
      `Run: ${setupRunAllowed ? "Allowed" : "Blocked"}`,
      setupRunAllowed ? "ok" : "bad"
    );

    setField("setupStageInput", status.stage);
    setField("setupSubstageInput", status.substage);
    setField("setupWireIndexInput", status.wireIndex);

    renderSetupMissingList("setupMissingConfigList", status.missingConfig);
    renderSetupMissingList("setupMissingCalibList", status.missingCalib);

    const floorCal = !!status.floorCalibrated;
    const floorRunning = !!status.floorRunning;
    const floorStage = Number(status.floorStage || 0);
    const floorStatus = floorCal
      ? "Done"
      : floorRunning
      ? `Running (stage ${floorStage})`
      : floorStage
      ? `Stage ${floorStage}`
      : "Pending";
    setText("setupFloorStatus", floorStatus);
    setText("floorCalStageText", floorStage ? String(floorStage) : "--");
    setText("floorCalRunningText", floorRunning ? "Yes" : "No");
    if (status.floorCalibrated !== undefined) {
      setText("floorCalDoneText", floorCal ? "Yes" : "No");
    }
    setText(
      "setupPresenceStatus",
      status.presenceCalibrated ? "Done" : "Pending"
    );
    setText("setupCapStatus", status.capCalibrated ? "Done" : "Pending");

    renderSetupWireGrid(status);
    updateSetupBanner(status);
    setUserTabEnabled(setupDone);
    updateSetupWizardOverlay(status);
    maybeShowSetupWizard(status);
    applySafetyLocks();
  }

