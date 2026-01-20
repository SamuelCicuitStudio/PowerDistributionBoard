  // ========================================================
  // ===============        DOM READY INIT      =============
  // ========================================================

  document.addEventListener("DOMContentLoaded", () => {
    if (typeof adminHasToken !== "undefined" && !adminHasToken) {
      return;
    }
    mountAllOverlays();
    bindConfirmModal();

    renderAllOutputs("manualOutputs", true);
    renderAllOutputs("userAccessGrid", false);

    initPowerButton();
    bindDeviceSettingsSubtabs();
    initDashboardClock();
    liveRender();
    scheduleLiveInterval();

    // Keep session alive with backend heartbeat
    startHeartbeat(4000);
    loadControls();
    loadDeviceInfo();
    bindSessionHistoryButton();
    bindErrorButton();
    bindLogButton();
    bindEventBadge();
    bindEventToast();
    bindCalibrationButton();
    bindSetupWizardControls();
    bindWizardControls();
    bindLiveControlButton();
    updateStatusBarState();
    updateModePills();
    startWireTestPoll();
    updateSessionStatsUI(null);
    // Disconnect button
    const disconnectBtn = document.getElementById("disconnectBtn");
    if (disconnectBtn) {
      disconnectBtn.addEventListener("click", disconnectDevice);
    }

    // Relay toggle (manual)
    const relayToggle = document.getElementById("relayToggle");
    if (relayToggle) {
      relayToggle.addEventListener("change", async () => {
        const desired = relayToggle.checked;
        relayToggle.disabled = true;

        if (
          guardUnsafeAction("toggling the relay", {
            blockAuto: true,
            blockCalib: true,
          })
        ) {
          relayToggle.checked = !desired;
          relayToggle.disabled = false;
          return;
        }

        const pre = lastMonitor || (await fetchMonitorSnapshot());
        if (pre) applyMonitorSnapshot(pre);

        const resp = await sendControlCommand("set", "relay", desired);
        if (resp && resp.error) {
          relayToggle.disabled = false;
          await pollLiveOnce();
          return;
        }

        const mon = await waitForMonitorMatch((m) => m && m.relay === desired);
        if (mon) applyMonitorSnapshot(mon);
        else await pollLiveOnce();

        relayToggle.disabled = false;
      });
    }

    // Fan slider manual control
    const fanSlider = document.getElementById("fanSlider");
    if (fanSlider) {
      fanSlider.addEventListener("input", async () => {
        setFanSpeedValue(fanSlider.value);
        if (
          guardUnsafeAction("adjusting fan speed", {
            blockAuto: true,
            blockCalib: true,
          })
        ) {
          return;
        }
        const speed = parseInt(fanSlider.value, 10) || 0;
        sendControlCommand("set", "fanSpeed", speed);
      });
      setFanSpeedValue(fanSlider.value);
    }

    const confirmBtn = document.getElementById("wiresCoolConfirmBtn");
    if (confirmBtn) {
      confirmBtn.addEventListener("click", confirmWiresCool);
    }

    const modelTarget = document.getElementById("modelParamTarget");
    if (modelTarget) {
      modelTarget.addEventListener("change", () => {
        updateModelParamFields(lastLoadedControls);
      });
    }

    fetchSetupStatus();

    // Ensure tooltips exist for all visible UI elements.
    applyTooltips(document);
  });

