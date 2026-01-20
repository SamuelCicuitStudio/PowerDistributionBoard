  // ========================================================
  // ===============  DEVICE + NICHROME SETTINGS ============
  // ========================================================

  function bindDeviceSettingsSubtabs(root = document) {
    if (!root) return;

    const buttons = Array.from(
      root.querySelectorAll(".device-subtab-btn[data-device-subtab]")
    );
    const panels = Array.from(
      root.querySelectorAll(".device-subtab-panel[data-device-subtab-panel]")
    );

    if (!buttons.length || !panels.length) return;

    const setActive = (name) => {
      buttons.forEach((btn) => {
        const active = btn.dataset.deviceSubtab === name;
        btn.classList.toggle("is-active", active);
        btn.setAttribute("aria-selected", active ? "true" : "false");
      });

      panels.forEach((panel) => {
        const active = panel.dataset.deviceSubtabPanel === name;
        panel.classList.toggle("is-active", active);
        panel.hidden = !active;
      });

      try {
        localStorage.setItem("deviceSettingsSubtab", name);
      } catch (e) {
        // ignore storage errors
      }
    };

    const needsBind = buttons.some((btn) => !btn.dataset.subtabBound);
    if (!needsBind) return;

    buttons.forEach((btn) => {
      if (btn.dataset.subtabBound) return;
      btn.dataset.subtabBound = "true";
      btn.addEventListener("click", () => {
        const name = btn.dataset.deviceSubtab;
        if (name) setActive(name);
      });
    });

    let initial = "nichrome";
    try {
      const saved = localStorage.getItem("deviceSettingsSubtab");
      if (saved && buttons.some((btn) => btn.dataset.deviceSubtab === saved)) {
        initial = saved;
      }
    } catch (e) {
      // ignore storage errors
    }

    setActive(initial);
  }

  async function waitUntilApplied(expected, timeoutMs = 2000, stepMs = 120) {
    const deadline = Date.now() + timeoutMs;

    while (Date.now() < deadline) {
      try {
        const res = await fetch("/load_controls", {
          cache: "no-store",
          headers: cborHeaders(),
        });
        if (!res.ok) break;
        const data = await readCbor(res, {});

        let ok = true;

        if (expected.wireRes) {
          const wr = data.wireRes || {};
          for (const [idx, val] of Object.entries(expected.wireRes)) {
            if (!approxEqual(wr[String(idx)], val, 0.05)) {
              ok = false;
              break;
            }
          }
        }

        if (ok) {
          await loadControls();
          return true;
        }
      } catch (err) {
        console.warn("waitUntilApplied error:", err);
        break;
      }

      await sleep(stepMs);
    }

    await loadControls();
    return false;
  }

  function parseModelParamTarget(raw) {
    const value = String(raw || "").toLowerCase();
    if (value === "floor") return { kind: "floor" };
    const match = /^wire(\d+)$/.exec(value);
    const idx = match ? parseInt(match[1], 10) : 1;
    const safeIdx =
      Number.isFinite(idx) && idx >= 1 && idx <= 10 ? idx : 1;
    return { kind: "wire", index: safeIdx };
  }

  function getModelParamTarget() {
    const el = document.getElementById("modelParamTarget");
    if (!el) return { kind: "wire", index: 1 };
    return parseModelParamTarget(el.value || "wire1");
  }

  function getModelParamValues(data, target) {
    if (!data || !target) return { tau: undefined, k: undefined, c: undefined };
    if (target.kind === "floor") {
      return {
        tau: data.floorTau,
        k: data.floorK,
        c: data.floorC,
      };
    }
    const key = String(target.index);
    const wireTau = data.wireTau || {};
    const wireK = data.wireK || {};
    const wireC = data.wireC || {};
    return {
      tau: wireTau[key],
      k: wireK[key],
      c: wireC[key],
    };
  }

  function updateModelParamFields(data) {
    const targetEl = document.getElementById("modelParamTarget");
    if (!targetEl) return;
    const target = parseModelParamTarget(targetEl.value || "wire1");
    const values = getModelParamValues(data, target);
    setField("modelTau", values.tau);
    setField("modelK", values.k);
    setField("modelC", values.c);
  }

  async function saveDeviceAndNichrome() {
    if (
      guardUnsafeAction("saving device settings", {
        blockAuto: true,
        blockCalib: true,
      })
    ) {
      return;
    }
    const cmds = [];
    const expected = { wireRes: {} };
    const cur = lastLoadedControls || {};

    // Core device settings
    const acFreq = getInt("acFrequency");
    if (acFreq !== undefined && acFreq !== cur.acFrequency) {
      cmds.push(["set", "acFrequency", acFreq]);
    }

    const chargeR = getFloat("chargeResistor");
    if (
      chargeR !== undefined &&
      !approxEqual(chargeR, cur.chargeResistor, 0.05)
    ) {
      cmds.push(["set", "chargeResistor", chargeR]);
    }

    const currLimit = getFloat("currLimit");
    if (
      currLimit !== undefined &&
      !approxEqual(currLimit, cur.currLimit, 0.05)
    ) {
      cmds.push(["set", "currLimit", currLimit]);
    }

    const currentSource = getInt("currentSource");
    if (
      currentSource !== undefined &&
      (currentSource === 0 || currentSource === 1) &&
      currentSource !== cur.currentSource
    ) {
      cmds.push(["set", "currentSource", currentSource]);
    }

    const tempWarnC = getFloat("tempWarnC");
    if (
      tempWarnC !== undefined &&
      !approxEqual(tempWarnC, cur.tempWarnC, 0.05)
    ) {
      cmds.push(["set", "tempWarnC", tempWarnC]);
    }

    const tempTripC = getFloat("tempTripC");
    if (
      tempTripC !== undefined &&
      !approxEqual(tempTripC, cur.tempTripC, 0.05)
    ) {
      cmds.push(["set", "tempTripC", tempTripC]);
    }


    const wireGauge = getInt("wireGauge");
    if (wireGauge !== undefined && wireGauge !== cur.wireGauge) {
      cmds.push(["set", "wireGauge", wireGauge]);
    }

    // Wire resistances R01..R10 -> wireRes1..wireRes10
    const curWr = cur.wireRes || {};
    for (let i = 1; i <= 10; i++) {
      const id = "r" + String(i).padStart(2, "0") + "ohm";
      const val = getFloat(id);
      const curVal = curWr[String(i)];
      if (val !== undefined && !approxEqual(val, curVal, 0.05)) {
        cmds.push(["set", "wireRes" + i, val]);
        expected.wireRes[String(i)] = val;
      }
    }

    const wireOhmPerM = getFloat("wireOhmPerM");
    if (
      wireOhmPerM !== undefined &&
      !approxEqual(wireOhmPerM, cur.wireOhmPerM, 0.001)
    ) {
      cmds.push(["set", "wireOhmPerM", wireOhmPerM]);
    }

    const floorThicknessMm = getFloat("floorThicknessMm");
    if (
      floorThicknessMm !== undefined &&
      !approxEqual(floorThicknessMm, cur.floorThicknessMm, 0.1)
    ) {
      cmds.push(["set", "floorThicknessMm", floorThicknessMm]);
    }

    const floorMaterialSelect = document.getElementById("floorMaterial");
    if (floorMaterialSelect) {
      const floorMaterial = floorMaterialSelect.value || "wood";
      const curFloorMaterial =
        cur.floorMaterial !== undefined
          ? cur.floorMaterial
          : floorMaterialFromCode(cur.floorMaterialCode);
      if (floorMaterial !== curFloorMaterial) {
        cmds.push(["set", "floorMaterial", floorMaterial]);
      }
    }

    const floorMaxC = getFloat("floorMaxC");
    if (
      floorMaxC !== undefined &&
      !approxEqual(floorMaxC, cur.floorMaxC, 0.1)
    ) {
      cmds.push(["set", "floorMaxC", floorMaxC]);
    }

    const floorSwitchMarginC = getFloat("floorSwitchMarginC");
    if (
      floorSwitchMarginC !== undefined &&
      !approxEqual(floorSwitchMarginC, cur.floorSwitchMarginC, 0.1)
    ) {
      cmds.push(["set", "floorSwitchMarginC", floorSwitchMarginC]);
    }

    const nichromeFinalTempC = getFloat("nichromeFinalTempC");
    if (
      nichromeFinalTempC !== undefined &&
      !approxEqual(nichromeFinalTempC, cur.nichromeFinalTempC, 0.1)
    ) {
      cmds.push(["set", "nichromeFinalTempC", nichromeFinalTempC]);
    }

    const ntcGateIndex = getInt("ntcGateIndex");
    if (
      ntcGateIndex !== undefined &&
      Number.isFinite(ntcGateIndex) &&
      ntcGateIndex !== cur.ntcGateIndex
    ) {
      cmds.push(["set", "ntcGateIndex", ntcGateIndex]);
    }

    const ntcBeta = getFloat("ntcBeta");
    if (ntcBeta !== undefined && !approxEqual(ntcBeta, cur.ntcBeta, 0.5)) {
      cmds.push(["set", "ntcBeta", ntcBeta]);
    }

    const ntcT0C = getFloat("ntcT0C");
    if (ntcT0C !== undefined && !approxEqual(ntcT0C, cur.ntcT0C, 0.05)) {
      cmds.push(["set", "ntcT0C", ntcT0C]);
    }

    const ntcR0 = getFloat("ntcR0");
    if (ntcR0 !== undefined && !approxEqual(ntcR0, cur.ntcR0, 0.5)) {
      cmds.push(["set", "ntcR0", ntcR0]);
    }

    const ntcFixedRes = getFloat("ntcFixedRes");
    if (
      ntcFixedRes !== undefined &&
      !approxEqual(ntcFixedRes, cur.ntcFixedRes, 0.5)
    ) {
      cmds.push(["set", "ntcFixedRes", ntcFixedRes]);
    }

    const presenceMinDropV = getFloat("presenceMinDropV");
    if (
      presenceMinDropV !== undefined &&
      !approxEqual(presenceMinDropV, cur.presenceMinDropV, 0.01)
    ) {
      cmds.push(["set", "presenceMinDropV", presenceMinDropV]);
    }

    const modelTarget = getModelParamTarget();
    const modelTau = getFloat("modelTau");
    const modelK = getFloat("modelK");
    const modelC = getFloat("modelC");
    const curModel = getModelParamValues(cur, modelTarget);
    if (
      modelTau !== undefined &&
      !approxEqual(modelTau, curModel.tau, 0.01)
    ) {
      const key =
        modelTarget.kind === "floor"
          ? "floorTau"
          : "wireTau" + modelTarget.index;
      cmds.push(["set", key, modelTau]);
    }
    if (modelK !== undefined && !approxEqual(modelK, curModel.k, 0.001)) {
      const key =
        modelTarget.kind === "floor"
          ? "floorK"
          : "wireK" + modelTarget.index;
      cmds.push(["set", key, modelK]);
    }
    if (modelC !== undefined && !approxEqual(modelC, curModel.c, 0.01)) {
      const key =
        modelTarget.kind === "floor"
          ? "floorC"
          : "wireC" + modelTarget.index;
      cmds.push(["set", key, modelC]);
    }

    if (!cmds.length) {
      return;
    }

    // Send sequentially to preserve ordering
    for (const [a, t, v] of cmds) {
      await sendControlCommand(a, t, v);
    }

    await waitUntilApplied(expected, 2000, 120);
  }

  function resetDeviceAndNichrome() {
    if (!lastLoadedControls) {
      loadControls();
      return;
    }

    const data = lastLoadedControls;

    setField("acFrequency", data.acFrequency);
    setField("chargeResistor", data.chargeResistor);
    setField("currLimit", data.currLimit);
    if (data.currentSource !== undefined) {
      setField("currentSource", data.currentSource);
    }
    setField("tempWarnC", data.tempWarnC);
    setField("tempTripC", data.tempTripC);
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
      setField("floorMaterial", floorMaterialFromCode(data.floorMaterialCode));
    } else {
      setField("floorMaterial", "wood");
    }
    if (data.floorMaxC !== undefined) {
      setField("floorMaxC", data.floorMaxC);
    }
    if (data.floorSwitchMarginC !== undefined) {
      setField("floorSwitchMarginC", data.floorSwitchMarginC);
    }
    if (data.nichromeFinalTempC !== undefined) {
      setField("nichromeFinalTempC", data.nichromeFinalTempC);
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

    const wr = data.wireRes || {};
    for (let i = 1; i <= 10; i++) {
      const key = String(i);
      setField("r" + key.padStart(2, "0") + "ohm", wr[key]);
    }

    updateModelParamFields(data);

  }

