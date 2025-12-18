/**
 * Mock backend for the admin calibration UI.
 *
 * How to use:
 *   1) Serve the UI normally (no firmware needed).
 *   2) Open the browser console and run: window.enableCalibrationMock();
 *   3) The calibration modal, wire test, and NTC calibrate buttons will
 *      operate against this mock instead of real endpoints.
 *
 * Notes:
 *   - Unknown fetch() URLs fall through to the real network fetch.
 *   - Calibration samples are synthesized (smooth heat-up + cooldown).
 */
(function () {
  const realFetch = window.fetch ? window.fetch.bind(window) : null;
  if (!realFetch) return;

  let mockEnabled = false;

  const state = {
    calib: {
      running: false,
      mode: "none",
      intervalMs: 500,
      startMs: 0,
      samples: [],
      timer: null,
      targetC: 120,
      wireIndex: 1,
      saved: false,
      savedMs: 0,
      seq: 0,
    },
    wireTest: {
      running: false,
      targetC: 120,
      wireIndex: 1,
      tempC: 25,
      duty: 0,
      onMs: 0,
      offMs: 333,
      updatedMs: 0,
    },
    ntc: {
      r0: 13710,
    },
  };

  function nowMs() {
    return Math.floor(performance.now());
  }

  function synthTemp(tMs) {
    // Simple 2-phase curve: rise then plateau + mild decay.
    const t = tMs / 1000;
    if (t < 80) {
      return 25 + 0.9 * t; // ~97 C at 80s
    }
    if (t < 140) {
      return 97 + 0.25 * (t - 80); // drift to ~112 C
    }
    const decay = Math.max(0, 112 - 0.15 * (t - 140));
    return 25 + decay;
  }

  function addCalibSample() {
    if (!state.calib.running) return;
    const tMs = nowMs() - state.calib.startMs;
    const tempC = synthTemp(tMs);
    const voltageV = 310 - 0.08 * state.calib.samples.length;
    const currentA = 8 + 0.02 * state.calib.samples.length;
    state.calib.samples.push({
      t_ms: tMs,
      time_s: tMs / 1000,
      temp_c: tempC,
      volt_v: voltageV,
      curr_a: currentA,
      ntc_valid: true,
    });
    state.calib.seq++;
  }

  function startCalibTimer() {
    stopCalibTimer();
    addCalibSample();
    state.calib.timer = setInterval(addCalibSample, state.calib.intervalMs);
  }

  function stopCalibTimer() {
    if (state.calib.timer) {
      clearInterval(state.calib.timer);
      state.calib.timer = null;
    }
  }

  function handleCalibStatus() {
    const meta = state.calib;
    return {
      running: meta.running,
      mode: meta.mode,
      count: meta.samples.length,
      capacity: 2048,
      interval_ms: meta.intervalMs,
      start_ms: meta.startMs,
      saved: meta.saved,
      saved_ms: meta.savedMs,
      target_c: meta.targetC,
      wire_index: meta.wireIndex,
      pwm_running: state.wireTest.running,
      pwm_wire_index: state.wireTest.wireIndex,
      pwm_on_ms: state.wireTest.onMs,
      pwm_off_ms: state.wireTest.offMs,
    };
  }

  function handleCalibData(url) {
    const params = new URL(url, window.location.origin).searchParams;
    const offset = parseInt(params.get("offset") || "0", 10);
    const count = parseInt(params.get("count") || "200", 10);
    const slice = state.calib.samples.slice(offset, offset + count);
    return {
      samples: slice,
    };
  }

  function handleCalibStart(body) {
    const mode = body.mode || "ntc";
    const interval = body.interval_ms || 500;
    const maxSamples = body.max_samples || 1200;
    const targetC = body.target_c || 120;
    const wireIndex = body.wire_index || 1;

    state.calib.running = true;
    state.calib.mode = mode;
    state.calib.intervalMs = interval;
    state.calib.startMs = nowMs();
    state.calib.samples = [];
    state.calib.targetC = targetC;
    state.calib.wireIndex = wireIndex;
    state.calib.saved = false;
    state.calib.savedMs = 0;
    state.calib.seq = 0;

    // Fill some starter samples
    for (let i = 0; i < Math.min(20, maxSamples); i++) {
      addCalibSample();
    }
    startCalibTimer();

    // Auto-start wire test mock only for model calibration (not for NTC-only)
    if (mode === "model") {
      state.wireTest.running = true;
      state.wireTest.targetC = targetC;
      state.wireTest.wireIndex = wireIndex;
      state.wireTest.duty = 0.5;
      state.wireTest.onMs = 160;
      state.wireTest.offMs = 173;
      state.wireTest.updatedMs = nowMs();
    } else {
      state.wireTest.running = false;
    }

    return { status: "ok", running: true };
  }

  function handleCalibStop() {
    stopCalibTimer();
    state.calib.running = false;
    state.calib.saved = true;
    state.calib.savedMs = nowMs();
    return { status: "ok", running: false, saved: true };
  }

  function handleCalibClear() {
    stopCalibTimer();
    state.calib.running = false;
    state.calib.samples = [];
    state.calib.saved = false;
    state.calib.savedMs = 0;
    return { status: "ok", cleared: true, file_removed: true };
  }

  function handleWireTestStatus() {
    const wt = state.wireTest;
    if (wt.running) {
      const t = Math.sin(nowMs() / 3000);
      wt.tempC = 25 + (wt.targetC - 25) * 0.6 + 5 * t;
      wt.duty = 0.4 + 0.2 * Math.sin(nowMs() / 2000);
      wt.onMs = Math.round(wt.duty * 333);
      wt.offMs = 333 - wt.onMs;
      wt.updatedMs = nowMs();
    }
    return {
      running: wt.running,
      wire_index: wt.wireIndex,
      target_c: wt.targetC,
      temp_c: wt.tempC,
      duty: wt.duty,
      on_ms: wt.onMs,
      off_ms: wt.offMs,
      updated_ms: wt.updatedMs,
    };
  }

  function handleWireTestStart(body) {
    const target = body.target_c || 120;
    const idx = body.wire_index || 1;
    state.wireTest.running = true;
    state.wireTest.targetC = target;
    state.wireTest.wireIndex = idx;
    state.wireTest.updatedMs = nowMs();
    return { status: "ok", running: true };
  }

  function handleWireTestStop() {
    state.wireTest.running = false;
    state.wireTest.onMs = 0;
    state.wireTest.offMs = 333;
    state.wireTest.duty = 0;
    return { status: "ok", running: false };
  }

  function handleNtcCalibrate(body) {
    const ref = body.ref_temp_c || 32.5;
    // Nudge R0 slightly each time
    state.ntc.r0 += (Math.random() - 0.5) * 50;
    return { status: "ok", ref_c: ref, r0_ohm: state.ntc.r0 };
  }

  function jsonResponse(obj, status = 200) {
    return new Response(JSON.stringify(obj), {
      status,
      headers: { "Content-Type": "application/json" },
    });
  }

  async function mockFetch(input, init = {}) {
    const url = typeof input === "string" ? input : input.url;
    const method = (init.method || (typeof input === "object" && input.method) || "GET").toUpperCase();

    try {
      // Calibration status/data
      if (url.includes("/calib_status")) {
        return jsonResponse(handleCalibStatus());
      }
      if (url.includes("/calib_data")) {
        return jsonResponse(handleCalibData(url));
      }
      if (url.includes("/calib_start") && method === "POST") {
        const body = init.body ? JSON.parse(init.body) : {};
        return jsonResponse(handleCalibStart(body));
      }
      if (url.includes("/calib_stop") && method === "POST") {
        return jsonResponse(handleCalibStop());
      }
      if (url.includes("/calib_clear") && method === "POST") {
        return jsonResponse(handleCalibClear());
      }

      // Wire test
      if (url.includes("/wire_test_status")) {
        return jsonResponse(handleWireTestStatus());
      }
      if (url.includes("/wire_test_start") && method === "POST") {
        const body = init.body ? JSON.parse(init.body) : {};
        return jsonResponse(handleWireTestStart(body));
      }
      if (url.includes("/wire_test_stop") && method === "POST") {
        return jsonResponse(handleWireTestStop());
      }

      // NTC calibrate
      if (url.includes("/ntc_calibrate") && method === "POST") {
        const body = init.body ? JSON.parse(init.body) : {};
        return jsonResponse(handleNtcCalibrate(body));
      }
    } catch (err) {
      console.warn("[mock] handler error", err);
      return jsonResponse({ error: "mock_error" }, 500);
    }

    // Fallback to real network
    return realFetch(input, init);
  }

  function enableMock() {
    if (mockEnabled) return true;
    mockEnabled = true;
    window.fetch = mockFetch;
    console.info("[mock] Calibration mock enabled.");
    return true;
  }

  window.enableCalibrationMock = enableMock;

  // Auto-enable for convenience.
  enableMock();
})();
