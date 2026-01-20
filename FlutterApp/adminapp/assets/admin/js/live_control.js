  // ========================================================
  // ===============      LIVE CONTROL VIEW     =============
  // ========================================================

  const LIVE_CTRL_COLORS = [
    "#ffb347",
    "#00c2ff",
    "#00ff9d",
    "#ff6b6b",
    "#9b6bff",
    "#ffd166",
    "#4cc9f0",
    "#f72585",
    "#b8f2e6",
    "#a3be8c",
  ];
  const LIVE_CTRL_SETPOINT_COLOR = "#ff2d6f";

  const LIVE_CTRL_TICK_EVERY_SECONDS = 5;

  function isLiveControlOpen() {
    const modal = document.getElementById("liveControlModal");
    if (modal) return modal.classList.contains("show");
    return liveControlModalOpen;
  }

  function getLiveControlSetpoint() {
    if (testModeState.active && Number.isFinite(testModeState.targetC)) {
      return testModeState.targetC;
    }
    const monitorTarget = lastMonitor ? Number(lastMonitor.wireTargetC) : NaN;
    if (Number.isFinite(monitorTarget)) return monitorTarget;
    const input = document.getElementById("wireTestTargetC");
    const fallback = input ? Number(input.value) : NaN;
    if (Number.isFinite(fallback)) return fallback;
    return NaN;
  }

  function getLiveControlModeLabel() {
    if (testModeState.active) return testModeState.label;
    if (lastState === "Running") return "Run";
    if (lastState === "Shutdown") return "Off";
    return "Idle";
  }

  function getLiveControlAllowedWires() {
    const access =
      (lastLoadedControls && lastLoadedControls.outputAccess) || {};
    const accessKeys = Object.keys(access || {});
    const hasAccessMap = accessKeys.some((k) => k.startsWith("output"));
    const allowed = [];
    for (let i = 1; i <= 10; i++) {
      const key = "output" + i;
      if (hasAccessMap && !access[key]) continue;
      allowed.push(i);
    }
    return allowed;
  }

  function getLiveControlPresentWires(samples, allowed) {
    if (!samples.length) return [];
    const temps = samples[samples.length - 1].wireTemps || [];
    return allowed.filter((idx) => {
      const t = Number(temps[idx - 1]);
      return Number.isFinite(t) && t > -100;
    });
  }

  function renderLiveControlLegend(wires, setpointC) {
    const el = document.getElementById("liveControlLegend");
    if (!el) return;
    const items = [];

    if (Number.isFinite(setpointC)) {
      items.push(
        `<div class="legend-item"><span class="legend-swatch" style="background:${LIVE_CTRL_SETPOINT_COLOR}"></span><span class="legend-label">Setpoint</span></div>`
      );
    }

    for (const wire of wires) {
      const color = LIVE_CTRL_COLORS[(wire - 1) % LIVE_CTRL_COLORS.length];
      items.push(
        `<div class="legend-item"><span class="legend-swatch" style="background:${color}"></span><span class="legend-label">Wire ${wire}</span></div>`
      );
    }

    if (!items.length) {
      items.push(
        '<div class="legend-item"><span class="legend-label">No allowed outputs</span></div>'
      );
    }

    el.innerHTML = items.join("");
  }

  function drawLiveControlYAxis() {
    const yAxisSvg = document.getElementById("liveControlYAxis");
    if (!yAxisSvg) return;

    const W = 72;
    yAxisSvg.setAttribute("width", W);
    yAxisSvg.setAttribute("height", CALIB_H);
    yAxisSvg.setAttribute("viewBox", `0 0 ${W} ${CALIB_H}`);

    let s = `
      <line class="axisLine" x1="${W - 1}" y1="${CALIB_Y1}" x2="${
      W - 1
    }" y2="${CALIB_Y0}"></line>
      <text class="label" x="16" y="${
        (CALIB_Y1 + CALIB_Y0) / 2
      }" transform="rotate(-90 16 ${(CALIB_Y1 + CALIB_Y0) / 2})">C</text>
    `;

    for (let t = CALIB_T_MIN; t <= CALIB_T_MAX; t += CALIB_Y_TICK_STEP) {
      const y = yFromTemp(t);
      s += `<line class="yTick" x1="${W - 8}" y1="${y}" x2="${
        W - 1
      }" y2="${y}"></line>`;
      s += `<text class="yLabel" x="${W - 12}" y="${
        y + 4
      }" text-anchor="end">${t}</text>`;
    }
    yAxisSvg.innerHTML = s;
  }

  function buildLiveControlGrid(xMax, intervalMs, pointCount) {
    let s = `<g class="grid">`;
    for (let t = CALIB_T_MIN; t <= CALIB_T_MAX; t += CALIB_Y_TICK_STEP) {
      const y = yFromTemp(t);
      s += `<line class="gridLine" x1="0" y1="${y}" x2="${xMax}" y2="${y}"></line>`;
    }
    const vEverySamples = Math.max(
      1,
      Math.round(1000 / Math.max(50, intervalMs || 500))
    );
    for (let i = 0; i < pointCount; i += vEverySamples) {
      const x = CALIB_PLOT_PAD_LEFT + i * CALIB_DX;
      s += `<line x1="${x}" y1="${CALIB_Y1}" x2="${x}" y2="${CALIB_Y0}"></line>`;
    }
    s += `</g>`;
    return s;
  }

  function buildLiveControlXAxis(xMax) {
    return `
      <line class="xAxis" x1="0" y1="${CALIB_Y0}" x2="${xMax}" y2="${CALIB_Y0}"></line>
      <text class="label" x="${Math.max(40, xMax / 2 - 20)}" y="${
      CALIB_H - 10
    }">Time</text>
    `;
  }

  function buildLiveControlTimeTicks(samples, intervalMs) {
    let s = `<g>`;
    const tickEverySamples = Math.max(
      1,
      Math.round(
        (LIVE_CTRL_TICK_EVERY_SECONDS * 1000) / Math.max(50, intervalMs || 500)
      )
    );
    for (let i = 0; i < samples.length; i += tickEverySamples) {
      const x = CALIB_PLOT_PAD_LEFT + i * CALIB_DX;
      const tMs = samples[i].t_ms || 0;
      const label = fmtUptime(tMs);
      s += `<line class="tick" x1="${x}" y1="${CALIB_Y0}" x2="${x}" y2="${
        CALIB_Y0 + 6
      }"></line>`;
      s += `<text class="subtext" x="${x - 18}" y="${
        CALIB_Y0 + 22
      }">${label}</text>`;
    }
    s += `</g>`;
    return s;
  }

  function buildLiveControlWirePath(samples, wireIndex) {
    let d = "";
    let started = false;
    for (let i = 0; i < samples.length; i++) {
      const temps = samples[i].wireTemps || [];
      const raw = Number(temps[wireIndex - 1]);
      if (!Number.isFinite(raw) || raw <= -100) {
        started = false;
        continue;
      }
      const x = CALIB_PLOT_PAD_LEFT + i * CALIB_DX;
      const y = yFromTemp(raw);
      d += `${started ? " L" : " M"} ${x} ${y}`;
      started = true;
    }
    return d;
  }

  function buildLiveControlWirePaths(samples, wires) {
    let s = "";
    for (const wire of wires) {
      const path = buildLiveControlWirePath(samples, wire);
      if (!path) continue;
      const color = LIVE_CTRL_COLORS[(wire - 1) % LIVE_CTRL_COLORS.length];
      s += `<path class="live-line" stroke="${color}" d="${path}"></path>`;
    }
    return s;
  }

  function buildLiveControlSetpointLine(xMax, setpointC) {
    if (!Number.isFinite(setpointC)) return "";
    const y = yFromTemp(setpointC);
    return `<line class="setpoint-line" stroke="${LIVE_CTRL_SETPOINT_COLOR}" x1="0" y1="${y}" x2="${xMax}" y2="${y}"></line>`;
  }

  function scrollLiveControlToLatest() {
    const wrap = document.getElementById("liveControlScrollWrap");
    if (!wrap) return;
    wrap.scrollLeft = wrap.scrollWidth;
  }

  function renderLiveControlChart() {
    const plotSvg = document.getElementById("liveControlPlot");
    const scrollWrap = document.getElementById("liveControlScrollWrap");
    if (!plotSvg || !scrollWrap) return;

    drawLiveControlYAxis();

    const samples = (liveControlSamples || []).filter((s) =>
      Array.isArray(s.wireTemps)
    );
    const setpoint = getLiveControlSetpoint();
    const modeLabel = getLiveControlModeLabel();

    if (!samples.length) {
      plotSvg.setAttribute("width", 600);
      plotSvg.setAttribute("height", CALIB_H);
      plotSvg.setAttribute("viewBox", `0 0 600 ${CALIB_H}`);
      plotSvg.innerHTML = `<text class="subtext" x="8" y="18">No live data yet.</text>`;
      const tEl = document.getElementById("liveControlNowTimePill");
      const sEl = document.getElementById("liveControlSetpointPill");
      const mEl = document.getElementById("liveControlModePill");
      if (tEl) tEl.textContent = "--:--";
      if (sEl) sEl.textContent = "--";
      if (mEl) mEl.textContent = modeLabel;
      renderLiveControlLegend([], setpoint);
      return;
    }

    const allowed = getLiveControlAllowedWires();
    const present = getLiveControlPresentWires(samples, allowed);
    renderLiveControlLegend(present, setpoint);

    const intervalMs = Math.max(50, liveControlLastIntervalMs || 500);
    const xMax =
      CALIB_PLOT_PAD_LEFT +
      Math.max(1, samples.length - 1) * CALIB_DX +
      CALIB_RIGHT_PAD;
    const nearEnd =
      scrollWrap.scrollLeft + scrollWrap.clientWidth >=
      scrollWrap.scrollWidth - 30;

    plotSvg.setAttribute("width", xMax);
    plotSvg.setAttribute("height", CALIB_H);
    plotSvg.setAttribute("viewBox", `0 0 ${xMax} ${CALIB_H}`);
    plotSvg.innerHTML = `
      ${buildLiveControlGrid(xMax, intervalMs, samples.length)}
      ${buildLiveControlXAxis(xMax)}
      ${buildLiveControlTimeTicks(samples, intervalMs)}
      ${buildLiveControlSetpointLine(xMax, setpoint)}
      ${buildLiveControlWirePaths(samples, present)}
    `;

    const last = samples[samples.length - 1];
    const tEl = document.getElementById("liveControlNowTimePill");
    const sEl = document.getElementById("liveControlSetpointPill");
    const mEl = document.getElementById("liveControlModePill");
    if (tEl) tEl.textContent = fmtUptime(last.t_ms || 0);
    if (sEl)
      sEl.textContent = Number.isFinite(setpoint) ? setpoint.toFixed(1) : "--";
    if (mEl) mEl.textContent = modeLabel;

    if (!liveControlChartPaused && nearEnd) {
      scrollLiveControlToLatest();
    }
  }

  function bindLiveControlChartDrag() {
    const scrollWrap = document.getElementById("liveControlScrollWrap");
    if (!scrollWrap || scrollWrap.__dragBound) return;
    scrollWrap.__dragBound = true;

    const dragStart = (clientX) => {
      liveControlDrag.dragging = true;
      scrollWrap.classList.add("dragging");
      liveControlDrag.startX = clientX;
      liveControlDrag.startScrollLeft = scrollWrap.scrollLeft;
    };
    const dragMove = (clientX) => {
      if (!liveControlDrag.dragging) return;
      const dx = clientX - liveControlDrag.startX;
      scrollWrap.scrollLeft = liveControlDrag.startScrollLeft - dx;
    };
    const dragEnd = () => {
      liveControlDrag.dragging = false;
      scrollWrap.classList.remove("dragging");
    };

    scrollWrap.addEventListener("mousedown", (e) => {
      if (e.button !== 0) return;
      dragStart(e.clientX);
    });
    window.addEventListener("mousemove", (e) => dragMove(e.clientX));
    window.addEventListener("mouseup", dragEnd);

    scrollWrap.addEventListener(
      "touchstart",
      (e) => {
        if (!e.touches || e.touches.length !== 1) return;
        dragStart(e.touches[0].clientX);
      },
      { passive: true }
    );
    scrollWrap.addEventListener(
      "touchmove",
      (e) => {
        if (!e.touches || e.touches.length !== 1) return;
        dragMove(e.touches[0].clientX);
      },
      { passive: true }
    );
    scrollWrap.addEventListener("touchend", dragEnd);
  }

  function toggleLiveControlPause() {
    liveControlChartPaused = !liveControlChartPaused;
    const pauseBtn = document.getElementById("liveControlPauseBtn");
    if (pauseBtn)
      pauseBtn.textContent = liveControlChartPaused ? "Resume" : "Pause";
    if (!liveControlChartPaused) {
      renderLiveControlChart();
    }
  }

  function clearLiveControlSamples() {
    liveControlSamples = [];
    liveControlStartPerf = null;
    liveControlLastIntervalMs = 1000;
    renderLiveControlChart();
  }

  function pushLiveControlSample(mon) {
    if (!mon || !Array.isArray(mon.wireTemps)) return;
    const now = performance.now();
    if (liveControlStartPerf === null) liveControlStartPerf = now;
    const tMs = Math.round(now - liveControlStartPerf);
    const temps = mon.wireTemps.map((t) => Number(t));
    const setpoint = getLiveControlSetpoint();

    liveControlSamples.push({
      t_ms: tMs,
      wireTemps: temps,
      target_c: setpoint,
    });
    if (liveControlSamples.length > liveControlMaxSamples) {
      liveControlSamples.shift();
    }
    if (liveControlSamples.length >= 2) {
      const prev = liveControlSamples[liveControlSamples.length - 2].t_ms;
      const dt = tMs - prev;
      if (dt > 0 && dt < 60000) liveControlLastIntervalMs = dt;
    }
  }

  function openLiveControlModal() {
    const m = document.getElementById("liveControlModal");
    if (!m) return;
    m.classList.add("show");
    liveControlModalOpen = true;
    liveControlChartPaused = false;
    const pauseBtn = document.getElementById("liveControlPauseBtn");
    if (pauseBtn) pauseBtn.textContent = "Pause";
    bindLiveControlChartDrag();
    renderLiveControlChart();
    applyTooltips(m);
  }

  function closeLiveControlModal() {
    const m = document.getElementById("liveControlModal");
    if (m) m.classList.remove("show");
    liveControlModalOpen = false;
    liveControlChartPaused = false;
  }

  function bindLiveControlButton() {
    const btn = document.getElementById("liveControlBtn");
    if (btn) btn.addEventListener("click", openLiveControlModal);
    const topBtn = document.getElementById("topLiveControlBtn");
    if (topBtn) topBtn.addEventListener("click", openLiveControlModal);
    const stopBtn = document.getElementById("topStopTestBtn");
    if (stopBtn) stopBtn.addEventListener("click", stopActiveTestMode);

    const latestBtn = document.getElementById("liveControlLatestBtn");
    if (latestBtn) {
      latestBtn.addEventListener("click", () => scrollLiveControlToLatest());
    }
    const pauseBtn = document.getElementById("liveControlPauseBtn");
    if (pauseBtn) pauseBtn.addEventListener("click", toggleLiveControlPause);

    const clearBtn = document.getElementById("liveControlClearBtn");
    if (clearBtn) clearBtn.addEventListener("click", clearLiveControlSamples);
  }

  function scheduleLiveInterval() {
    if (monitorPollTimer) clearInterval(monitorPollTimer);
    const ms = lastState === "Running" ? 250 : 1000;
    pollLiveOnce();
    monitorPollTimer = setInterval(pollLiveOnce, ms);
  }

  // Hook power UI -> live interval
  const _origSetPowerUI = setPowerUI;
  setPowerUI = function (state, extras) {
    _origSetPowerUI(state, extras || {});
    scheduleLiveInterval();
  };

