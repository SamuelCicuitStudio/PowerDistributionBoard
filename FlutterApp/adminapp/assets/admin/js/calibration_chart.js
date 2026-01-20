  function initCalibrationChartUi() {
    drawCalibYAxis();
    bindCalibChartDrag();
  }

  function clamp(v, lo, hi) {
    return Math.max(lo, Math.min(hi, v));
  }

  function fmtUptime(ms) {
    const total = Math.max(0, Math.floor((ms || 0) / 1000));
    const hh = Math.floor(total / 3600);
    const mm = Math.floor((total % 3600) / 60);
    const ss = total % 60;
    if (hh > 0)
      return `${hh}:${String(mm).padStart(2, "0")}:${String(ss).padStart(
        2,
        "0"
      )}`;
    return `${mm}:${String(ss).padStart(2, "0")}`;
  }

  function fmtEpochTime(epochSec) {
    if (!epochSec) return "--:--";
    const d = new Date(epochSec * 1000);
    if (Number.isNaN(d.getTime())) return "--:--";
    const hh = String(d.getHours()).padStart(2, "0");
    const mm = String(d.getMinutes()).padStart(2, "0");
    const ss = String(d.getSeconds()).padStart(2, "0");
    return `${hh}:${mm}:${ss}`;
  }

  function fmtCalibTime(msFromStart, startEpochSec, startMs) {
    if (startEpochSec) {
      const epoch = startEpochSec + Math.round((msFromStart || 0) / 1000);
      return fmtEpochTime(epoch);
    }
    return fmtUptime((startMs || 0) + (msFromStart || 0));
  }

  const CALIB_T_MIN = 0;
  const CALIB_T_MAX = 150;
  const CALIB_Y_TICK_STEP = 10;
  const CALIB_H = 220;
  const CALIB_PLOT_PAD_LEFT = 10;
  const CALIB_Y1 = 18;
  const CALIB_Y0 = CALIB_H - 46;
  const CALIB_RIGHT_PAD = 24;
  const CALIB_DX = 10;
  const CALIB_TICK_EVERY_SECONDS = 5;

  function yFromTemp(tC) {
    const t = clamp(Number(tC), CALIB_T_MIN, CALIB_T_MAX);
    return (
      CALIB_Y0 -
      ((t - CALIB_T_MIN) * (CALIB_Y0 - CALIB_Y1)) / (CALIB_T_MAX - CALIB_T_MIN)
    );
  }

  function tag(x, y, text) {
    const padX = 6;
    const charW = 7;
    const w = Math.max(34, String(text).length * charW + padX * 2);
    const h = 20;
    return `
      <g>
        <rect class="tag" x="${x}" y="${
      y - h / 2
    }" width="${w}" height="${h}" rx="9"></rect>
        <text class="tagText" x="${x + padX}" y="${y + 4}">${text}</text>
      </g>
    `;
  }

  function drawCalibYAxis() {
    const yAxisSvg = document.getElementById("calibYAxis");
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

  function buildCalibGrid(xMax, intervalMs, pointCount) {
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

  function buildCalibXAxis(xMax) {
    return `
      <line class="xAxis" x1="0" y1="${CALIB_Y0}" x2="${xMax}" y2="${CALIB_Y0}"></line>
      <text class="label" x="${Math.max(40, xMax / 2 - 20)}" y="${
      CALIB_H - 10
    }">Time</text>
    `;
  }

  function buildCalibTimeTicks(samples, intervalMs, startMs, startEpochSec) {
    let s = `<g>`;
    const tickEverySamples = Math.max(
      1,
      Math.round(
        (CALIB_TICK_EVERY_SECONDS * 1000) / Math.max(50, intervalMs || 500)
      )
    );
    for (let i = 0; i < samples.length; i += tickEverySamples) {
      const x = CALIB_PLOT_PAD_LEFT + i * CALIB_DX;
      const tMs = samples[i].t_ms || 0;
      const label = fmtCalibTime(tMs, startEpochSec, startMs);
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

  function buildCalibPolyline(samples) {
    if (samples.length === 0) return "";
    const pts = samples
      .map(
        (p, i) => `${CALIB_PLOT_PAD_LEFT + i * CALIB_DX},${yFromTemp(p.temp_c)}`
      )
      .join(" ");
    return `<polyline class="temp-line" points="${pts}"></polyline>`;
  }

  function buildCalibLatestMarker(samples, xMax, startMs, startEpochSec) {
    if (samples.length === 0) return "";
    const i = samples.length - 1;
    const p = samples[i];
    const x = CALIB_PLOT_PAD_LEFT + i * CALIB_DX;
    const y = yFromTemp(p.temp_c);

    const timeLabel = fmtCalibTime(p.t_ms || 0, startEpochSec, startMs);
    const tempLabel = `${Number(p.temp_c).toFixed(1)}C`;
    const timeTagX = clamp(x + 8, 8, xMax - 110);

    return `
      <g>
        <line class="crosshair" x1="${x}" y1="${CALIB_Y1}" x2="${x}" y2="${CALIB_Y0}"></line>
        <line class="crosshair" x1="0" y1="${y}" x2="${xMax}" y2="${y}"></line>
        <circle class="end-dot" cx="${x}" cy="${y}" r="6"></circle>
        ${tag(timeTagX, CALIB_Y0 + 26, timeLabel)}
        ${tag(8, clamp(y, CALIB_Y1 + 12, CALIB_Y0 - 12), tempLabel)}
      </g>
    `;
  }

  function renderCalibrationChart() {
    const plotSvg = document.getElementById("calibPlot");
    const scrollWrap = document.getElementById("calibScrollWrap");
    if (!plotSvg || !scrollWrap) return;

    drawCalibYAxis();

    const samples = (calibSamples || []).filter(
      (s) => isFinite(s.temp_c) && isFinite(s.t_ms)
    );

    if (!samples.length) {
      plotSvg.setAttribute("width", 600);
      plotSvg.setAttribute("height", CALIB_H);
      plotSvg.setAttribute("viewBox", `0 0 600 ${CALIB_H}`);
      plotSvg.innerHTML = `<text class="subtext" x="8" y="18">No calibration data yet.</text>`;
      const tEl = document.getElementById("calibNowTempPill");
      const tsEl = document.getElementById("calibNowTimePill");
      if (tEl) tEl.textContent = "--";
      if (tsEl) tsEl.textContent = "--:--";
      return;
    }

    const intervalMs =
      (calibLastMeta && calibLastMeta.interval_ms) ||
      (calibLastMeta && calibLastMeta.intervalMs) ||
      500;
    const startMs =
      (calibLastMeta && (calibLastMeta.start_ms || calibLastMeta.startMs)) || 0;
    const startEpochSec =
      (calibLastMeta &&
        (calibLastMeta.start_epoch || calibLastMeta.startEpoch)) ||
      0;

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
      ${buildCalibGrid(xMax, intervalMs, samples.length)}
      ${buildCalibXAxis(xMax)}
      ${buildCalibTimeTicks(samples, intervalMs, startMs, startEpochSec)}
      <text class="subtext" x="8" y="18">Latest point: dot + dotted guides</text>
      ${buildCalibPolyline(samples)}
      ${buildCalibLatestMarker(samples, xMax, startMs, startEpochSec)}
    `;

    const last = samples[samples.length - 1];
    const tEl = document.getElementById("calibNowTempPill");
    const tsEl = document.getElementById("calibNowTimePill");
    if (tEl) tEl.textContent = Number(last.temp_c).toFixed(1);
    if (tsEl)
      tsEl.textContent = fmtCalibTime(last.t_ms || 0, startEpochSec, startMs);

    if (!calibChartPaused && nearEnd) {
      scrollCalibToLatest();
    }
  }

  function bindCalibChartDrag() {
    const scrollWrap = document.getElementById("calibScrollWrap");
    if (!scrollWrap || scrollWrap.__dragBound) return;
    scrollWrap.__dragBound = true;

    const dragStart = (clientX) => {
      calibChartDrag.dragging = true;
      scrollWrap.classList.add("dragging");
      calibChartDrag.startX = clientX;
      calibChartDrag.startScrollLeft = scrollWrap.scrollLeft;
    };
    const dragMove = (clientX) => {
      if (!calibChartDrag.dragging) return;
      const dx = clientX - calibChartDrag.startX;
      scrollWrap.scrollLeft = calibChartDrag.startScrollLeft - dx;
    };
    const dragEnd = () => {
      calibChartDrag.dragging = false;
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

