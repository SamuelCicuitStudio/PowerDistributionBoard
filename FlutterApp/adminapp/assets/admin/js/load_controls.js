  // ========================================================
  // ===============        LOAD CONTROLS        ============
  // ========================================================

  async function loadControls() {
    try {
      const res = await fetch("/load_controls", {
        cache: "no-store",
        headers: cborHeaders(),
      });
      if (!res.ok) throw new Error("HTTP " + res.status);
      const data = await readCbor(res, {});

      console.log("Fetched /load_controls:", data);

      lastLoadedControls = data;

      // LT toggle
      const ltToggle = document.getElementById("ltToggle");
      if (ltToggle) ltToggle.checked = !!data.ledFeedback;

      updateModePills();

      // Buzzer mute initial sync (accept bool, 0/1, "true"/"false")
      if (data.buzzerMute !== undefined) {
        const muted =
          data.buzzerMute === true ||
          data.buzzerMute === 1 ||
          data.buzzerMute === "1" ||
          data.buzzerMute === "true";
        isMuted = muted;
        setMuteUI(isMuted);
        console.log(
          "[Mute] State from /load_controls:",
          data.buzzerMute,
          "=> isMuted =",
          isMuted
        );
      }

      // Ready / Off LEDs
      const readyLed = document.getElementById("readyLed");
      const offLed = document.getElementById("offLed");
      if (readyLed)
        readyLed.style.backgroundColor = data.ready ? "limegreen" : "gray";
      if (offLed) offLed.style.backgroundColor = data.off ? "red" : "gray";

      // Device numeric fields
      setField("acFrequency", data.acFrequency);
      setField("chargeResistor", data.chargeResistor);
      setField("currLimit", data.currLimit);
      setField("tempWarnC", data.tempWarnC);
      setField("tempTripC", data.tempTripC);
      if (data.deviceId !== undefined) setField("userDeviceId", data.deviceId);
      // Always clear credential fields on load to avoid autofill showing deviceId.
      setField("userCurrentPassword", "");
      setField("userNewPassword", "");
      const fanSlider = document.getElementById("fanSlider");
      if (fanSlider && typeof data.fanSpeed === "number") {
        fanSlider.value = data.fanSpeed;
        setFanSpeedValue(data.fanSpeed);
      }
      if (data.wireGauge !== undefined) {
        setField("wireGauge", data.wireGauge);
      }
      if (data.wireOhmPerM !== undefined) {
        setField("wireOhmPerM", data.wireOhmPerM);
      }
      if (data.floorThicknessMm !== undefined) {
        setField("floorThicknessMm", data.floorThicknessMm);
      }
      if (data.floorMaterial !== undefined) {
        setField("floorMaterial", data.floorMaterial);
      } else if (data.floorMaterialCode !== undefined) {
        setField(
          "floorMaterial",
          floorMaterialFromCode(data.floorMaterialCode)
        );
      } else {
        setField("floorMaterial", "wood");
      }
      if (data.floorMaxC !== undefined) {
        setField("floorMaxC", data.floorMaxC);
      }
      if (data.floorSwitchMarginC !== undefined) {
        setField("floorSwitchMarginC", data.floorSwitchMarginC);
      }
      if (data.currentSource !== undefined) {
        setField("currentSource", data.currentSource);
      }
    if (data.nichromeFinalTempC !== undefined) {
      setField("nichromeFinalTempC", data.nichromeFinalTempC);
      const maxC = Number(data.nichromeFinalTempC);
      const tgt = document.getElementById("wireTestTargetC");
      if (tgt && Number.isFinite(maxC) && maxC > 0) {
        tgt.max = String(maxC);
        const cur = parseFloat(tgt.value);
        if (!Number.isFinite(cur) || cur > maxC) {
          tgt.value = maxC.toFixed(1);
        }
      }
    }
    if (data.ntcGateIndex !== undefined) {
      setField("ntcGateIndex", data.ntcGateIndex);
    }
      if (data.ntcBeta !== undefined) {
        setField("ntcBeta", data.ntcBeta);
      }
      if (data.ntcT0C !== undefined) {
        setField("ntcT0C", data.ntcT0C);
      }
      if (data.ntcR0 !== undefined) {
        setField("ntcR0", data.ntcR0);
      }
      if (data.ntcFixedRes !== undefined) {
        setField("ntcFixedRes", data.ntcFixedRes);
      }
      if (data.presenceMinDropV !== undefined) {
        setField("presenceMinDropV", data.presenceMinDropV);
      }
      if (data.ntcCalTargetC !== undefined) {
        setField("ntcCalTargetC", data.ntcCalTargetC);
      }
      if (data.ntcCalSampleMs !== undefined) {
        setField("ntcCalSampleMs", data.ntcCalSampleMs);
      }
      if (data.ntcCalTimeoutMs !== undefined) {
        setField("ntcCalTimeoutMs", data.ntcCalTimeoutMs);
      }
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
      const floorTarget = document.getElementById("floorCalTargetC");
      if (floorTarget && !floorTarget.value && data.floorMaxC !== undefined) {
        floorTarget.value = data.floorMaxC;
      }
      const floorWire = document.getElementById("floorCalWireIndex");
      if (floorWire && !floorWire.value && data.ntcGateIndex !== undefined) {
        floorWire.value = data.ntcGateIndex;
      }
      if (data.capacitanceF !== undefined) {
        renderCapacitance(parseFloat(data.capacitanceF));
      }

      // Nichrome R01..R10 from wireRes{1..10}
      const wireRes = data.wireRes || {};
      for (let i = 1; i <= 10; i++) {
        const v = wireRes[String(i)];
        setField("r" + String(i).padStart(2, "0") + "ohm", v);
      }
      updateModelParamFields(data);

      // Output states
      const states = data.outputs || {};
      for (let i = 1; i <= 10; i++) {
        const checked = !!states["output" + i];
        const itemSel = "#manualOutputs .manual-item:nth-child(" + i + ")";
        const checkbox = document.querySelector(
          itemSel + ' input[type="checkbox"]'
        );
        const led = document.querySelector(itemSel + " .led");
        if (checkbox) checkbox.checked = checked;
        if (led) led.classList.toggle("active", checked);
      }

      // Output Access flags
      const access = data.outputAccess || {};
      for (let i = 1; i <= 10; i++) {
        const checkbox = document.querySelector(
          "#userAccessGrid .manual-item:nth-child(" +
            i +
            ') input[type="checkbox"]'
        );
        if (checkbox) checkbox.checked = !!access["output" + i];
      }

      // Relay mirror
      const relayFromServer = !!data.relay;
      const relayToggle = document.getElementById("relayToggle");
      if (relayToggle) relayToggle.checked = relayFromServer;
      if (typeof setDot === "function") setDot("relay", relayFromServer);

      // Apply power button look from LEDs if desired
      applyReadyOffFlagsToPower(data.ready, data.off);

      setField("wifiSSID", "");
      setField("wifiPassword", "");
      setField("apSSID", "");
      setField("apPassword", "");

      if (data.setupRunAllowed !== undefined) {
        const configOk =
          data.setupConfigOk !== undefined
            ? !!data.setupConfigOk
            : setupConfigOk;
        const calibOk =
          data.setupCalibOk !== undefined
            ? !!data.setupCalibOk
            : setupCalibOk;
        setupConfigOk = configOk;
        setupCalibOk = calibOk;
        setupRunAllowed = !!data.setupRunAllowed && calibOk;
        updateSetupBanner({
          setupDone: data.setupDone,
          configOk,
          calibOk,
          missingConfig: lastSetupStatus ? lastSetupStatus.missingConfig : [],
          missingCalib: lastSetupStatus ? lastSetupStatus.missingCalib : [],
        });
        setUserTabEnabled(!!data.setupDone);
      }

      if (document.getElementById("setupMissingConfigList")) {
        fetchSetupStatus();
      }

      renderLiveControlChart();
      applySafetyLocks();
    } catch (err) {
      console.error("Failed to load controls:", err);
    }
  }

  async function fetchSetupStatus() {
    try {
      const res = await fetch("/setup_status", {
        cache: "no-store",
        headers: cborHeaders(),
      });
      if (!res.ok) throw new Error("HTTP " + res.status);
      const data = await readCbor(res, {});
      updateSetupUiFromStatus(data);
      return data;
    } catch (err) {
      console.warn("Failed to load setup status:", err);
      return null;
    }
  }

  async function updateSetupStatus(payload) {
    try {
      const res = await fetch("/setup_update", {
        method: "POST",
        headers: cborHeaders(),
        body: encodeCbor(payload || {}),
      });
      const data = await readCbor(res, {});
      if (!res.ok || (data && data.error)) {
        const msg = (data && data.error) || `HTTP ${res.status}`;
        openAlert("Setup Wizard", msg, "danger");
        return;
      }
      await fetchSetupStatus();
    } catch (err) {
      console.error("Setup update failed:", err);
      openAlert("Setup Wizard", "Update failed.", "danger");
    }
  }

  async function resetSetupWizard() {
    const payload = {
      clear_models: !!getBool("setupResetClearModels"),
      clear_wire_params: !!getBool("setupResetClearWire"),
      clear_floor_params: !!getBool("setupResetClearFloor"),
    };
    try {
      const res = await fetch("/setup_reset", {
        method: "POST",
        headers: cborHeaders(),
        body: encodeCbor(payload),
      });
      const data = await readCbor(res, {});
      if (!res.ok || (data && data.error)) {
        const msg = (data && data.error) || `HTTP ${res.status}`;
        openAlert("Setup Wizard", msg, "danger");
        return;
      }
      await fetchSetupStatus();
      resetWizardSkipped();
    } catch (err) {
      console.error("Setup reset failed:", err);
      openAlert("Setup Wizard", "Reset failed.", "danger");
    }
  }

  async function loadDeviceInfo() {
    try {
      const res = await fetch("/device_info", {
        cache: "no-store",
        headers: cborHeaders(),
      });
      if (!res.ok) throw new Error("HTTP " + res.status);
      const data = await readCbor(res, {});
      setText("deviceInfoId", data.deviceId || "--");
      setText("deviceInfoHw", data.hw || "--");
      setText("deviceInfoSw", data.sw || "--");
    } catch (err) {
      console.warn("Failed to load device info:", err);
      setText("deviceInfoId", "--");
      setText("deviceInfoHw", "--");
      setText("deviceInfoSw", "--");
    }
  }

  function applyReadyOffFlagsToPower(readyBool, offBool) {
    if (offBool) {
      setPowerUI("Shutdown", { ready: false });
    } else if (lastState === "Running") {
      setPowerUI("Running", { ready: !!readyBool });
    }
  }

