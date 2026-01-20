  async function startWireTest() {
    if (
      guardUnsafeAction("starting the wire test", {
        blockAuto: true,
        blockCalib: true,
      })
    ) {
      return;
    }
    const target = parseFloat(
      (document.getElementById("wireTestTargetC") || {}).value
    );

    if (!isFinite(target) || target <= 0) {
      openAlert("Wire Test", "Enter a valid target temperature.", "warning");
      return;
    }

    const payload = { target_c: target };

    try {
      const res = await fetch("/wire_test_start", {
        method: "POST",
        headers: cborHeaders(),
        body: encodeCbor(payload),
      });
      if (!res.ok) {
        openAlert("Wire Test", "Start failed.", "danger");
      } else {
        openAlert("Wire Test", "Started.", "success");
      }
      await pollWireTestOnce();
    } catch (err) {
      console.error("Wire test start error:", err);
    }
  }

  async function stopWireTest() {
    try {
      await fetch("/wire_test_stop", {
        method: "POST",
        headers: cborHeaders(),
        body: encodeCbor({}),
      });
      await pollWireTestOnce();
    } catch (err) {
      console.error("Wire test stop error:", err);
    }
  }

  async function clearCalibrationData() {
    try {
      const res = await fetch("/calib_clear", {
        method: "POST",
        headers: cborHeaders(),
        body: encodeCbor({}),
      });
      if (!res.ok) {
        console.error("Calibration clear failed:", res.status);
      }
      calibSamples = [];
      calibViewMode = "live";
      const historyBtn = document.getElementById("calibHistoryBtn");
      if (historyBtn) historyBtn.textContent = "Load History";
      renderCalibrationChart();
      await pollCalibrationOnce();
      refreshCalibHistoryList(true);
    } catch (err) {
      console.error("Calibration clear error:", err);
    }
  }

  async function pollWireTestOnce() {
    try {
      const res = await fetch("/wire_test_status", {
        cache: "no-store",
        headers: cborHeaders(),
      });
      if (!res.ok) return;
      const data = await readCbor(res, {});

      const running = !!data.running;
      const stateEl = document.getElementById("wireTestState");
      if (stateEl) stateEl.textContent = running ? "Running" : "Idle";

      const modeEl = document.getElementById("wireTestMode");
      if (modeEl) {
        modeEl.textContent = data.mode || "--";
      }

      const purposeEl = document.getElementById("wireTestPurpose");
      if (purposeEl) {
        const p = data.purpose || "none";
        const label =
          p === "wire_test"
            ? "Wire Test"
            : p === "model_cal"
            ? "Model Cal"
            : "--";
        purposeEl.textContent = label;
      }

      const activeEl = document.getElementById("wireTestActiveWire");
      if (activeEl) {
        activeEl.textContent = data.active_wire ? `#${data.active_wire}` : "--";
      }

      const ntcEl = document.getElementById("wireTestNtcTemp");
      if (ntcEl) {
        ntcEl.textContent = Number.isFinite(data.ntc_temp_c)
          ? `${data.ntc_temp_c.toFixed(1)} C`
          : "--";
      }

      const modelEl = document.getElementById("wireTestModelTemp");
      if (modelEl) {
        modelEl.textContent = Number.isFinite(data.active_temp_c)
          ? `${data.active_temp_c.toFixed(1)} C`
          : "--";
      }

      const packetEl = document.getElementById("wireTestPacket");
      if (packetEl) {
        packetEl.textContent = Number.isFinite(data.packet_ms)
          ? `${Math.round(data.packet_ms)} ms`
          : "--";
      }

      const frameEl = document.getElementById("wireTestFrame");
      if (frameEl) {
        frameEl.textContent = Number.isFinite(data.frame_ms)
          ? `${Math.round(data.frame_ms)} ms`
          : "--";
      }

      const tgtInput = document.getElementById("wireTestTargetC");
      if (tgtInput && isFinite(data.target_c)) {
        tgtInput.value = data.target_c;
      }

      const startBtn = document.getElementById("wireTestStartBtn");
      const stopBtn = document.getElementById("wireTestStopBtn");
      if (startBtn) startBtn.disabled = running;
      if (stopBtn)
        stopBtn.disabled = !(running && data.purpose === "wire_test");

      updateTestModeFromWireStatus(data);
    } catch (err) {
      console.warn("Wire test status error:", err);
    }
  }

  function updateTestModeFromWireStatus(data) {
    if (!data || typeof data !== "object") {
      testModeState = { active: false, label: "--", targetC: NaN };
      lastWireTestStatus = null;
      updateTestModeBadge();
      return;
    }

    const running = !!data.running;
    const purpose = data.purpose || "none";
    const label =
      purpose === "wire_test"
        ? "Wire Test"
        : purpose === "model_cal"
        ? "Model Cal"
        : purpose === "ntc_cal"
        ? "NTC Cal"
        : purpose === "floor_cal"
        ? "Floor Cal"
        : "--";

    const active = running && purpose !== "none";
    const target = Number(data.target_c);

    testModeState.active = active;
    testModeState.label = active ? label : "--";
    testModeState.targetC = Number.isFinite(target) ? target : NaN;
    lastWireTestStatus = data;

    updateTestModeBadge();
  }

  function updateTestModeBadge() {
    const badge = document.getElementById("testModeBadge");
    if (!badge) return;

    const modeEl = badge.querySelector ? badge.querySelector(".tm-mode") : null;

    if (testModeState.active) {
      badge.classList.add("active");
      badge.title = `Test mode active: ${testModeState.label}`;
      if (modeEl) modeEl.textContent = testModeState.label;
    } else {
      badge.classList.remove("active");
      badge.title = "Test mode active";
      if (modeEl) modeEl.textContent = "--";
    }
    updateStatusBarState();
  }

  function updateAmbientWaitFromMonitor(wait) {
    if (!wait || typeof wait !== "object") {
      ambientWaitState = { active: false, label: "--", tolC: NaN, sinceMs: 0 };
      updateAmbientWaitBadge();
      return;
    }

    const active = !!wait.active;
    const reason = String(wait.reason || "");
    const label =
      reason === "model_cal"
        ? "Model Cal"
        : reason === "ntc_cal"
        ? "NTC Cal"
        : reason === "floor_cal"
        ? "Floor Cal"
        : "Run";

    ambientWaitState.active = active;
    ambientWaitState.label = active ? label : "--";
    const tol = Number(wait.tol_c);
    ambientWaitState.tolC = Number.isFinite(tol) ? tol : NaN;
    ambientWaitState.sinceMs = Number(wait.since_ms) || 0;

    updateAmbientWaitBadge();
  }

  function updateAmbientWaitBadge() {
    const badge = document.getElementById("ambientWaitBadge");
    if (!badge) return;

    const modeEl = badge.querySelector ? badge.querySelector(".cw-mode") : null;

    if (ambientWaitState.active) {
      badge.classList.add("active");
      const tolText = Number.isFinite(ambientWaitState.tolC)
        ? `+/-${ambientWaitState.tolC.toFixed(1)} C`
        : "";
      const label =
        ambientWaitState.label && ambientWaitState.label !== "--"
          ? ` (${ambientWaitState.label})`
          : "";
      badge.title = `Cooling to ambient${label}${tolText ? " " + tolText : ""}`;
      if (modeEl) modeEl.textContent = ambientWaitState.label;
    } else {
      badge.classList.remove("active");
      badge.title = "Cooling to ambient";
      if (modeEl) modeEl.textContent = "--";
    }

    updateStatusBarState();
  }

  function updateStatusBarState() {
    const bar = document.querySelector(".user-status-global");
    if (bar) {
      bar.classList.toggle("testmode-active", testModeState.active);
      bar.classList.toggle("cooldown-active", ambientWaitState.active);
      bar.classList.toggle("run-active", lastState === "Running");
      const wireActive = !!(lastWireTestStatus && lastWireTestStatus.running);
      const calibRunning = !!(calibLastMeta && calibLastMeta.running);
      const calibActive = wireActive || calibRunning;
      bar.classList.toggle("calib-active", calibActive);
    }

    const stopBtn = document.getElementById("topStopTestBtn");
    if (stopBtn) {
      const calibRunning = !!(calibLastMeta && calibLastMeta.running);
      const canStop = testModeState.active || calibRunning;
      const label = testModeState.active
        ? testModeState.label
        : calibRunning
        ? "Calibration"
        : "";
      const title = label ? `Stop ${label}` : "Stop the active test";
      stopBtn.title = title;
      stopBtn.setAttribute("aria-label", title);
      const textEl = stopBtn.querySelector
        ? stopBtn.querySelector(".status-action-text")
        : null;
      if (textEl) textEl.textContent = "Stop";
      stopBtn.disabled = !canStop;
    }

    applySafetyLocks();
  }

  async function stopActiveTestMode() {
    const stopBtn = document.getElementById("topStopTestBtn");
    if (stopBtn) stopBtn.disabled = true;

    const purpose =
      (lastWireTestStatus && lastWireTestStatus.purpose) || "none";
    try {
      if (purpose === "wire_test") {
        await stopWireTest();
      } else if (purpose === "model_cal") {
        await stopCalibration();
        await stopWireTest();
      } else if (purpose === "floor_cal") {
        await stopCalibration();
        await stopWireTest();
      } else if (purpose === "ntc_cal") {
        await stopNtcCalibration();
        await stopWireTest();
      } else if (calibLastMeta && calibLastMeta.running) {
        await stopCalibration();
      }
    } finally {
      if (stopBtn) stopBtn.disabled = false;
    }
  }

  function startWireTestPoll() {
    stopWireTestPoll();
    pollWireTestOnce();
    wireTestPollTimer = setInterval(pollWireTestOnce, 1000);
  }

  function stopWireTestPoll() {
    if (wireTestPollTimer) {
      clearInterval(wireTestPollTimer);
      wireTestPollTimer = null;
    }
  }

