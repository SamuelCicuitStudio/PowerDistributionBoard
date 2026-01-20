  async function fetchCalibrationStatus() {
    try {
      const res = await fetch("/calib_status", {
        cache: "no-store",
        headers: cborHeaders(),
      });
      if (!res.ok) return null;
      return await readCbor(res, null);
    } catch (err) {
      console.warn("Calibration status error:", err);
      return null;
    }
  }

  async function fetchCalibrationPage(offset, count) {
    const url = `/calib_data?offset=${offset}&count=${count}`;
    const res = await fetch(url, { cache: "no-store", headers: cborHeaders() });
    if (!res.ok) return null;
    return await readCbor(res, null);
  }

  async function fetchCalibrationHistoryList() {
    try {
      const res = await fetch("/calib_history_list", {
        cache: "no-store",
        headers: cborHeaders(),
      });
      if (!res.ok) return null;
      return await readCbor(res, null);
    } catch (err) {
      console.warn("History list error:", err);
      return null;
    }
  }

  function formatEpochLocal(epochSec) {
    if (!epochSec) return "--";
    const d = new Date(epochSec * 1000);
    if (Number.isNaN(d.getTime())) return "--";
    return d.toLocaleString();
  }

  function formatElapsedMs(ms) {
    if (!Number.isFinite(ms) || ms <= 0) return "--";
    const sec = ms / 1000;
    if (sec >= 60) {
      const min = Math.floor(sec / 60);
      const rem = sec - min * 60;
      return `${min}m ${rem.toFixed(1)}s`;
    }
    return `${sec.toFixed(1)} s`;
  }

  function populateCalibHistorySelect(items, selectNewest) {
    const sel = document.getElementById("calibHistorySelect");
    if (!sel) return;
    const prev = sel.value;
    sel.innerHTML = "";

    if (!items || !items.length) {
      const opt = document.createElement("option");
      opt.value = "";
      opt.textContent = "No saved history";
      sel.appendChild(opt);
      sel.disabled = true;
      return;
    }

    const sorted = [...items].sort((a, b) => {
      const ea = Number(a.start_epoch || 0);
      const eb = Number(b.start_epoch || 0);
      return eb - ea;
    });

    for (const item of sorted) {
      const opt = document.createElement("option");
      opt.value = item.name || "";
      const label = item.start_epoch
        ? formatEpochLocal(item.start_epoch)
        : item.name;
      opt.textContent = label || item.name || "Unknown";
      sel.appendChild(opt);
    }

    sel.disabled = false;
    if (selectNewest) {
      sel.value = sel.options.length ? sel.options[0].value : "";
    } else if (prev) {
      sel.value = prev;
    }
  }

  async function refreshCalibHistoryList(selectNewest = false) {
    const data = await fetchCalibrationHistoryList();
    const items = data && data.items ? data.items : [];
    populateCalibHistorySelect(items, selectNewest);
  }

  async function loadCalibrationHistory() {
    const meta = calibLastMeta || (await fetchCalibrationStatus());
    if (!meta || !meta.count) {
      calibSamples = [];
      renderCalibrationChart();
      return;
    }

    const total = meta.count || 0;
    const all = [];
    let offset = 0;
    const pageSize = 200;

    while (offset < total) {
      const page = await fetchCalibrationPage(offset, pageSize);
      if (!page || !page.samples) break;
      all.push(...page.samples);
      offset += page.samples.length;
      if (page.samples.length < pageSize) break;
    }

    calibSamples = all;
    renderCalibrationChart();
    scrollCalibToLatest();

    const last = [...calibSamples].reverse().find((s) => isFinite(s.temp_c));
    if (last && isFinite(last.temp_c)) {
      setCalibText("calibTempText", `${last.temp_c.toFixed(1)} C`);
    }
    if (meta && Number.isFinite(meta.target_c)) {
      setCalibText("calibTargetText", `${meta.target_c.toFixed(1)} C`);
    }
  }

  async function loadSavedCalibration() {
    const sel = document.getElementById("calibHistorySelect");
    const name = sel ? sel.value : "";
    if (!name) {
      openAlert("Calibration", "Select a saved history first.", "warning");
      return;
    }
    try {
      const res = await fetch(
        `/calib_history_file?name=${encodeURIComponent(name)}`,
        {
          cache: "no-store",
          headers: cborHeaders(),
        }
      );
      if (!res.ok) {
        openAlert("Calibration", "Failed to load saved history.", "danger");
        return;
      }
      const data = await readCbor(res, {});
      calibViewMode = "saved";
      calibSamples = Array.isArray(data.samples) ? data.samples : [];
      calibLastMeta = data.meta || null;
      renderCalibrationChart();
      scrollCalibToLatest();

      const last = [...calibSamples].reverse().find((s) => isFinite(s.temp_c));
      setCalibText(
        "calibTempText",
        last && isFinite(last.temp_c) ? `${last.temp_c.toFixed(1)} C` : "--"
      );

      if (calibLastMeta) {
        setCalibText("calibStatusText", "Saved");
        setCalibText("calibModeText", calibLastMeta.mode || "--");
        setCalibText(
          "calibCountText",
          calibLastMeta.count != null ? String(calibLastMeta.count) : "0"
        );
        setCalibText(
          "calibIntervalText",
          calibLastMeta.interval_ms ? `${calibLastMeta.interval_ms} ms` : "--"
        );
        if (Number.isFinite(calibLastMeta.target_c)) {
          setCalibText(
            "calibTargetText",
            `${calibLastMeta.target_c.toFixed(1)} C`
          );
        } else {
          setCalibText("calibTargetText", "--");
        }
      }
      const historyBtn = document.getElementById("calibHistoryBtn");
      if (historyBtn) historyBtn.textContent = "Resume Live";
    } catch (err) {
      console.error("Saved history load error:", err);
      openAlert("Calibration", "Saved history load failed.", "danger");
    }
  }

  async function toggleCalibrationHistory() {
    const btn = document.getElementById("calibHistoryBtn");
    if (calibViewMode !== "live") {
      calibViewMode = "live";
      if (btn) btn.textContent = "Load History";
      await pollCalibrationOnce();
      return;
    }

    calibViewMode = "history";
    if (btn) btn.textContent = "Resume Live";
    await loadCalibrationHistory();
  }

  async function pollCalibrationOnce() {
    const meta = await fetchCalibrationStatus();
    if (!meta) return;

    const prevMeta = calibLastMeta;
    calibLastMeta = meta;
    const modeLabel =
      meta.mode === "model"
        ? "Model"
        : meta.mode === "floor"
        ? "Floor"
        : meta.mode === "ntc"
        ? "NTC"
        : "--";
    const statusLabel = meta.running
      ? meta.count
        ? "Running"
        : "Starting"
      : "Idle";
    setCalibText("calibStatusText", statusLabel);
    setCalibText("calibModeText", modeLabel);
    setCalibText(
      "calibCountText",
      meta.count != null ? String(meta.count) : "0"
    );
    setCalibText(
      "calibIntervalText",
      meta.interval_ms ? `${meta.interval_ms} ms` : "--"
    );
    const wireIdx = meta.wire_index;
    setCalibText("calibWireText", wireIdx ? `#${wireIdx}` : "--");
    if (Number.isFinite(meta.target_c)) {
      setCalibText("calibTargetText", `${meta.target_c.toFixed(1)} C`);
    } else {
      setCalibText("calibTargetText", "--");
    }
    const intervalMs = Number(meta.interval_ms);
    const elapsedMs =
      Number.isFinite(intervalMs) && meta.count ? meta.count * intervalMs : NaN;
    setCalibText("calibElapsedText", formatElapsedMs(elapsedMs));

    const desiredMs = meta.running
      ? Math.max(
          250,
          Math.min(1000, Number.isFinite(intervalMs) ? intervalMs : 500)
        )
      : 1000;
    setCalibPollInterval(desiredMs);

    updateStatusBarState();


    if (calibViewMode !== "live") return;
    if (calibChartPaused) return;

    const count = meta.count || 0;
    if (!count) {
      calibSamples = [];
      renderCalibrationChart();
      setCalibText("calibTempText", "--");
      return;
    }

    const page = await fetchCalibrationPage(Math.max(0, count - 200), 200);
    if (page && page.samples) {
      calibSamples = page.samples;
      renderCalibrationChart();

      const last = [...calibSamples].reverse().find((s) => isFinite(s.temp_c));
      if (last && isFinite(last.temp_c)) {
        setCalibText("calibTempText", `${last.temp_c.toFixed(1)} C`);
      } else {
        setCalibText("calibTempText", "--");
      }
      if (last && Number.isFinite(last.t_ms)) {
        setCalibText("calibElapsedText", formatElapsedMs(last.t_ms));
      }
    }
  }

  function startCalibrationPoll() {
    stopCalibrationPoll();
    calibPollIntervalMs = 1000;
    pollCalibrationOnce();
    calibPollTimer = setInterval(pollCalibrationOnce, calibPollIntervalMs);
  }

  function stopCalibrationPoll() {
    if (calibPollTimer) {
      clearInterval(calibPollTimer);
      calibPollTimer = null;
    }
  }

  function setCalibPollInterval(ms) {
    const clamped = Math.max(250, Math.min(2000, ms));
    if (clamped === calibPollIntervalMs) return;
    calibPollIntervalMs = clamped;
    if (calibPollTimer) {
      clearInterval(calibPollTimer);
      calibPollTimer = setInterval(pollCalibrationOnce, calibPollIntervalMs);
    }
  }

