  // ========================================================
  // ===============     SETUP WIZARD FLOW      =============
  // ========================================================

  const WIZARD_STEPS = [
    {
      id: "wizardStepIntro",
      title: "Welcome",
      desc: "Review missing items before starting setup.",
      optional: false,
      stage: 0,
    },
    {
      id: "wizardStepCredentials",
      title: "Credentials",
      desc: "Set admin and user accounts.",
      optional: true,
      skipKey: "cred",
      stage: 1,
    },
    {
      id: "wizardStepWifi",
      title: "Wi-Fi",
      desc: "Configure station and access point settings.",
      optional: true,
      skipKey: "wifi",
      stage: 2,
    },
    {
      id: "wizardStepOutputs",
      title: "Output Selection",
      desc: "Enable the outputs used for setup and runtime.",
      optional: false,
      stage: 3,
    },
    {
      id: "wizardStepPresence",
      title: "Presence Check",
      desc: "Run the voltage-drop presence probe.",
      optional: true,
      skipKey: "presence",
      stage: 4,
    },
    {
      id: "wizardStepNtcParams",
      title: "NTC Parameters",
      desc: "Confirm beta, T0, and resistance values.",
      optional: false,
      stage: 5,
    },
    {
      id: "wizardStepDevice",
      title: "Device Settings",
      desc: "Set device, safety, wire, and floor configuration.",
      optional: false,
      stage: 6,
    },
    {
      id: "wizardStepWireCal",
      title: "Wire Calibration",
      desc: "Calibrate wires now or skip for later.",
      optional: true,
      skipKey: "wire",
      stage: 7,
    },
    {
      id: "wizardStepFloorCal",
      title: "Floor Calibration",
      desc: "Calibrate floor model now or skip for later.",
      optional: true,
      skipKey: "floor",
      stage: 8,
    },
    {
      id: "wizardStepFinish",
      title: "Finish",
      desc: "Complete setup and review calibration status.",
      optional: false,
      stage: 9,
    },
  ];

  const WIZARD_CRED_KEYS = new Set(["ADMID", "ADMPW", "USRID", "USRPW"]);
  const WIZARD_WIFI_KEYS = new Set(["WIFSSD", "WIFPASS", "APNAM", "APPSS"]);
  const WIZARD_OUTPUT_KEYS = new Set(["outputs"]);
  const WIZARD_NTC_KEYS = new Set(["NTCBT", "NTCT0", "NTCR0", "NTCFR"]);
  const WIZARD_OPTIONAL_CALIB_KEYS = new Set([]);

  function normalizeSetupKeys(list) {
    if (!Array.isArray(list)) return [];
    return list.map((k) => String(k || "").trim()).filter((k) => k.length);
  }

  function getMissingConfigForStep(status, stepKey) {
    const missing = normalizeSetupKeys(status && status.missingConfig);
    if (stepKey === "cred") {
      return missing.filter((k) => WIZARD_CRED_KEYS.has(k));
    }
    if (stepKey === "wifi") {
      return missing.filter((k) => WIZARD_WIFI_KEYS.has(k));
    }
    if (stepKey === "outputs") {
      return missing.filter((k) => WIZARD_OUTPUT_KEYS.has(k));
    }
    if (stepKey === "ntc") {
      return missing.filter((k) => WIZARD_NTC_KEYS.has(k));
    }
    if (stepKey === "device") {
      return missing.filter(
        (k) =>
          !WIZARD_CRED_KEYS.has(k) &&
          !WIZARD_WIFI_KEYS.has(k) &&
          !WIZARD_OUTPUT_KEYS.has(k) &&
          !WIZARD_NTC_KEYS.has(k)
      );
    }
    return [];
  }

  function getMissingCalibForStep(status, stepKey) {
    const missing = normalizeSetupKeys(status && status.missingCalib);
    if (stepKey === "wire") {
      return missing.filter((k) => /^CALW\d+$/.test(k));
    }
    if (stepKey === "floor") {
      return missing.filter((k) => k === "CALFLR");
    }
    if (stepKey === "presence") {
      return missing.filter((k) => k === "CALPRS");
    }
    return [];
  }

  function getWizardMissingConfig(status) {
    let missing = normalizeSetupKeys(status && status.missingConfig);
    if (wizardSkipped.cred) {
      missing = missing.filter((k) => !WIZARD_CRED_KEYS.has(k));
    }
    if (wizardSkipped.wifi) {
      missing = missing.filter((k) => !WIZARD_WIFI_KEYS.has(k));
    }
    return filterMissingForAccess(missing);
  }

  function getWizardMissingCalib(status) {
    let missing = normalizeSetupKeys(status && status.missingCalib);
    missing = missing.filter((k) => !WIZARD_OPTIONAL_CALIB_KEYS.has(k));
    if (wizardSkipped.wire) {
      missing = missing.filter((k) => !/^CALW\d+$/.test(k));
    }
    if (wizardSkipped.floor) {
      missing = missing.filter((k) => k !== "CALFLR");
    }
    if (wizardSkipped.presence) {
      missing = missing.filter((k) => k !== "CALPRS");
    }
    return filterMissingForAccess(missing);
  }

  function isWizardConfigOk(status) {
    return getWizardMissingConfig(status).length === 0;
  }

  function isWizardCalibOk(status) {
    return getWizardMissingCalib(status).length === 0;
  }

  function syncWizardSkippedWithStatus(status) {
    if (!status) return;
    if (wizardSkipped.cred && getMissingConfigForStep(status, "cred").length === 0) {
      setWizardSkipped("cred", false);
    }
    if (wizardSkipped.wifi && getMissingConfigForStep(status, "wifi").length === 0) {
      setWizardSkipped("wifi", false);
    }
    if (wizardSkipped.wire && getMissingCalibForStep(status, "wire").length === 0) {
      setWizardSkipped("wire", false);
    }
    if (wizardSkipped.floor && getMissingCalibForStep(status, "floor").length === 0) {
      setWizardSkipped("floor", false);
    }
    if (
      wizardSkipped.presence &&
      getMissingCalibForStep(status, "presence").length === 0
    ) {
      setWizardSkipped("presence", false);
    }
  }

  function isWizardStepComplete(index, status) {
    const step = WIZARD_STEPS[index];
    if (!step) return false;
    if (step.id === "wizardStepIntro") return true;
    if (step.id === "wizardStepCredentials") {
      return (
        wizardSkipped.cred ||
        getMissingConfigForStep(status, "cred").length === 0
      );
    }
    if (step.id === "wizardStepWifi") {
      return (
        wizardSkipped.wifi ||
        getMissingConfigForStep(status, "wifi").length === 0
      );
    }
    if (step.id === "wizardStepDevice") {
      return getMissingConfigForStep(status, "device").length === 0;
    }
    if (step.id === "wizardStepPresence") {
      return (
        wizardSkipped.presence ||
        getMissingCalibForStep(status, "presence").length === 0
      );
    }
    if (step.id === "wizardStepOutputs") {
      return getMissingConfigForStep(status, "outputs").length === 0;
    }
    if (step.id === "wizardStepNtcParams") {
      return getMissingConfigForStep(status, "ntc").length === 0;
    }
    if (step.id === "wizardStepWireCal") {
      return (
        wizardSkipped.wire ||
        getMissingCalibForStep(status, "wire").length === 0
      );
    }
    if (step.id === "wizardStepFloorCal") {
      return (
        wizardSkipped.floor ||
        getMissingCalibForStep(status, "floor").length === 0
      );
    }
    if (step.id === "wizardStepFinish") {
      return isWizardConfigOk(status) && isWizardCalibOk(status);
    }
    return true;
  }

  function wizardMaxAccessibleIndex(status) {
    if (!status) return wizardStepIndex;
    for (let i = 0; i < WIZARD_STEPS.length; i++) {
      if (!isWizardStepComplete(i, status)) return i;
    }
    return WIZARD_STEPS.length - 1;
  }

  function refreshWizardRailState(status) {
    const rail = document.getElementById("setupWizardRail");
    if (!rail) return;
    const items = rail.querySelectorAll(".wizard-rail-item");
    if (!items.length) return;
    const maxIndex = wizardMaxAccessibleIndex(status);
    items.forEach((item, i) => {
      const step = WIZARD_STEPS[i];
      const skipped = !!(step && step.skipKey && wizardSkipped[step.skipKey]);
      item.classList.toggle("active", i === wizardStepIndex);
      item.classList.toggle("disabled", i > maxIndex);
      item.classList.toggle("complete", isWizardStepComplete(i, status));
      item.classList.toggle("skipped", skipped);
      item.setAttribute("aria-disabled", i > maxIndex ? "true" : "false");
      if (i > maxIndex) {
        item.title = "Complete or skip previous steps first.";
      } else if (skipped) {
        item.title = "Skipped for now.";
      } else {
        item.title = "";
      }
    });
  }

  function wizardIndexForStage(stage) {
    const s = Number(stage);
    if (!Number.isFinite(s)) return 0;
    const idx = WIZARD_STEPS.findIndex((step) => step.stage === s);
    if (idx >= 0) return idx;
    return s <= WIZARD_STEPS[0].stage ? 0 : WIZARD_STEPS.length - 1;
  }

  function syncWizardStepFromStatus(status) {
    if (!status) return;
    syncWizardSkippedWithStatus(status);
    const nextIndex = wizardIndexForStage(status.stage);
    const maxIndex = wizardMaxAccessibleIndex(status);
    const targetIndex = Math.min(nextIndex, maxIndex);
    if (targetIndex === wizardStepIndex) {
      if (wizardActive) refreshWizardRailState(status);
      return;
    }
    wizardStepIndex = targetIndex;
    if (wizardActive) {
      setWizardStep(wizardStepIndex, false);
    }
  }

  function hasMissingWireCalibration(status) {
    const missingRaw =
      status && Array.isArray(status.missingCalib) ? status.missingCalib : [];
    const missing = filterMissingForAccess(missingRaw);
    return missing.some((key) => /^CALW\d+$/.test(String(key || "")));
  }

  function formatWizardWireStatus(status) {
    if (!status) return "Pending";
    const runMap = status.wireRunning || {};
    const stageMap = status.wireStage || {};
    const runningKey = Object.keys(runMap).find((k) => !!runMap[k]);
    if (runningKey) {
      const stage = Number(stageMap[runningKey] || 0);
      const stageLabel = stage ? ` stage ${stage}` : "";
      return `Running (W${runningKey}${stageLabel})`;
    }
    return hasMissingWireCalibration(status) ? "Pending" : "Done";
  }

  function formatWizardFloorStatus(status) {
    if (!status) return "Pending";
    const floorStage = Number(status.floorStage || 0);
    if (status.floorRunning) {
      return `Running (stage ${floorStage || 0})`;
    }
    if (status.floorCalibrated) return "Done";
    if (floorStage) return `Stage ${floorStage}`;
    return "Pending";
  }

  function setupWizardOverlay() {
    return document.getElementById("setupWizardOverlay");
  }

  function buildWizardRail() {
    const rail = document.getElementById("setupWizardRail");
    if (!rail) return;
    rail.innerHTML = "";
    WIZARD_STEPS.forEach((step, i) => {
      const item = document.createElement("div");
      item.className = "wizard-rail-item";
      if (step.optional) item.classList.add("optional");
      item.textContent = `${i + 1}. ${step.title}`;
      item.addEventListener("click", () => requestWizardStep(i));
      rail.appendChild(item);
    });
    refreshWizardRailState(lastSetupStatus);
  }

  function cacheWizardNode(el) {
    if (!el) return;
    const exists = wizardNodeCache.some((item) => item.el === el);
    if (exists) return;
    wizardNodeCache.push({
      el,
      parent: el.parentNode,
      next: el.nextSibling,
    });
  }

  function mountWizardNodes() {
    const map = [
      {
        selector: ".admin-credentials-card",
        mount: "wizardMountCredentials",
      },
      {
        selector: ".user-settings-card",
        mount: "wizardMountUser",
      },
      {
        selector: ".wifi-station-card",
        mount: "wizardMountWifiStation",
      },
      {
        selector: ".wifi-ap-card",
        mount: "wizardMountWifiAp",
      },
      {
        selector: ".device-settings-box",
        mount: "wizardMountDevice",
      },
      {
        selector: "#ntcParamsCard",
        mount: "wizardMountNtcParams",
      },
      {
        selector: "#presenceProbeSection",
        mount: "wizardMountPresence",
      },
      {
        selector: ".user-access-card",
        mount: "wizardMountOutputs",
      },
    ];

    map.forEach((entry) => {
      const el = document.querySelector(entry.selector);
      const mount = document.getElementById(entry.mount);
      if (!el || !mount) return;
      cacheWizardNode(el);
      mount.appendChild(el);
    });
  }

  function restoreWizardNodes() {
    wizardNodeCache.forEach((item) => {
      if (!item.el || !item.parent) return;
      if (item.next && item.next.parentNode === item.parent) {
        item.parent.insertBefore(item.el, item.next);
      } else {
        item.parent.appendChild(item.el);
      }
    });
  }

  function setWizardStep(index, updateStage = false) {
    if (index < 0 || index >= WIZARD_STEPS.length) return;
    wizardStepIndex = index;

    WIZARD_STEPS.forEach((step, i) => {
      const el = document.getElementById(step.id);
      if (el) el.classList.toggle("active", i === index);
    });

    const rail = document.getElementById("setupWizardRail");
    if (rail) {
      const items = rail.querySelectorAll(".wizard-rail-item");
      items.forEach((item, i) => {
        item.classList.toggle("active", i === index);
      });
    }

    const step = WIZARD_STEPS[index];
    const titleEl = document.getElementById("setupWizardStepTitle");
    const descEl = document.getElementById("setupWizardStepDesc");
    if (titleEl) titleEl.textContent = step.title;
    if (descEl) descEl.textContent = step.desc;

    updateWizardControls();
    updateWizardNote();
    updateWizardFloorGate(lastSetupStatus);
    refreshWizardRailState(lastSetupStatus);

    if (updateStage) {
      updateSetupStatus({ stage: step.stage, substage: 0 });
    }
  }

  function updateWizardControls() {
    const backBtn = document.getElementById("wizardBackBtn");
    const nextBtn = document.getElementById("wizardNextBtn");
    const skipBtn = document.getElementById("wizardSkipBtn");
    const finishBtn = document.getElementById("wizardFinishBtn");

    const isFirst = wizardStepIndex === 0;
    const isLast = wizardStepIndex === WIZARD_STEPS.length - 1;
    const step = WIZARD_STEPS[wizardStepIndex];
    const status = lastSetupStatus;
    const canAdvance = !status || isWizardStepComplete(wizardStepIndex, status);
    const canFinish = !status || isWizardConfigOk(status);

    if (backBtn) backBtn.disabled = isFirst;
    if (nextBtn) nextBtn.style.display = isLast ? "none" : "";
    if (nextBtn) nextBtn.disabled = !canAdvance;
    if (finishBtn) finishBtn.style.display = isLast ? "" : "none";
    if (finishBtn) finishBtn.disabled = !canFinish;
    if (skipBtn) skipBtn.style.display = step.optional ? "" : "none";
  }

  function updateWizardNote() {
    const note = document.getElementById("setupWizardNote");
    if (!note) return;
    const step = WIZARD_STEPS[wizardStepIndex];
    if (step && step.optional) {
      const skipped = !!(step.skipKey && wizardSkipped[step.skipKey]);
      note.textContent = skipped
        ? "Step skipped. You can complete it later."
        : "You can skip this step and return later.";
    } else {
      note.textContent = "Complete this step to proceed.";
    }
  }

  function updateWizardFloorGate(status) {
    const note = document.getElementById("wizardFloorGateNote");
    const floorBtn = document.getElementById("wizardOpenFloorCalBtn");
    if (!note && !floorBtn) return;

    const wireCalComplete =
      !status || getMissingCalibForStep(status, "wire").length === 0;
    if (note) note.classList.toggle("show", !wireCalComplete);
    if (floorBtn) floorBtn.disabled = !wireCalComplete;
  }

  function updateWizardStatus(status) {
    const configPill = document.getElementById("setupWizardConfigPill");
    const calibPill = document.getElementById("setupWizardCalibPill");
    const wizardConfigOk = isWizardConfigOk(status);
    const wizardCalibOk = isWizardCalibOk(status);
    if (configPill) {
      setSetupPill(
        "setupWizardConfigPill",
        `Config: ${wizardConfigOk ? "OK" : "Missing"}`,
        wizardConfigOk ? "ok" : "warn"
      );
    }
    if (calibPill) {
      setSetupPill(
        "setupWizardCalibPill",
        `Calibration: ${wizardCalibOk ? "OK" : "Pending"}`,
        wizardCalibOk ? "ok" : "warn"
      );
    }

    renderSetupMissingList("wizardMissingConfigList", getWizardMissingConfig(status));
    renderSetupMissingList("wizardMissingCalibList", getWizardMissingCalib(status));

    const wireStatusEl = document.getElementById("wizardWireCalStatus");
    if (wireStatusEl) {
      wireStatusEl.textContent = formatWizardWireStatus(status);
    }

    const floorStatusEl = document.getElementById("wizardFloorCalStatus");
    if (floorStatusEl) {
      floorStatusEl.textContent = formatWizardFloorStatus(status);
    }

    const summaryConfig = document.getElementById("wizardSummaryConfig");
    const summaryCalib = document.getElementById("wizardSummaryCalib");
    if (summaryConfig) {
      summaryConfig.textContent = wizardConfigOk ? "Complete" : "Incomplete";
    }
    if (summaryCalib) {
      summaryCalib.textContent = wizardCalibOk ? "Complete" : "Pending";
    }
    updateWizardFloorGate(status);
    updateWizardControls();
    refreshWizardRailState(status);
  }

  function updateSetupWizardOverlay(status) {
    if (!wizardActive) return;
    if (!status) return;
    updateWizardStatus(status);
  }

  function openSetupWizard() {
    const overlay = setupWizardOverlay();
    if (!overlay) return;
    if (!wizardActive) {
      wizardActive = true;
      overlay.classList.add("show");
      overlay.setAttribute("aria-hidden", "false");
      document.body.classList.add("wizard-active");
      mountWizardNodes();
      bindDeviceSettingsSubtabs(document);
      buildWizardRail();
      const maxIndex = wizardMaxAccessibleIndex(lastSetupStatus);
      wizardStepIndex = Math.min(wizardStepIndex, maxIndex);
      setWizardStep(wizardStepIndex);
    }
  }

  function closeSetupWizard() {
    const overlay = setupWizardOverlay();
    if (!overlay) return;
    overlay.classList.remove("show");
    overlay.setAttribute("aria-hidden", "true");
    document.body.classList.remove("wizard-active");
    wizardActive = false;
    restoreWizardNodes();
  }

  function maybeShowSetupWizard(status) {
    if (!status) return;
    const needsWizard = !status.setupDone || !isWizardConfigOk(status);
    if (needsWizard) {
      openSetupWizard();
    } else if (wizardActive) {
      closeSetupWizard();
    }
  }

  function requestWizardStep(index) {
    if (index < 0 || index >= WIZARD_STEPS.length) return;
    const status = lastSetupStatus;
    if (status) {
      const maxIndex = wizardMaxAccessibleIndex(status);
      if (index > maxIndex && index !== wizardStepIndex) {
        openAlert(
          "Setup Wizard",
          "Complete or skip the previous step before continuing.",
          "warning"
        );
        return;
      }
    }
    setWizardStep(index, true);
  }

  function wizardNext() {
    const status = lastSetupStatus;
    if (status && !isWizardStepComplete(wizardStepIndex, status)) {
      openAlert(
        "Setup Wizard",
        "Complete or skip this step to continue.",
        "warning"
      );
      return;
    }
    const next = Math.min(wizardStepIndex + 1, WIZARD_STEPS.length - 1);
    requestWizardStep(next);
  }

  function wizardBack() {
    const prev = Math.max(wizardStepIndex - 1, 0);
    requestWizardStep(prev);
  }

  function wizardSkip() {
    const step = WIZARD_STEPS[wizardStepIndex];
    if (step && step.skipKey) {
      setWizardSkipped(step.skipKey, true);
      updateWizardStatus(lastSetupStatus);
    }
    wizardNext();
  }

  async function wizardFinish() {
    const status = await fetchSetupStatus();
    if (!status) return;

    const missingConfig = getWizardMissingConfig(status);
    if (missingConfig.length) {
      const detail = missingConfig.slice(0, 5).map(setupLabelForKey).join(", ");
      openAlert(
        "Setup Wizard",
        `Configuration is incomplete: ${detail}`,
        "warning"
      );
      return;
    }

    const missingCalib = getWizardMissingCalib(status);
    if (missingCalib.length) {
      const detail = missingCalib.slice(0, 5).map(setupLabelForKey).join(", ");
      openAlert(
        "Setup Wizard",
        `Calibration is still pending: ${detail}. You can finish now and calibrate later.`,
        "warning"
      );
    }

    const finishStage = WIZARD_STEPS[WIZARD_STEPS.length - 1].stage;
    await updateSetupStatus({ setup_done: true, stage: finishStage, substage: 0 });
    const updated = await fetchSetupStatus();
    if (updated && updated.setupDone) {
      closeSetupWizard();
    }
  }

  function bindWizardControls() {
    const backBtn = document.getElementById("wizardBackBtn");
    const nextBtn = document.getElementById("wizardNextBtn");
    const skipBtn = document.getElementById("wizardSkipBtn");
    const finishBtn = document.getElementById("wizardFinishBtn");
    const wireBtn = document.getElementById("wizardOpenWireCalBtn");
    const floorBtn = document.getElementById("wizardOpenFloorCalBtn");
    const floorWireBtn = document.getElementById("wizardFloorGoWireBtn");
    const floorNtcBtn = document.getElementById("wizardFloorGoNtcBtn");

    if (backBtn) backBtn.addEventListener("click", wizardBack);
    if (nextBtn) nextBtn.addEventListener("click", wizardNext);
    if (skipBtn) skipBtn.addEventListener("click", wizardSkip);
    if (finishBtn) finishBtn.addEventListener("click", wizardFinish);

    if (wireBtn) {
      wireBtn.addEventListener("click", () => {
        openCalibrationModal();
      });
    }
    if (floorBtn) {
      floorBtn.addEventListener("click", () => {
        openCalibrationModal();
      });
    }
    if (floorWireBtn) {
      floorWireBtn.addEventListener("click", () => {
        const idx = WIZARD_STEPS.findIndex(
          (step) => step.id === "wizardStepWireCal"
        );
        if (idx >= 0) requestWizardStep(idx);
      });
    }
    if (floorNtcBtn) {
      floorNtcBtn.addEventListener("click", () => {
        const idx = WIZARD_STEPS.findIndex(
          (step) => step.id === "wizardStepNtcParams"
        );
        if (idx >= 0) requestWizardStep(idx);
      });
    }
  }

