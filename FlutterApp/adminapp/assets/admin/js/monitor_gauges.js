  // ========================================================
  // ===============        MONITOR GAUGES       ============
  // ========================================================

  function updateGauge(id, value, unit, maxValue) {
    const display = document.getElementById(id);
    if (!display) return;
    const svg = display.closest("svg");
    if (!svg) return;
    const stroke = svg.querySelector("path.gauge-fg");
    if (!stroke) return;

    if (value === "Off") {
      stroke.setAttribute("stroke-dasharray", "0, 100");
      display.textContent = "Off";
      return;
    }

    const num = parseFloat(value);
    if (!Number.isFinite(num)) return;

    const percent = Math.min((num / maxValue) * 100, 100);
    stroke.setAttribute("stroke-dasharray", percent + ", 100");
    display.textContent = num.toFixed(2) + unit;
  }

  function setGaugeUnknown(id) {
    const display = document.getElementById(id);
    if (!display) return;
    const svg = display.closest("svg");
    const stroke = svg ? svg.querySelector("path.gauge-fg") : null;
    if (stroke) {
      stroke.setAttribute("stroke-dasharray", "0, 100");
    }
    display.textContent = "--";
  }

  function renderCapacitance(capF) {
    const display = document.getElementById("capacitanceValue");
    if (!display) return;

    const svg = display.closest("svg");
    const stroke = svg ? svg.querySelector("path.gauge-fg") : null;

    if (!Number.isFinite(capF) || capF <= 0) {
      if (stroke) {
        stroke.setAttribute("stroke-dasharray", "0, 100");
      }
      display.textContent = "--";
      return;
    }

    const capMilli = capF * 1000.0;
    updateGauge("capacitanceValue", capMilli, "mF", 500);
  }

  function startMonitorPolling(intervalMs = 400) {
    if (window.__MONITOR_INTERVAL__) {
      clearInterval(window.__MONITOR_INTERVAL__);
    }

    window.__MONITOR_INTERVAL__ = setInterval(async () => {
      try {
        const res = await fetch("/monitor", {
          cache: "no-store",
          headers: cborHeaders(),
        });
        if (res.status === 401) {
          noteAuthFailure();
          return;
        }
        if (!res.ok) return;
        resetAuthFailures();
        const data = await readCbor(res, {});

        // Relay sync
        const serverRelay = data.relay === true;
        setDot("relay", serverRelay);
        const relayToggle = document.getElementById("relayToggle");
        if (relayToggle) relayToggle.checked = serverRelay;

        // AC presence
        const ac = data.ac === true;
        setDot("ac", ac);

        // Voltage gauge (always show measured bus voltage)
        const v = Number(data.capVoltage);
        if (Number.isFinite(v)) {
          updateGauge("voltageValue", v, "V", 400);
        } else {
          setGaugeUnknown("voltageValue");
        }

        // Raw ADC display (scaled /100, e.g., 4095 -> 40.95)
        const adcEl = document.getElementById("adcRawValue");
        if (adcEl) {
          const rawScaled = parseFloat(data.capAdcRaw);
          adcEl.textContent = Number.isFinite(rawScaled)
            ? rawScaled.toFixed(2)
            : "--";
        }

        // Current gauge
        let rawCurrent = readAcsCurrent(data);
        if (!ac || !Number.isFinite(rawCurrent)) rawCurrent = 0;
        rawCurrent = Math.max(0, Math.min(100, rawCurrent));
        updateGauge("currentValue", rawCurrent, "A", 100);

        // Temperatures (up to 12 sensors)
        const temps = data.temperatures || [];
        for (let i = 0; i < 12; i++) {
          const id = "temp" + (i + 1) + "Value";
          const t = temps[i];
          const num = Number(t);
          if (
            t === undefined ||
            t === null ||
            Number.isNaN(num) ||
            num === -127
          ) {
            updateGauge(id, "Off", "\u00B0C", 150);
          } else {
            updateGauge(id, num, "\u00B0C", 150);
          }
        }

        // Capacitance (F -> mF)
        const capF = parseFloat(data.capacitanceF);
        renderCapacitance(capF);

        // Ready / Off LEDs
        const readyLed = document.getElementById("readyLed");
        const offLed = document.getElementById("offLed");
        if (readyLed)
          readyLed.style.backgroundColor = data.ready ? "limegreen" : "gray";
        if (offLed) offLed.style.backgroundColor = data.off ? "red" : "gray";

        // Fan slider reflect
        const fanSlider = document.getElementById("fanSlider");
        if (fanSlider && typeof data.fanSpeed === "number") {
          fanSlider.value = data.fanSpeed;
          setFanSpeedValue(data.fanSpeed);
        }
      } catch (err) {
        console.error("Monitor error:", err);
      }
    }, intervalMs);
  }

