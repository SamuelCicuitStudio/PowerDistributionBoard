  // ========================================================
  // ===============         LIVE OVERLAY        ============
  // ========================================================

  const LIVE = {
    svg: null,
    interval: null,
    markers: [],
    tempMarkers: [],
  };

  (function initLiveMarkers() {
    deltax = 10;
    const offset = 2.6;
    const l = 3;
    const h = 2.5 - deltax;

    LIVE.markers = [
      // AC (red) bottom-left
      {
        id: "ac",
        color: "red",
        x: 8 + h,
        y: 81 + offset,
        tx: 35, // where you want it to touch near live-core
        ty: 65,
        layout: "L", // 90 deg path
      },
      // Relay (yellow) bottom-right
      {
        id: "relay",
        color: "yellow",
        x: 92 - h,
        y: 81 + offset,
        tx: 65, // where you want it to touch near live-core
        ty: 65,
        layout: "L", // 90 deg path
      },
      // Left side outputs 6..10
      {
        id: "o6",
        color: "cyan",
        x: 8 + h,
        y: 11 + offset,
        tx: 45,
        ty: 40,
        layout: "L", // vertical then horizontal
      },
      {
        id: "o7",
        color: "cyan",
        x: 8 + h,
        y: 23 + offset,
        tx: 35,
        ty: 40,
        layout: "L", // vertical then horizontal
      },
      {
        id: "o8",
        color: "cyan",
        x: 8 + h,
        y: 35 + offset,
        tx: 45,
        ty: 35 + offset,
        layout: "straight",
      },
      {
        id: "o9",
        color: "cyan",
        x: 8 + h,
        y: 47 + offset,
        tx: 45,
        ty: 47 + offset,
        layout: "straight",
      },
      {
        id: "o10",
        color: "cyan",
        x: 8 + h,
        y: 59 + offset,
        tx: 45,
        ty: 59 + offset,
        layout: "straight",
      },
      // Right side outputs 5..1
      {
        id: "o5",
        color: "cyan",
        x: 92 - h,
        y: 11 + offset,
        tx: 55,
        ty: 40,
        layout: "L", // vertical then horizontal
      },
      {
        id: "o4",
        color: "cyan",
        x: 92 - h,
        y: 23 + offset,
        tx: 65,
        ty: 40,
        layout: "L", // vertical then horizontal
      },
      {
        id: "o3",
        color: "cyan",
        x: 92 - h,
        y: 35 + offset,
        tx: 45,
        ty: 35 + offset,
        layout: "straight",
      },
      {
        id: "o2",
        color: "cyan",
        x: 92 - h,
        y: 47 + offset,
        tx: 45,
        ty: 47 + offset,
        layout: "straight",
      },
      {
        id: "o1",
        color: "cyan",
        x: 92 - h,
        y: 59 + offset,
        tx: 45,
        ty: 59 + offset,
        layout: "straight",
      },
    ];
    delaty = 2.5;

    LIVE.tempMarkers = [
      // near left outputs 6..10
      { wire: 6, x: 20 - deltax, y: 11 + delaty },
      { wire: 7, x: 20 - deltax, y: 23 + delaty },
      { wire: 8, x: 20 - deltax, y: 35 + delaty },
      { wire: 9, x: 20 - deltax, y: 47 + delaty },
      { wire: 10, x: 20 - deltax, y: 59 + delaty },
      // near right outputs 5..1
      { wire: 5, x: 80 + deltax, y: 11 + delaty },
      { wire: 4, x: 80 + deltax, y: 23 + delaty },
      { wire: 3, x: 80 + deltax, y: 35 + delaty },
      { wire: 2, x: 80 + deltax, y: 47 + delaty },
      { wire: 1, x: 80 + deltax, y: 59 + delaty },
    ];
  })();

  function liveRender() {
    const svg = document.querySelector("#liveTab .live-overlay");
    if (!svg) return;

    LIVE.svg = svg;
    svg.innerHTML = "";

    const ns = "http://www.w3.org/2000/svg";
    const coreBox = computeCoreBox(svg);
    LIVE.coreBox = coreBox;

    // ---- Traces + dots ----
    for (const m of LIVE.markers) {
      const pts = buildTracePoints(m, coreBox);
      if (!pts || pts.length < 2) continue;

      // Draw the trace as a polyline (supports straight or L)
      const poly = document.createElementNS(ns, "polyline");
      poly.setAttribute("class", "trace");
      poly.setAttribute("points", pts.map((p) => `${p.x},${p.y}`).join(" "));
      svg.appendChild(poly);

      // Dot at the first point (wire endpoint)
      const first = pts[0];
      const dot = document.createElementNS(ns, "circle");
      dot.setAttribute("class", "dot " + m.color + " off");
      dot.setAttribute("r", 3.2);
      dot.setAttribute("cx", first.x);
      dot.setAttribute("cy", first.y);
      dot.dataset.id = m.id;
      svg.appendChild(dot);
    }

    // ---- Temperature badges (unchanged) ----
    for (const t of LIVE.tempMarkers) {
      const g = document.createElementNS(ns, "g");
      g.setAttribute("class", "temp-badge");
      g.dataset.wire = String(t.wire);

      const c = document.createElementNS(ns, "circle");
      c.setAttribute("class", "temp-circle");
      c.setAttribute("r", 4.3);
      c.setAttribute("cx", t.x);
      c.setAttribute("cy", t.y);

      const txt = document.createElementNS(ns, "text");
      txt.setAttribute("class", "temp-label");
      txt.setAttribute("x", t.x);
      txt.setAttribute("y", t.y + 0.3);
      txt.textContent = "--";

      g.appendChild(c);
      g.appendChild(txt);
      svg.appendChild(g);
    }
  }

  function setDot(id, state) {
    if (!LIVE.svg) return;

    const c = LIVE.svg.querySelector('circle[data-id="' + id + '"]');
    if (!c) return;

    // Remove previous state flags
    c.classList.remove("on", "off", "missing");

    // Support:
    //  - true  => ON
    //  - false => OFF (connected)
    //  - "missing"/"disconnected"/null => not connected (cyan / missing)
    if (state === "missing" || state === "disconnected" || state === null) {
      c.classList.add("missing");
      return;
    }

    const on = !!state;
    c.classList.toggle("on", on);
    c.classList.toggle("off", !on);
  }

  async function fetchMonitorSnapshot() {
    const res = await fetch("/monitor", {
      cache: "no-store",
      headers: cborHeaders(),
    });
    if (res.status === 401) {
      noteAuthFailure();
      return null;
    }
    if (!res.ok) return null;
    resetAuthFailures();
    return await readCbor(res, null);
  }

  function readAcsCurrent(mon) {
    if (!mon) return NaN;
    const acs = Number(
      mon.currentAcs !== undefined
        ? mon.currentAcs
        : mon.current_acs !== undefined
        ? mon.current_acs
        : mon.currentACS
    );
    if (Number.isFinite(acs)) return acs;
    const fallback = Number(mon.current);
    return Number.isFinite(fallback) ? fallback : NaN;
  }

    function applyMonitorSnapshot(mon) {
      if (!mon) return;
      lastMonitor = mon;
      updateAmbientWaitFromMonitor(mon.ambientWait);
      // --- Session stats (Live tab headline) ---
      if (mon.session) {
        updateSessionStatsUI(mon.session);
      }

    // --- Lifetime counters ---
    if (mon.sessionTotals) {
      const t = mon.sessionTotals;
      const totalEnergyEl = document.getElementById("totalEnergy");
      const totalSessionsEl = document.getElementById("totalSessions");
      const totalOkEl = document.getElementById("totalSessionsOk");

      if (totalEnergyEl)
        totalEnergyEl.textContent = (t.totalEnergy_Wh || 0).toFixed(2) + " Wh";
      if (totalSessionsEl)
        totalSessionsEl.textContent = (t.totalSessions || 0).toString();
      if (totalOkEl)
        totalOkEl.textContent = (t.totalSessionsOk || 0).toString();
    }

    const outs = mon.outputs || {};
    for (let i = 1; i <= 10; i++) {
      setDot("o" + i, !!outs["output" + i]);
    }
    for (let i = 1; i <= 10; i++) {
      const itemSel = "#manualOutputs .manual-item:nth-child(" + i + ")";
      const checkbox = document.querySelector(
        itemSel + ' input[type="checkbox"]'
      );
      const led = document.querySelector(itemSel + " .led");
      const on = !!outs["output" + i];
      if (checkbox) checkbox.checked = on;
      if (led) led.classList.toggle("active", on);
    }

    // Per-wire temps
    const wireTemps = mon.wireTemps || [];
    if (LIVE.svg) {
      for (const cfg of LIVE.tempMarkers) {
        const badge = LIVE.svg.querySelector(
          'g.temp-badge[data-wire="' + cfg.wire + '"]'
        );
        if (!badge) continue;

        const label = badge.querySelector("text.temp-label");
        if (!label) continue;

        const t = wireTemps[cfg.wire - 1];
        let txt = "--";

        badge.classList.remove("warn", "hot");

        if (typeof t === "number" && t > -100) {
          const rounded = Math.round(t);
          txt = String(rounded);

          if (t >= 400) badge.classList.add("hot");
          else if (t >= 250) badge.classList.add("warn");

          badge.setAttribute(
            "title",
            "Wire " + cfg.wire + ": " + t.toFixed(1) + "degC"
          );
        } else {
          badge.removeAttribute("title");
        }

        label.textContent = txt;
      }
    }

    // Relay + AC
    const serverRelay = mon.relay === true;
    const ac = mon.ac === true;
    setDot("relay", serverRelay);
    setDot("ac", ac);

    const relayToggle = document.getElementById("relayToggle");
    if (relayToggle) relayToggle.checked = serverRelay;

    // Voltage gauge (always show measured bus voltage)
    const v = Number(mon.capVoltage);
    if (Number.isFinite(v)) {
      updateGauge("voltageValue", v, "V", 400);
    } else {
      setGaugeUnknown("voltageValue");
    }

    // Raw ADC display (scaled /100, e.g., 4095 -> 40.95)
    const adcEl = document.getElementById("adcRawValue");
    if (adcEl) {
      const rawScaled = parseFloat(mon.capAdcRaw);
      adcEl.textContent = Number.isFinite(rawScaled)
        ? rawScaled.toFixed(2)
        : "--";
    }

    // Current gauge
    let rawCurrent = readAcsCurrent(mon);
    if (!ac || !Number.isFinite(rawCurrent)) rawCurrent = 0;
    rawCurrent = Math.max(0, Math.min(100, rawCurrent));
    updateGauge("currentValue", rawCurrent, "A", 100);

    // Temperatures (up to 12 sensors)
    const temps = mon.temperatures || [];
    for (let i = 0; i < 12; i++) {
      const id = "temp" + (i + 1) + "Value";
      const t = temps[i];
      const num = Number(t);
      if (t === undefined || t === null || Number.isNaN(num) || num === -127) {
        updateGauge(id, "Off", "\u00B0C", 150);
      } else {
        updateGauge(id, num, "\u00B0C", 150);
      }
    }

    // Capacitance (F -> mF)
    const capF = parseFloat(mon.capacitanceF);
    renderCapacitance(capF);

    // Ready / Off LEDs
    const readyLed = document.getElementById("readyLed");
    const offLed = document.getElementById("offLed");
    if (readyLed)
      readyLed.style.backgroundColor = mon.ready ? "limegreen" : "gray";
    if (offLed) offLed.style.backgroundColor = mon.off ? "red" : "gray";

    // Fan slider reflect
    const fanSlider = document.getElementById("fanSlider");
    if (fanSlider && typeof mon.fanSpeed === "number") {
      fanSlider.value = mon.fanSpeed;
      setFanSpeedValue(mon.fanSpeed);
    }

    if (mon.eventUnread) {
      handleEventUnreadUpdate(mon.eventUnread);
    }

    updateWifiSignal(mon);
    updateTopTemps(mon);
    updateTopPower(mon);
  }

  async function waitForMonitorMatch(predicate, opts = {}) {
    const timeoutMs = opts.timeoutMs || 1500;
    const intervalMs = opts.intervalMs || 120;
    const start = Date.now();
    let last = null;
    while (Date.now() - start < timeoutMs) {
      const mon = await fetchMonitorSnapshot();
      if (mon) {
        last = mon;
        if (predicate(mon)) return mon;
      }
      await sleep(intervalMs);
    }
    return last;
  }

  async function pollLiveOnce() {
    try {
      const mon = await fetchMonitorSnapshot();
      if (!mon) return;
      applyMonitorSnapshot(mon);
      pushLiveControlSample(mon);
      if (isLiveControlOpen()) {
        renderLiveControlChart();
      }
    } catch (err) {
      console.warn("live poll failed:", err);
    }
  }

  // -------------------- Live SSE playback (constant speed) --------------------
  function applyLiveSample(sample) {
    if (!sample) return;

    // Outputs mask -> dots
    if (typeof sample.mask === "number") {
      for (let i = 0; i < 10; i++) {
        const on = !!(sample.mask & (1 << i));
        setDot("o" + (i + 1), on);
      }
    }

    // Wire temps -> badges
    const wireTemps = sample.wireTemps || [];
    if (LIVE.svg) {
      for (const cfg of LIVE.tempMarkers) {
        const badge = LIVE.svg.querySelector(
          'g.temp-badge[data-wire="' + cfg.wire + '"]'
        );
        if (!badge) continue;

        const label = badge.querySelector("text.temp-label");
        if (!label) continue;

        const t = wireTemps[cfg.wire - 1];
        let txt = "--";

        badge.classList.remove("warn", "hot");

        if (typeof t === "number" && t > -100) {
          const rounded = Math.round(t);
          txt = String(rounded);

          if (t >= 400) badge.classList.add("hot");
          else if (t >= 250) badge.classList.add("warn");

          badge.setAttribute(
            "title",
            "Wire " + cfg.wire + ": " + Number(t).toFixed(1) + "\u00B0C"
          );
        } else {
          badge.removeAttribute("title");
        }

        label.textContent = txt;
      }
    }

    setDot("relay", sample.relay === true);
    setDot("ac", sample.ac === true);

    const relayToggle = document.getElementById("relayToggle");
    if (relayToggle) relayToggle.checked = sample.relay === true;
  }

  // Live snapshots are polled from /monitor (no server push).
  function computeCoreBox(svg) {
    const core = document.querySelector("#liveTab .live-core");
    if (!svg || !core) return null;

    const vb = svg.viewBox.baseVal;
    const svgRect = svg.getBoundingClientRect();
    const coreRect = core.getBoundingClientRect();

    const scaleX = vb.width / svgRect.width;
    const scaleY = vb.height / svgRect.height;

    const x1 = (coreRect.left - svgRect.left) * scaleX;
    const y1 = (coreRect.top - svgRect.top) * scaleY;
    const x2 = (coreRect.right - svgRect.left) * scaleX;
    const y2 = (coreRect.bottom - svgRect.top) * scaleY;

    return {
      x1,
      y1,
      x2,
      y2,
      cx: (x1 + x2) / 2,
      cy: (y1 + y2) / 2,
    };
  }

  function clamp(v, min, max) {
    return v < min ? min : v > max ? max : v;
  }

  function makeTracePath(start, end, layout) {
    // layout:
    //  - "straight"  => direct line
    //  - "L" / "Lh"  => 90 deg with horizontal-then-vertical
    //  - "Lv"        => 90 deg with vertical-then-horizontal
    //  - undefined   => default "L"

    const mode = layout || "L";

    if (mode === "straight") {
      return [start, end];
    }

    // Decide orientation for L
    if (mode === "Lv") {
      // vertical then horizontal
      const mid = { x: start.x, y: end.y };
      return [start, mid, end];
    }

    // "L" or "Lh" or default:
    // horizontal then vertical
    const horizontalFirst =
      mode === "Lh" ||
      mode === "L" ||
      Math.abs(end.x - start.x) >= Math.abs(end.y - start.y);

    if (horizontalFirst) {
      const mid = { x: end.x, y: start.y };
      return [start, mid, end];
    } else {
      const mid = { x: start.x, y: end.y };
      return [start, mid, end];
    }
  }

  function getCoreAnchor(start, coreBox) {
    // Hit the nearest edge of the live-core box, perpendicular-style
    if (!coreBox) return start;

    const { x1, y1, x2, y2 } = coreBox;

    // Left of core -> go to left edge
    if (start.x < x1) {
      return { x: x1, y: clamp(start.y, y1, y2) };
    }

    // Right of core -> go to right edge
    if (start.x > x2) {
      return { x: x2, y: clamp(start.y, y1, y2) };
    }

    // Above core -> go to top edge
    if (start.y < y1) {
      return { x: clamp(start.x, x1, x2), y: y1 };
    }

    // Below core -> go to bottom edge
    return { x: clamp(start.x, x1, x2), y: y2 };
  }
  function buildTracePoints(m, coreBox) {
    const start = { x: m.x, y: m.y };

    // 1) Full manual override: explicit polyline
    //    m.points = [ {x,y}, {x,y}, ... ]
    if (Array.isArray(m.points) && m.points.length >= 2) {
      return m.points;
    }

    // 2) New: explicit end point with chosen layout
    //    m.tx, m.ty, optional m.layout
    if (typeof m.tx === "number" && typeof m.ty === "number") {
      const end = { x: m.tx, y: m.ty };
      return makeTracePath(start, end, m.layout);
    }

    // 3) Backward compat: ax/ay (old style)
    //    Now also respects m.layout if present
    if (typeof m.ax === "number" && typeof m.ay === "number") {
      const end = { x: m.ax, y: m.ay };
      return makeTracePath(start, end, m.layout);
    }

    // 4) If we can't see the core, only show the dot
    if (!coreBox) return [start];

    // 5) Auto: anchor on live-core box + layout rules
    const anchor = getCoreAnchor(start, coreBox);
    return makeTracePath(start, anchor, m.layout);
  }

