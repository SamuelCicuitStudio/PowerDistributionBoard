  // Calibration modal controls
  let calibPollTimer = null;
  let calibPollIntervalMs = 1000;
  let calibSamples = [];
  let calibViewMode = "live";
  let calibLastMeta = null;
  let wireTestPollTimer = null;
  let calibChartPaused = false;
  let calibChartDrag = {
    dragging: false,
    startX: 0,
    startScrollLeft: 0,
  };

  function setCalibText(id, value) {
    const el = document.getElementById(id);
    if (el) el.textContent = value;
  }

  function mountOverlayInsideUiContainer(id) {
    const el = document.getElementById(id);
    const stage = document.querySelector(".ui-container");
    if (!el || !stage) return;
    if (el.parentElement !== stage) stage.appendChild(el);
  }

  function mountAllOverlays() {
    mountOverlayInsideUiContainer("sessionHistoryModal");
    mountOverlayInsideUiContainer("calibrationModal");
    mountOverlayInsideUiContainer("errorModal");
    mountOverlayInsideUiContainer("logModal");
    mountOverlayInsideUiContainer("liveControlModal");
    const shClose = document.querySelector(
      "#sessionHistoryModal .session-history-close"
    );
    if (shClose) shClose.textContent = "x";
  }

  function syncCalibrationInputsFromControls(data) {
    if (!data) return;
    if (data.ntcCalTargetC !== undefined) {
      setField("ntcCalTargetC", data.ntcCalTargetC);
    }
    if (data.ntcCalSampleMs !== undefined) {
      setField("ntcCalSampleMs", data.ntcCalSampleMs);
    }
    if (data.ntcCalTimeoutMs !== undefined) {
      setField("ntcCalTimeoutMs", data.ntcCalTimeoutMs);
    }
    if (data.floorMaxC !== undefined) {
      const floorTarget = document.getElementById("floorCalTargetC");
      if (floorTarget && !floorTarget.value) {
        floorTarget.value = data.floorMaxC;
      }
    }
    if (data.ntcGateIndex !== undefined) {
      const floorWire = document.getElementById("floorCalWireIndex");
      if (floorWire && !floorWire.value) {
        floorWire.value = data.ntcGateIndex;
      }
    }
    const duty = document.getElementById("floorCalDutyPct");
    if (duty && !duty.value) duty.value = "50";
    const ambient = document.getElementById("floorCalAmbientMin");
    if (ambient && !ambient.value) ambient.value = "5";
    const heat = document.getElementById("floorCalHeatMin");
    if (heat && !heat.value) heat.value = "20";
    const cool = document.getElementById("floorCalCoolMin");
    if (cool && !cool.value) cool.value = "10";
    const interval = document.getElementById("floorCalIntervalMs");
    if (interval && !interval.value) interval.value = "1000";

    if (data.floorCalibrated !== undefined) {
      setText("floorCalDoneText", data.floorCalibrated ? "Yes" : "No");
    }
    if (data.floorTau !== undefined) {
      setText("floorCalTauText", formatValue(data.floorTau, 2));
    }
    if (data.floorK !== undefined) {
      setText("floorCalKText", formatValue(data.floorK, 3));
    }
    if (data.floorC !== undefined) {
      setText("floorCalCText", formatValue(data.floorC, 2));
    }
  }

  function openCalibrationModal() {
    const m = document.getElementById("calibrationModal");
    if (!m) return;
    m.classList.add("show");
    calibChartPaused = false;
    const pauseBtn = document.getElementById("calibPauseBtn");
    if (pauseBtn) pauseBtn.textContent = "Pause";
    calibViewMode = "live";
    calibSamples = [];
    const historyBtn = document.getElementById("calibHistoryBtn");
    if (historyBtn) historyBtn.textContent = "Load History";
    setCalibrationInfoVisible(false);
    if (lastLoadedControls) {
      syncCalibrationInputsFromControls(lastLoadedControls);
    }
    initCalibrationChartUi();
    startCalibrationPoll();
    startNtcCalPoll();
    startWireTestPoll();
    refreshCalibHistoryList(true);
  }

  function closeCalibrationModal() {
    const m = document.getElementById("calibrationModal");
    if (m) m.classList.remove("show");
    calibChartPaused = false;
    setCalibrationInfoVisible(false);
    stopCalibrationPoll();
    stopNtcCalPoll();
  }

  function bindCalibrationButton() {
    const btn = document.getElementById("calibrationBtn");
    if (btn) btn.addEventListener("click", openCalibrationModal);

    const infoBtn = document.getElementById("calibrationInfoBtn");
    if (infoBtn) {
      infoBtn.addEventListener("click", toggleCalibrationInfo);
    }
    const infoClose = document.getElementById("calibrationInfoCloseBtn");
    if (infoClose) {
      infoClose.addEventListener("click", () =>
        setCalibrationInfoVisible(false)
      );
    }
    const latestBtn = document.getElementById("calibLatestBtn");
    if (latestBtn) {
      latestBtn.addEventListener("click", () => scrollCalibToLatest());
    }
    const pauseBtn = document.getElementById("calibPauseBtn");
    if (pauseBtn) {
      pauseBtn.addEventListener("click", toggleCalibPause);
    }
    const startModel = document.getElementById("startModelCalibBtn");
    if (startModel) {
      startModel.addEventListener("click", () => startCalibration("model"));
    }

    const stopBtn = document.getElementById("stopCalibBtn");
    if (stopBtn) {
      stopBtn.addEventListener("click", stopCalibration);
    }

    const wireStart = document.getElementById("wireTestStartBtn");
    if (wireStart) {
      wireStart.addEventListener("click", startWireTest);
    }

    const wireStop = document.getElementById("wireTestStopBtn");
    if (wireStop) {
      wireStop.addEventListener("click", stopWireTest);
    }

    const historyBtn = document.getElementById("calibHistoryBtn");
    if (historyBtn) {
      historyBtn.addEventListener("click", toggleCalibrationHistory);
    }

    const topBtn = document.getElementById("topCalibBtn");
    if (topBtn) {
      topBtn.addEventListener("click", openCalibrationModal);
    }

    const historyLoadBtn = document.getElementById("calibHistoryLoadBtn");
    if (historyLoadBtn) {
      historyLoadBtn.addEventListener("click", loadSavedCalibration);
    }

    const historyRefreshBtn = document.getElementById("calibHistoryRefreshBtn");
    if (historyRefreshBtn) {
      historyRefreshBtn.addEventListener("click", () =>
        refreshCalibHistoryList(true)
      );
    }

    const clearBtn = document.getElementById("calibClearBtn");
    if (clearBtn) {
      clearBtn.addEventListener("click", clearCalibrationData);
    }

    const ntcStart = document.getElementById("ntcCalStartBtn");
    if (ntcStart) {
      ntcStart.addEventListener("click", startNtcCalibration);
    }
    const ntcStop = document.getElementById("ntcCalStopBtn");
    if (ntcStop) {
      ntcStop.addEventListener("click", stopNtcCalibration);
    }
    const ntcBetaBtn = document.getElementById("ntcBetaCalBtn");
    if (ntcBetaBtn) {
      ntcBetaBtn.addEventListener("click", startNtcBetaCalibration);
    }
    const floorBtn = document.getElementById("startFloorCalibBtn");
    if (floorBtn) {
      floorBtn.addEventListener("click", startFloorCalibration);
    }
    const presenceBtn = document.getElementById("presenceProbeBtn");
    if (presenceBtn) {
      presenceBtn.addEventListener("click", runPresenceProbe);
    }
  }

  function bindSetupWizardControls() {
    const updateBtn = document.getElementById("setupUpdateBtn");
    if (updateBtn) {
      updateBtn.addEventListener("click", () => {
        const payload = {};
        const stage = getInt("setupStageInput");
        const substage = getInt("setupSubstageInput");
        const wireIndex = getInt("setupWireIndexInput");
        if (stage !== undefined) payload.stage = stage;
        if (substage !== undefined) payload.substage = substage;
        if (wireIndex !== undefined) payload.wire_index = wireIndex;
        if (!Object.keys(payload).length) {
          openAlert("Setup Wizard", "Enter a stage or wire index.", "warning");
          return;
        }
        updateSetupStatus(payload);
      });
    }

    const doneBtn = document.getElementById("setupDoneBtn");
    if (doneBtn) {
      doneBtn.addEventListener("click", async () => {
        const status = lastSetupStatus || (await fetchSetupStatus());
        if (!status || !status.configOk) {
          openAlert(
            "Setup Wizard",
            "Configuration is incomplete. Please finish required steps.",
            "warning"
          );
          return;
        }
        if (!status.calibOk) {
          openAlert(
            "Setup Wizard",
            "Calibration is incomplete. Finish required calibration steps first.",
            "warning"
          );
          return;
        }
        updateSetupStatus({ setup_done: true });
      });
    }

    const clearBtn = document.getElementById("setupClearDoneBtn");
    if (clearBtn) {
      clearBtn.addEventListener("click", () =>
        updateSetupStatus({ setup_done: false })
      );
    }

    const refreshBtn = document.getElementById("setupRefreshBtn");
    if (refreshBtn) {
      refreshBtn.addEventListener("click", fetchSetupStatus);
    }

    const resetBtn = document.getElementById("setupResetBtn");
    if (resetBtn) {
      resetBtn.addEventListener("click", () =>
        openConfirm("setupReset", resetSetupWizard)
      );
    }

    const bannerOpen = document.getElementById("setupBannerOpenBtn");
    if (bannerOpen) {
      bannerOpen.addEventListener("click", () => {
        if (
          lastSetupStatus &&
          (!lastSetupStatus.setupDone || !lastSetupStatus.configOk)
        ) {
          syncWizardStepFromStatus(lastSetupStatus);
          openSetupWizard();
          return;
        }
        switchTab(3);
        const card = document.querySelector(".setup-wizard-card");
        if (card && card.scrollIntoView) {
          card.scrollIntoView({ block: "center", behavior: "smooth" });
        }
      });
    }

    const bannerCalib = document.getElementById("setupBannerCalibBtn");
    if (bannerCalib) {
      bannerCalib.addEventListener("click", openCalibrationModal);
    }
  }

  document.addEventListener("keydown", (e) => {
    if (e.key === "Escape") {
      setCalibrationInfoVisible(false);
      closeErrorModal();
    }
  });

  function scrollCalibToLatest() {
    const wrap = document.getElementById("calibScrollWrap");
    if (!wrap) return;
    wrap.scrollLeft = wrap.scrollWidth;
  }

  function toggleCalibPause() {
    calibChartPaused = !calibChartPaused;
    const pauseBtn = document.getElementById("calibPauseBtn");
    if (pauseBtn) pauseBtn.textContent = calibChartPaused ? "Resume" : "Pause";
    if (!calibChartPaused) {
      // Catch up immediately after resuming.
      pollCalibrationOnce();
    }
  }

  function setCalibrationInfoVisible(show) {
    const pop = document.getElementById("calibrationInfoPopover");
    if (!pop) return;
    if (show) pop.classList.add("show");
    else pop.classList.remove("show");
    pop.setAttribute("aria-hidden", show ? "false" : "true");
  }

  function toggleCalibrationInfo() {
    const pop = document.getElementById("calibrationInfoPopover");
    if (!pop) return;
    const show = !pop.classList.contains("show");
    setCalibrationInfoVisible(show);
  }

  
  
  
  
  function avgLastTemps(temps, count) {
    let sum = 0;
    let used = 0;
    for (let i = temps.length - 1; i >= 0 && used < count; i--) {
      const t = temps[i];
      if (Number.isFinite(t)) {
        sum += t;
        used++;
      }
    }
    return used ? sum / used : NaN;
  }

  function computeModelFromSamples(samples, fallback) {
    if (!Array.isArray(samples) || samples.length < 5) return null;

    const tms = [];
    const temps = [];
    const powers = [];
    let maxPower = 0;

    for (const s of samples) {
      const t = Number(s.t_ms);
      const temp = Number(s.temp_c);
      const v = Number(s.v);
      const i = Number(s.i);
      tms.push(Number.isFinite(t) ? t : NaN);
      temps.push(Number.isFinite(temp) ? temp : NaN);
      let p = NaN;
      if (Number.isFinite(v) && Number.isFinite(i)) {
        p = v * i;
        if (Number.isFinite(p) && p > maxPower) maxPower = p;
      }
      powers.push(p);
    }

    if (!Number.isFinite(maxPower) || maxPower <= 0) {
      maxPower = Number.isFinite(fallback.max_power_w)
        ? fallback.max_power_w
        : 0;
    }
    const threshold = Math.max(5, maxPower * 0.2);

    let start = 0;
    while (
      start < powers.length &&
      !(Number.isFinite(powers[start]) && powers[start] > threshold)
    ) {
      start++;
    }
    if (start >= powers.length) start = 0;

    let end = -1;
    let lowCount = 0;
    for (let i = start + 1; i < powers.length; i++) {
      if (!Number.isFinite(powers[i]) || powers[i] < threshold) {
        lowCount++;
        if (lowCount >= 3) {
          end = i - 2;
          break;
        }
      } else {
        lowCount = 0;
      }
    }

    let peakIndex = start;
    let peakTemp = -Infinity;
    for (let i = start; i < temps.length; i++) {
      const temp = temps[i];
      if (Number.isFinite(temp) && temp > peakTemp) {
        peakTemp = temp;
        peakIndex = i;
      }
      if (end >= start && i >= end) break;
    }
    if (!Number.isFinite(peakTemp)) return null;
    if (end < start || end > peakIndex) end = peakIndex;

    const ambient = avgLastTemps(temps, 10);
    if (!Number.isFinite(ambient)) return null;

    const deltaT = peakTemp - ambient;
    if (!Number.isFinite(deltaT) || deltaT <= 1.0) return null;

    const tStartMs = Number.isFinite(tms[start]) ? tms[start] : 0;
    const tPeakMs = Number.isFinite(tms[peakIndex]) ? tms[peakIndex] : tStartMs;

    const t63 = ambient + 0.632 * deltaT;
    let t63Ms = NaN;
    for (let i = start; i <= peakIndex; i++) {
      if (
        Number.isFinite(temps[i]) &&
        temps[i] >= t63 &&
        Number.isFinite(tms[i])
      ) {
        t63Ms = tms[i];
        break;
      }
    }

    let tauSec = NaN;
    if (Number.isFinite(t63Ms) && t63Ms > tStartMs) {
      tauSec = (t63Ms - tStartMs) / 1000;
    } else if (tPeakMs > tStartMs) {
      tauSec = (tPeakMs - tStartMs) / 3000;
    }

    let pSum = 0;
    let pCount = 0;
    for (let i = start; i <= peakIndex; i++) {
      const p = powers[i];
      if (Number.isFinite(p) && p > 0) {
        pSum += p;
        pCount++;
      }
    }
    let pAvg = pCount ? pSum / pCount : NaN;
    if (!Number.isFinite(pAvg) || pAvg <= 0) pAvg = maxPower;

    let kLoss = pAvg / deltaT;
    if (!Number.isFinite(kLoss) || kLoss <= 0) kLoss = NaN;

    let thermalC =
      Number.isFinite(kLoss) && Number.isFinite(tauSec) ? tauSec * kLoss : NaN;

    return {
      wire_tau: Number.isFinite(tauSec) ? tauSec : fallback.wire_tau,
      wire_k_loss: Number.isFinite(kLoss) ? kLoss : fallback.wire_k_loss,
      wire_c: Number.isFinite(thermalC) ? thermalC : fallback.wire_c,
      max_power_w: Number.isFinite(maxPower) ? maxPower : fallback.max_power_w,
    };
  }

  
