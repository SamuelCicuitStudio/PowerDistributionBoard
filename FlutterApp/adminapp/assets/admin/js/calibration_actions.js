  async function startCalibration(mode) {
    if (
      guardUnsafeAction("starting calibration", {
        blockAuto: true,
        blockCalib: true,
      })
    ) {
      return;
    }
    const payload = {
      mode,
      interval_ms: 500,
      max_samples: 1200,
      epoch: Math.floor(Date.now() / 1000),
    };
    const targetC = getFloat("wireTestTargetC");
    const maxC = getFloat("nichromeFinalTempC");
    if (mode === "model" && Number.isFinite(maxC) && maxC > 0) {
      payload.target_c = maxC;
    } else if (Number.isFinite(targetC) && targetC > 0) {
      payload.target_c = targetC;
    }

    setCalibText("calibStatusText", "Starting");
    const modeLabel =
      mode === "model" ? "Model" : mode === "floor" ? "Floor" : "--";
    setCalibText("calibModeText", modeLabel);
    setCalibText("calibCountText", "0");
    setCalibText("calibIntervalText", `${payload.interval_ms} ms`);
    setCalibText(
      "calibTargetText",
      Number.isFinite(payload.target_c)
        ? `${payload.target_c.toFixed(1)} C`
        : "--"
    );
    setCalibText("calibTempText", "--");
    setCalibText(
      "calibWireText",
      payload.wire_index ? `#${payload.wire_index}` : "--"
    );
    setCalibText("calibElapsedText", "--");

    try {
      const res = await fetch("/calib_start", {
        method: "POST",
        headers: cborHeaders(),
        body: encodeCbor(payload),
      });
      const data = await readCbor(res, {});
      if (!res.ok || (data && data.error)) {
        const err = (data && data.error) || `HTTP ${res.status}`;
        const extras = [];
        if (data && data.detail) extras.push(data.detail);
        if (data && data.state) extras.push(`state=${data.state}`);
        const msg = extras.length ? `${err} (${extras.join(", ")})` : err;
        console.error("Calibration start failed:", msg);
        openAlert("Calibration", msg, "danger");
        return;
      }
      calibViewMode = "live";
      const historyBtn = document.getElementById("calibHistoryBtn");
      if (historyBtn) historyBtn.textContent = "Load History";
      await pollCalibrationOnce();
    } catch (err) {
      console.error("Calibration start error:", err);
    }
  }

  async function stopCalibration() {
    try {
      const epoch = Math.floor(Date.now() / 1000);
      const res = await fetch("/calib_stop", {
        method: "POST",
        headers: cborHeaders(),
        body: encodeCbor({ epoch }),
      });
      if (!res.ok) {
        console.error("Calibration stop failed:", res.status);
      }
      await pollCalibrationOnce();
      refreshCalibHistoryList(true);
      fetchSetupStatus();
    } catch (err) {
      console.error("Calibration stop error:", err);
    }
  }

  async function startNtcCalibration() {
    if (
      guardUnsafeAction("starting NTC calibration", {
        blockAuto: true,
        blockCalib: true,
      })
    ) {
      return;
    }
    const payload = {};
    const targetC = getFloat("ntcCalTargetC");
    const sampleMs = getInt("ntcCalSampleMs");
    const timeoutMs = getInt("ntcCalTimeoutMs");
    if (Number.isFinite(targetC)) payload.target_c = targetC;
    if (Number.isFinite(sampleMs)) payload.sample_ms = sampleMs;
    if (Number.isFinite(timeoutMs)) payload.timeout_ms = timeoutMs;

    try {
      const res = await fetch("/ntc_calibrate", {
        method: "POST",
        headers: cborHeaders(),
        body: encodeCbor(payload),
      });
      const data = await readCbor(res, {});
      if (!res.ok || (data && data.error)) {
        const msg = (data && data.error) || `HTTP ${res.status}`;
        openAlert("NTC Calibration", msg, "danger");
        return;
      }
      openAlert("NTC Calibration", "Started.", "success");
      await pollNtcCalOnce();
    } catch (err) {
      console.error("NTC calibration start error:", err);
      openAlert("NTC Calibration", "Start failed.", "danger");
    }
  }

  async function stopNtcCalibration() {
    try {
      await fetch("/ntc_cal_stop", {
        method: "POST",
        headers: cborHeaders(),
        body: encodeCbor({}),
      });
      await pollNtcCalOnce();
      fetchSetupStatus();
    } catch (err) {
      console.error("NTC calibration stop error:", err);
    }
  }

  async function startNtcBetaCalibration() {
    if (
      guardUnsafeAction("starting beta calibration", {
        blockAuto: true,
        blockCalib: true,
      })
    ) {
      return;
    }
    const payload = {};
    const refC = getFloat("ntcBetaRefC");
    if (Number.isFinite(refC)) payload.ref_temp_c = refC;
    try {
      const res = await fetch("/ntc_beta_calibrate", {
        method: "POST",
        headers: cborHeaders(),
        body: encodeCbor(payload),
      });
      const data = await readCbor(res, {});
      if (!res.ok || (data && data.error)) {
        const msg = (data && data.error) || `HTTP ${res.status}`;
        openAlert("NTC Beta Calibration", msg, "danger");
        return;
      }
      openAlert("NTC Beta Calibration", "Applied.", "success");
      await pollNtcCalOnce();
    } catch (err) {
      console.error("NTC beta calibration error:", err);
      openAlert("NTC Beta Calibration", "Request failed.", "danger");
    }
  }

  async function startFloorCalibration() {
    if (
      guardUnsafeAction("starting floor calibration", {
        blockAuto: true,
        blockCalib: true,
      })
    ) {
      return;
    }
    const payload = {
      mode: "floor",
      epoch: Math.floor(Date.now() / 1000),
    };

    const targetC = getFloat("floorCalTargetC");
    const dutyPct = getFloat("floorCalDutyPct");
    const ambientMin = getFloat("floorCalAmbientMin");
    const heatMin = getFloat("floorCalHeatMin");
    const coolMin = getFloat("floorCalCoolMin");
    const intervalMs = getInt("floorCalIntervalMs");
    const wireIndex = getInt("floorCalWireIndex");

    if (Number.isFinite(targetC)) payload.target_c = targetC;
    if (Number.isFinite(dutyPct)) payload.duty_pct = dutyPct;
    if (Number.isFinite(intervalMs)) payload.interval_ms = intervalMs;
    if (Number.isFinite(wireIndex)) payload.wire_index = wireIndex;

    if (Number.isFinite(ambientMin)) payload.ambient_ms = ambientMin * 60000;
    if (Number.isFinite(heatMin)) payload.heat_ms = heatMin * 60000;
    if (Number.isFinite(coolMin)) payload.cool_ms = coolMin * 60000;

    setCalibText("calibStatusText", "Starting");
    setCalibText("calibModeText", "Floor");
    setCalibText("calibCountText", "0");
    setCalibText(
      "calibIntervalText",
      Number.isFinite(payload.interval_ms) ? `${payload.interval_ms} ms` : "--"
    );
    setCalibText(
      "calibTargetText",
      Number.isFinite(payload.target_c)
        ? `${payload.target_c.toFixed(1)} C`
        : "--"
    );
    setCalibText("calibTempText", "--");
    setCalibText(
      "calibWireText",
      payload.wire_index ? `#${payload.wire_index}` : "--"
    );
    setCalibText("calibElapsedText", "--");

    try {
      const res = await fetch("/calib_start", {
        method: "POST",
        headers: cborHeaders(),
        body: encodeCbor(payload),
      });
      const data = await readCbor(res, {});
      if (!res.ok || (data && data.error)) {
        const err = (data && data.error) || `HTTP ${res.status}`;
        const extras = [];
        if (data && data.detail) extras.push(data.detail);
        if (data && data.state) extras.push(`state=${data.state}`);
        const msg = extras.length ? `${err} (${extras.join(", ")})` : err;
        openAlert("Floor Calibration", msg, "danger");
        return;
      }
      calibViewMode = "live";
      const historyBtn = document.getElementById("calibHistoryBtn");
      if (historyBtn) historyBtn.textContent = "Load History";
      await pollCalibrationOnce();
    } catch (err) {
      console.error("Floor calibration start error:", err);
    }
  }

  async function runPresenceProbe() {
    if (
      guardUnsafeAction("starting presence probe", {
        blockAuto: true,
        blockCalib: true,
      })
    ) {
      return;
    }
    const statusEl = document.getElementById("presenceProbeStatusText");
    const presenceBtn = document.getElementById("presenceProbeBtn");
    const inWizardPresence =
      wizardActive &&
      WIZARD_STEPS[wizardStepIndex] &&
      WIZARD_STEPS[wizardStepIndex].id === "wizardStepPresence";
    if (presenceBtn) presenceBtn.disabled = true;
    try {
      if (inWizardPresence) {
        if (statusEl) statusEl.textContent = "Charging (2s)...";
        await sleep(2000);
      }
      if (statusEl) statusEl.textContent = "Running...";
      const res = await fetch("/presence_probe", {
        method: "POST",
        headers: cborHeaders(),
        body: encodeCbor({}),
      });
      const data = await readCbor(res, {});
      if (!res.ok || (data && data.error)) {
        const msg = (data && data.error) || `HTTP ${res.status}`;
        if (statusEl) statusEl.textContent = "Failed";
        openAlert("Presence Probe", msg, "danger");
        return;
      }
      if (statusEl) statusEl.textContent = "Done";
      renderPresenceProbeResults(data && data.wirePresent);
      await fetchSetupStatus();
    } catch (err) {
      console.error("Presence probe error:", err);
      if (statusEl) statusEl.textContent = "Failed";
    } finally {
      if (presenceBtn) presenceBtn.disabled = false;
    }
  }

  function renderPresenceProbeResults(presentList) {
    const grid = document.getElementById("presenceProbeGrid");
    if (!grid) return;
    grid.innerHTML = "";
    const present = Array.isArray(presentList) ? presentList : [];
    const access =
      (lastLoadedControls && lastLoadedControls.outputAccess) || null;
    let rendered = false;
    for (let i = 1; i <= 10; i++) {
      if (access && access["output" + i] === false) {
        continue;
      }
      const chip = document.createElement("div");
      const isPresent = !!present[i - 1];
      chip.className = "presence-probe-chip";
      chip.classList.add(isPresent ? "is-present" : "is-missing");
      chip.textContent = `Wire ${i}: ${isPresent ? "OK" : "Missing"}`;
      grid.appendChild(chip);
      rendered = true;
    }
    if (!rendered) {
      const note = document.createElement("div");
      note.className = "presence-probe-chip is-missing";
      note.textContent = "No outputs enabled.";
      grid.appendChild(note);
    }
  }

  async function pollNtcCalOnce() {
    try {
      const res = await fetch("/ntc_cal_status", {
        cache: "no-store",
        headers: cborHeaders(),
      });
      if (!res.ok) return;
      const data = await readCbor(res, {});
      updateNtcCalUi(data);
    } catch (err) {
      console.warn("NTC calibration status failed:", err);
    }
  }

  function startNtcCalPoll() {
    stopNtcCalPoll();
    pollNtcCalOnce();
    ntcCalPollTimer = setInterval(pollNtcCalOnce, ntcCalPollIntervalMs);
  }

  function stopNtcCalPoll() {
    if (ntcCalPollTimer) {
      clearInterval(ntcCalPollTimer);
      ntcCalPollTimer = null;
    }
  }

  function updateNtcCalUi(data) {
    if (!data) return;
    let state = "Idle";
    if (data.running) state = "Running";
    else if (data.done) state = "Done";
    else if (data.error) state = "Error";
    setText("ntcCalStateText", state);
    if (data.error) {
      const err = data.error || "error";
      setText("ntcCalStateText", `Error (${err})`);
    }
    setText(
      "ntcCalTargetText",
      Number.isFinite(data.target_c) ? `${data.target_c.toFixed(1)} C` : "--"
    );
    setText(
      "ntcCalHeatsinkText",
      Number.isFinite(data.heatsink_c)
        ? `${data.heatsink_c.toFixed(1)} C`
        : "--"
    );
    setText(
      "ntcCalOhmText",
      Number.isFinite(data.ntc_ohm) ? `${data.ntc_ohm.toFixed(1)} ohm` : "--"
    );
    setText(
      "ntcCalSamplesText",
      Number.isFinite(data.samples) ? String(data.samples) : "0"
    );
    setText(
      "ntcCalElapsedText",
      formatElapsedMs(Number(data.elapsed_ms) || 0)
    );
    setText(
      "ntcCalShAText",
      Number.isFinite(data.sh_a) ? data.sh_a.toExponential(3) : "--"
    );
    setText(
      "ntcCalShBText",
      Number.isFinite(data.sh_b) ? data.sh_b.toExponential(3) : "--"
    );
    setText(
      "ntcCalShCText",
      Number.isFinite(data.sh_c) ? data.sh_c.toExponential(3) : "--"
    );
    setText(
      "ntcCalWireText",
      Number.isFinite(data.wire_index) ? `#${data.wire_index}` : "--"
    );
  }

  
