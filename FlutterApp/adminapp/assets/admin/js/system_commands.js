  // ========================================================
  // ===============     SYSTEM CONTROL CMDS     ============
  // ========================================================

  async function startSystem() {
    if (guardUnsafeAction("starting the loop", { blockCalib: true })) {
      return;
    }
    if (!setupRunAllowed) {
      openAlert(
        "Setup incomplete",
        "Complete configuration and calibration before running.",
        "warning"
      );
      return;
    }
    showWiresCoolPrompt();
    const res = await sendControlCommand("set", "systemStart", true);
    if (res && res.error) {
      hideWiresCoolPrompt();
      openAlert("Start system", res.error, "danger");
    }
  }

  async function shutdownSystem() {
    if (guardUnsafeAction("stopping the loop", { blockCalib: true })) {
      return;
    }
    await sendControlCommand("set", "systemShutdown", true);
  }

  async function forceCalibration() {
    if (
      guardUnsafeAction("starting calibration", {
        blockAuto: true,
        blockCalib: true,
      })
    ) {
      return;
    }
    if (calibrationBusy) {
      openAlert(
        "Calibration",
        "A calibration is already in progress.",
        "warning"
      );
      return;
    }

    calibrationBusy = true;
    try {
      const res = await sendControlCommand("set", "calibrate", true);
      if (res && !res.error) {
        openAlert("Calibration", "Calibration started.", "success");
      } else {
        openAlert(
          "Calibration",
          (res && res.error) || "Failed to start calibration",
          "danger"
        );
      }
    } catch (err) {
      console.error("Calibration request failed:", err);
      openAlert("Calibration", "Request failed.", "danger");
    } finally {
      calibrationBusy = false;
    }
  }

  async function resetSystem() {
    if (guardUnsafeAction("resetting the device", { blockCalib: true })) {
      return;
    }
    await sendControlCommand("set", "systemReset", true);
  }

  async function rebootSystem() {
    if (guardUnsafeAction("rebooting the device", { blockCalib: true })) {
      return;
    }
    await sendControlCommand("set", "reboot", true);
  }

  function showWiresCoolPrompt() {
    const prompt = document.getElementById("wiresCoolPrompt");
    if (!prompt) return;

    prompt.classList.add("show");

    if (wiresCoolPromptTimer) {
      clearTimeout(wiresCoolPromptTimer);
      wiresCoolPromptTimer = null;
    }

    wiresCoolPromptTimer = setTimeout(() => {
      hideWiresCoolPrompt();
    }, 5000);
  }

  function hideWiresCoolPrompt() {
    const prompt = document.getElementById("wiresCoolPrompt");
    if (!prompt) return;

    prompt.classList.remove("show");

    if (wiresCoolPromptTimer) {
      clearTimeout(wiresCoolPromptTimer);
      wiresCoolPromptTimer = null;
    }
  }

  async function confirmWiresCool() {
    if (
      guardUnsafeAction("confirming wires cool", {
        blockAuto: true,
        blockCalib: true,
      })
    ) {
      return;
    }
    const res = await sendControlCommand("set", "confirmWiresCool", true);
    if (res && res.error) {
      openAlert("Confirm Wires Cool", res.error, "danger");
      return;
    }
    hideWiresCoolPrompt();
    openAlert("Confirm Wires Cool", "Confirmation sent.", "success");
  }

