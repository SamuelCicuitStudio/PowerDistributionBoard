const UPDATE_MS = 200;
const DX = 10;
const T_MIN = 0;
const T_MAX = 150;
const Y_TICK_STEP = 10;
const MAX_POINTS = 30 * 60 * (1000 / UPDATE_MS);

const H = 320;
const PLOT_PAD_LEFT = 10;
const Y0 = 260;
const Y1 = 30;
const RIGHT_PAD = 24;

const TICK_EVERY_SECONDS = 5;
const TICK_EVERY_SAMPLES = Math.round((TICK_EVERY_SECONDS * 1000) / UPDATE_MS);

const yAxisSvg = document.getElementById("yAxis");
const plotSvg = document.getElementById("chart");
const scrollWrap = document.getElementById("scrollWrap");
const tNowEl = document.getElementById("tNow");
const tsNowEl = document.getElementById("tsNow");
const btnLatest = document.getElementById("btnLatest");
const btnPause = document.getElementById("btnPause");

const data = [];
let paused = false;

function clamp(v, lo, hi) {
  return Math.max(lo, Math.min(hi, v));
}
function fmtTime(tsMs) {
  const d = new Date(tsMs);
  const hh = String(d.getHours()).padStart(2, "0");
  const mm = String(d.getMinutes()).padStart(2, "0");
  const ss = String(d.getSeconds()).padStart(2, "0");
  return `${hh}:${mm}:${ss}`;
}
function yFromTemp(tC) {
  const t = clamp(tC, T_MIN, T_MAX);
  return Y0 - ((t - T_MIN) * (Y0 - Y1)) / (T_MAX - T_MIN);
}
function tag(x, y, text) {
  const padX = 6,
    charW = 7;
  const w = Math.max(34, text.length * charW + padX * 2);
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

/* ===== Sticky Y-axis ===== */
function drawYAxis() {
  const W = 72;
  yAxisSvg.setAttribute("width", W);
  yAxisSvg.setAttribute("height", H);
  yAxisSvg.setAttribute("viewBox", `0 0 ${W} ${H}`);

  let s = `
    <line class="axisLine" x1="${W - 1}" y1="${Y1}" x2="${
    W - 1
  }" y2="${Y0}"></line>
    <text class="label" x="16" y="${(Y1 + Y0) / 2}" transform="rotate(-90 16 ${
    (Y1 + Y0) / 2
  })">°C</text>
  `;
  for (let t = T_MIN; t <= T_MAX; t += Y_TICK_STEP) {
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
drawYAxis();

/* ===== Plot rendering ===== */
function buildGrid(xMax) {
  let s = `<g class="grid">`;
  for (let t = T_MIN; t <= T_MAX; t += Y_TICK_STEP) {
    const y = yFromTemp(t);
    s += `<line class="gridLine" x1="0" y1="${y}" x2="${xMax}" y2="${y}"></line>`;
  }
  const vEverySamples = Math.round(1000 / UPDATE_MS);
  for (let i = 0; i < data.length; i += vEverySamples) {
    const x = PLOT_PAD_LEFT + i * DX;
    s += `<line x1="${x}" y1="${Y1}" x2="${x}" y2="${Y0}"></line>`;
  }
  s += `</g>`;
  return s;
}

function buildXAxis(xMax) {
  return `
    <line class="xAxis" x1="0" y1="${Y0}" x2="${xMax}" y2="${Y0}"></line>
    <text class="label" x="${Math.max(40, xMax / 2 - 20)}" y="${
    H - 10
  }">Time</text>
  `;
}

function buildTimeTicks() {
  let s = `<g>`;
  for (let i = 0; i < data.length; i += TICK_EVERY_SAMPLES) {
    const x = PLOT_PAD_LEFT + i * DX;
    const ts = data[i]?.tsMs;
    if (!ts) continue;
    const label = fmtTime(ts);
    s += `<line class="tick" x1="${x}" y1="${Y0}" x2="${x}" y2="${
      Y0 + 6
    }"></line>`;
    s += `<text class="subtext" x="${x - 18}" y="${Y0 + 22}">${label}</text>`;
  }
  s += `</g>`;
  return s;
}

function buildPolyline() {
  if (data.length === 0) return "";
  const pts = data
    .map((p, i) => `${PLOT_PAD_LEFT + i * DX},${yFromTemp(p.tC)}`)
    .join(" ");
  return `<polyline class="temp-line" points="${pts}"></polyline>`;
}

function buildLatestMarker(xMax) {
  if (data.length === 0) return "";
  const i = data.length - 1;
  const p = data[i];
  const x = PLOT_PAD_LEFT + i * DX;
  const y = yFromTemp(p.tC);

  const timeLabel = fmtTime(p.tsMs);
  const tempLabel = `${p.tC.toFixed(1)}°C`;
  const timeTagX = clamp(x + 8, 8, xMax - 110);

  return `
    <g>
      <line class="crosshair" x1="${x}" y1="${Y1}" x2="${x}" y2="${Y0}"></line>
      <line class="crosshair" x1="0" y1="${y}" x2="${xMax}" y2="${y}"></line>
      <circle class="end-dot" cx="${x}" cy="${y}" r="6"></circle>
      ${tag(timeTagX, Y0 + 26, timeLabel)}
      ${tag(8, clamp(y, Y1 + 12, Y0 - 12), tempLabel)}
    </g>
  `;
}

function redrawPlot() {
  const xMax = PLOT_PAD_LEFT + Math.max(1, data.length - 1) * DX + RIGHT_PAD;
  plotSvg.setAttribute("width", xMax);
  plotSvg.setAttribute("height", H);
  plotSvg.setAttribute("viewBox", `0 0 ${xMax} ${H}`);

  plotSvg.innerHTML = `
    ${buildGrid(xMax)}
    ${buildXAxis(xMax)}
    ${buildTimeTicks()}
    <text class="subtext" x="8" y="18">Latest point: dot + dotted guides</text>
    ${buildPolyline()}
    ${buildLatestMarker(xMax)}
  `;
}

function scrollToLatest() {
  scrollWrap.scrollLeft = scrollWrap.scrollWidth;
}

/* ===== Drag-to-pan ===== */
let dragging = false,
  startX = 0,
  startScrollLeft = 0;
function dragStart(clientX) {
  dragging = true;
  scrollWrap.classList.add("dragging");
  startX = clientX;
  startScrollLeft = scrollWrap.scrollLeft;
}
function dragMove(clientX) {
  if (!dragging) return;
  const dx = clientX - startX;
  scrollWrap.scrollLeft = startScrollLeft - dx;
}
function dragEnd() {
  dragging = false;
  scrollWrap.classList.remove("dragging");
}
scrollWrap.addEventListener("mousedown", (e) => {
  if (e.button !== 0) return;
  dragStart(e.clientX);
});
window.addEventListener("mousemove", (e) => dragMove(e.clientX));
window.addEventListener("mouseup", dragEnd);
scrollWrap.addEventListener(
  "touchstart",
  (e) => {
    if (e.touches.length !== 1) return;
    dragStart(e.touches[0].clientX);
  },
  { passive: true }
);
scrollWrap.addEventListener(
  "touchmove",
  (e) => {
    if (e.touches.length !== 1) return;
    dragMove(e.touches[0].clientX);
  },
  { passive: true }
);
scrollWrap.addEventListener("touchend", dragEnd);

/* ===== Simulation (replace with real sensor) ===== */
let temp = 25,
  sp = 120;
function simulatedTempStep() {
  const err = sp - temp;
  temp += err * 0.05 + (Math.random() - 0.5) * 0.8;
  temp = clamp(temp, T_MIN, T_MAX);
  const t = Date.now() / 1000;
  sp = 115 + 10 * Math.sin(t * 0.2);
  return temp;
}

/* ===== Loop ===== */
function pushSample(tC) {
  const ts = Date.now();
  data.push({ tsMs: ts, tC });
  if (data.length > MAX_POINTS) data.shift();
  tNowEl.textContent = tC.toFixed(1);
  tsNowEl.textContent = fmtTime(ts);
}

function step() {
  if (paused) return;
  const tC = simulatedTempStep(); // replace with realTempC
  pushSample(tC);
  redrawPlot();
  const nearEnd =
    scrollWrap.scrollLeft + scrollWrap.clientWidth >=
    scrollWrap.scrollWidth - 30;
  if (nearEnd) scrollToLatest();
}

for (let i = 0; i < 90; i++) pushSample(simulatedTempStep());
redrawPlot();
scrollToLatest();
setInterval(step, UPDATE_MS);

btnLatest.addEventListener("click", scrollToLatest);
btnPause.addEventListener("click", () => {
  paused = !paused;
  btnPause.textContent = paused ? "Resume" : "Pause";
});
