  // ========================================================
  // ===============    MANUAL OUTPUT CONTROL    ============
  // ========================================================

  async function handleOutputToggle(index, checkbox) {
    const isOn = !!checkbox.checked;
    checkbox.disabled = true;

    if (
      guardUnsafeAction("toggling outputs", {
        blockAuto: true,
        blockCalib: true,
      })
    ) {
      checkbox.checked = !isOn;
      checkbox.disabled = false;
      return;
    }

    const pre = lastMonitor || (await fetchMonitorSnapshot());
    if (pre) applyMonitorSnapshot(pre);

    const resp = await sendControlCommand("set", "output" + index, isOn);
    if (resp && resp.error) {
      checkbox.disabled = false;
      await pollLiveOnce();
      return;
    }

    const mon = await waitForMonitorMatch(
      (m) => m && m.outputs && m.outputs["output" + index] === isOn
    );
    if (mon) applyMonitorSnapshot(mon);
    else await pollLiveOnce();

    checkbox.disabled = false;
  }

  async function updateOutputAccess(index, newState) {
    if (
      guardUnsafeAction("changing output access", {
        blockAuto: true,
        blockCalib: true,
      })
    ) {
      loadControls();
      return;
    }
    const key = "output" + index;
    const res = await sendControlCommand("set", "Access" + index, !!newState);
    if (res && res.error) {
      loadControls();
      return;
    }
    if (!lastLoadedControls) lastLoadedControls = {};
    if (!lastLoadedControls.outputAccess) lastLoadedControls.outputAccess = {};
    lastLoadedControls.outputAccess[key] = !!newState;
    renderLiveControlChart();
  }

  function toggleOutput(index, state) {
    sendControlCommand("set", "output" + index, !!state);
  }

