  // ========================================================
  // ===============       SAFETY INTERLOCKS     ============
  // ========================================================

  function isAutoLoopRunning() {
    return lastState === "Running";
  }

  function isCalibrationActive() {
    const wireActive = !!(lastWireTestStatus && lastWireTestStatus.running);
    const calibRunning = !!(calibLastMeta && calibLastMeta.running);
    return (
      !!(testModeState && testModeState.active) ||
      wireActive ||
      calibRunning
    );
  }

  function guardUnsafeAction(actionLabel, opts = {}) {
    if (opts.blockCalib && isCalibrationActive()) {
      openAlert(
        "Calibration active",
        `Stop the active calibration/test before ${actionLabel}.`,
        "warning"
      );
      return true;
    }
    if (opts.blockAuto && isAutoLoopRunning()) {
      openAlert(
        "Auto loop running",
        `Stop the loop before ${actionLabel}.`,
        "warning"
      );
      return true;
    }
    return false;
  }

  function applySafetyLocks() {
    const autoRunning = isAutoLoopRunning();
    const calibActive = isCalibrationActive();
    const blockSettings = autoRunning || calibActive;
    const blockStart = !setupRunAllowed && lastState === "Shutdown";

    const powerBtn = powerEl();
    if (powerBtn) {
      const disable = calibActive || blockStart;
      powerBtn.disabled = disable;
      powerBtn.classList.toggle("action-locked", disable);
      powerBtn.setAttribute("aria-disabled", disable ? "true" : "false");
    }

    document.querySelectorAll(".round-button.reset").forEach((btn) => {
      btn.classList.toggle("action-locked", calibActive);
      btn.setAttribute("aria-disabled", calibActive ? "true" : "false");
    });

    const confirmBtn = document.getElementById("wiresCoolConfirmBtn");
    if (confirmBtn) {
      const disable = autoRunning || calibActive;
      confirmBtn.classList.toggle("action-locked", disable);
      confirmBtn.setAttribute("aria-disabled", disable ? "true" : "false");
    }

    const forceBtn = document.getElementById("forceCalibrationBtn");
    if (forceBtn) forceBtn.disabled = blockSettings;

    const saveSelector =
      "#deviceSettingsTab .settings-btn-primary," +
      " #adminSettingsTab .settings-btn-primary," +
      " #userSettingsTab .settings-btn-primary";
    document.querySelectorAll(saveSelector).forEach((btn) => {
      btn.disabled = blockSettings;
    });

    document
      .querySelectorAll(
        "#deviceSettingsTab input, #deviceSettingsTab select, #deviceSettingsTab textarea"
      )
      .forEach((el) => {
        el.disabled = blockSettings;
      });

    document
      .querySelectorAll("#deviceSettingsTab .settings-btn")
      .forEach((btn) => {
        btn.disabled = blockSettings;
      });

    document
      .querySelectorAll('#userAccessGrid input[type="checkbox"]')
      .forEach((cb) => {
        cb.disabled = blockSettings;
      });

    document
      .querySelectorAll(".setup-wizard-card input")
      .forEach((el) => {
        el.disabled = blockSettings;
      });

      const lockIds = [
        "startModelCalibBtn",
        "wireTestStartBtn",
        "ntcCalStartBtn",
        "ntcCalStopBtn",
        "ntcBetaCalBtn",
        "startFloorCalibBtn",
        "presenceProbeBtn",
        "setupUpdateBtn",
        "setupDoneBtn",
        "setupClearDoneBtn",
        "setupResetBtn",
      ];
    lockIds.forEach((id) => {
      const el = document.getElementById(id);
      if (el) el.disabled = blockSettings;
    });
  }

