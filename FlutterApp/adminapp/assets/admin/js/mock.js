/**
 * Mock backend for the admin calibration UI + live monitoring.
 *
 * How to use:
 *   1) Serve the UI normally (no firmware needed).
 *   2) Open the browser console and run: window.enableCalibrationMock();
 *   3) The calibration modal, settings, and live monitor will use this mock.
 *
 * Notes:
 *   - Unknown fetch() URLs fall through to the real network fetch.
 *   - The mock keeps a persisted settings snapshot and live values.
 */
(function () {
  const realFetch = window.fetch ? window.fetch.bind(window) : null;
  if (!realFetch) return;

  let mockEnabled = false;

  function nowMs() {
    return Math.floor(performance.now());
  }

  function clamp(val, min, max) {
    return Math.max(min, Math.min(max, val));
  }

  function makeOutputMap(value) {
    const out = {};
    for (let i = 1; i <= 10; i++) {
      out["output" + i] = value;
    }
    return out;
  }

  function makeWireRes() {
    const out = {};
    for (let i = 1; i <= 10; i++) {
      out[String(i)] = Number((4.2 + i * 0.18).toFixed(2));
    }
    return out;
  }

  const FLOOR_THICKNESS_MIN_MM = 20;
  const FLOOR_THICKNESS_MAX_MM = 50;
  const DEFAULT_FLOOR_MAX_C = 35;
  const DEFAULT_NICHROME_FINAL_TEMP_C = 120;
  const WIRE_TARGET_MAX_C = 450;

  const MATERIAL_CODES = {
    wood: 0,
    epoxy: 1,
    concrete: 2,
    slate: 3,
    marble: 4,
    granite: 5,
  };

  function floorMaterialFromCode(code) {
    switch (Number(code)) {
      case 1:
        return "epoxy";
      case 2:
        return "concrete";
      case 3:
        return "slate";
      case 4:
        return "marble";
      case 5:
        return "granite";
      default:
        return "wood";
    }
  }

  function floorMaterialToCode(value) {
    const key = String(value || "").toLowerCase();
    if (Object.prototype.hasOwnProperty.call(MATERIAL_CODES, key)) {
      return MATERIAL_CODES[key];
    }
    const asNum = Number(value);
    if (Number.isFinite(asNum) && asNum >= 0 && asNum <= 5) {
      return asNum;
    }
    return MATERIAL_CODES.wood;
  }

  function materialBaseC(material) {
    switch (String(material || "").toLowerCase()) {
      case "epoxy":
        return 29.0;
      case "concrete":
        return 30.5;
      case "slate":
        return 31.5;
      case "marble":
        return 32.5;
      case "granite":
        return 33.0;
      default:
        return 28.0;
    }
  }

  function resolveFloorTargetC() {
    const floorMax = Number(state.controls.floorMaxC);
    const thickness = Number(state.controls.floorThicknessMm);
    if (!Number.isFinite(floorMax) || floorMax <= 0) return NaN;
    if (!Number.isFinite(thickness) || thickness <= 0) return NaN;

    const mat =
      state.controls.floorMaterial ||
      floorMaterialFromCode(state.controls.floorMaterialCode);
    const base = materialBaseC(mat);
    const maxC = Math.min(floorMax, DEFAULT_FLOOR_MAX_C);
    const span = FLOOR_THICKNESS_MAX_MM - FLOOR_THICKNESS_MIN_MM;
    const norm =
      span > 0 ? (thickness - FLOOR_THICKNESS_MIN_MM) / span : 0;
    const gain = clamp(norm, 0, 1) * 2.5;
    let target = base + gain;
    if (target > maxC) target = maxC;
    if (target < 0) target = 0;
    return target;
  }

  function resolveWireTargetC() {
    const wt = state.wireTest;
    if (wt && wt.running && Number.isFinite(wt.targetC)) {
      return clamp(wt.targetC, 0, WIRE_TARGET_MAX_C);
    }
    const floorTarget = resolveFloorTargetC();
    if (Number.isFinite(floorTarget)) {
      return clamp(floorTarget, 0, WIRE_TARGET_MAX_C);
    }
    const fallback = Number(state.controls.nichromeFinalTempC);
    if (Number.isFinite(fallback)) {
      return clamp(fallback, 0, WIRE_TARGET_MAX_C);
    }
    return DEFAULT_NICHROME_FINAL_TEMP_C;
  }

  function synthTemp(tMs, targetC) {
    const t = tMs / 1000;
    const baseTarget = clamp(Number(targetC) || 120, 40, 150);
    const rise = 1 - Math.exp(-t / 60);
    const wobble = 0.02 * Math.sin(t / 8);
    const raw = 25 + (baseTarget - 25) * (rise + wobble);
    return clamp(raw, 15, 160);
  }

  function makeCalibSample(tMs, index, targetC) {
    const tempC = synthTemp(tMs, targetC);
    const voltageV = 310 - 0.03 * index;
    const currentA = 7.8 + 0.02 * index;
    const ntcV = 1.1 + 0.005 * (tempC - 25);
    const ntcOhm = Math.max(25, 10000 * Math.exp(-tempC / 55));
    const ntcAdc = Math.round((ntcV / 3.3) * 4095);
    return {
      t_ms: tMs,
      v: voltageV,
      i: currentA,
      temp_c: tempC,
      ntc_v: ntcV,
      ntc_ohm: ntcOhm,
      ntc_adc: ntcAdc,
      ntc_ok: true,
      pressed: false,
    };
  }

  function generateCalibSamples(count, intervalMs, startMs, targetC) {
    const out = [];
    for (let i = 0; i < count; i++) {
      const tMs = startMs + i * intervalMs;
      out.push(makeCalibSample(tMs, i, targetC));
    }
    return out;
  }

  function seedCalibHistory() {
    const epoch = Math.floor(Date.now() / 1000) - 86400;
    const samples = generateCalibSamples(180, 500, 0, 120);
    return [
      {
        name: `calib_${epoch}.json`,
        start_epoch: epoch,
        meta: {
          running: false,
          mode: "model",
          count: samples.length,
          capacity: 2048,
          interval_ms: 500,
          start_ms: 0,
          start_epoch: epoch,
          saved: true,
          saved_ms: 0,
          saved_epoch: epoch,
          target_c: 120,
          wire_index: 1,
        },
        samples,
      },
    ];
  }

  function seedSessionHistory() {
    const now = Date.now();
    return [
      {
        start_ms: now - 6 * 3600 * 1000,
        duration_s: 900,
        energy_Wh: 240.5,
        peakPower_W: 880,
        peakCurrent_A: 9.8,
      },
      {
        start_ms: now - 2 * 3600 * 1000,
        duration_s: 640,
        energy_Wh: 180.2,
        peakPower_W: 760,
        peakCurrent_A: 8.4,
      },
    ];
  }

  function buildSessionTotals(history) {
    const totals = {
      totalEnergy_Wh: 0,
      totalSessions: history.length,
      totalSessionsOk: history.length,
    };
    for (const s of history) {
      totals.totalEnergy_Wh += Number(s.energy_Wh) || 0;
    }
    return totals;
  }

  function seedEvents() {
    const now = Math.floor(Date.now() / 1000);
    return {
      unread: { warn: 2, error: 1 },
      warnings: [
        { epoch: now - 7200, ms: 0, reason: "Thermal drift warning" },
        { epoch: now - 3600, ms: 0, reason: "Line voltage low" },
      ],
      errors: [
        { epoch: now - 5400, ms: 0, reason: "Overcurrent trip" },
      ],
      last_error: { epoch: now - 5400, ms: 0, reason: "Overcurrent trip" },
      last_stop: { epoch: now - 1800, ms: 0, reason: "Manual shutdown" },
    };
  }
  const sessionHistory = seedSessionHistory();
  const sessionTotals = buildSessionTotals(sessionHistory);

  const state = {
    device: {
      state: "Idle",
      lastChangeMs: nowMs(),
    },
    controls: {
      ledFeedback: true,
      acFrequency: 50,
      chargeResistor: 1500,
      deviceId: "PDB-MOCK-001",
      wifiSSID: "PowerBoardLab",
      wireOhmPerM: 1.05,
      wireGauge: 16,
      buzzerMute: false,
      tempTripC: 90,
      tempWarnC: 80,
      idleCurrentA: 0.18,
      wireTauSec: 48,
      wireKLoss: 0.35,
      wireThermalC: 16.8,
      floorThicknessMm: 32,
      floorMaterial: "concrete",
      floorMaterialCode: 2,
      floorMaxC: 35,
      nichromeFinalTempC: 120,
      mixFrameMs: 500,
      mixRefOnMs: 160,
      mixRefResOhm: 9.5,
      mixBoostK: 1.2,
      mixBoostMs: 400,
      mixPreDeltaC: 2.5,
      mixHoldUpdateMs: 2000,
      mixHoldGain: 0.6,
      mixMinOnMs: 60,
      mixMaxOnMs: 900,
      mixMaxAvgMs: 1200,
      timingMode: "preset",
      timingProfile: "medium",
      currLimit: 12.5,
      manualMode: false,
      wireRes: makeWireRes(),
    },
    outputs: (() => {
      const out = makeOutputMap(false);
      out.output1 = true;
      out.output2 = true;
      out.output3 = true;
      return out;
    })(),
    outputAccess: makeOutputMap(true),
    relay: false,
    acPresent: true,
    fanSpeed: 35,
    logText:
      "[boot] PowerDistributionBoard mock log\n[info] Firmware: mock-1.0.0\n[info] Ready.\n",
    events: seedEvents(),
    session: {
      running: false,
      startMs: 0,
      energyWh: 0,
      durationS: 0,
      peakPowerW: 0,
      peakCurrentA: 0,
      lastUpdateMs: nowMs(),
      lastSession: sessionHistory.length ? sessionHistory[0] : null,
      history: sessionHistory,
      totals: sessionTotals,
    },
    sim: {
      lastMs: nowMs(),
      wireTemps: Array.from({ length: 10 }, () => 25),
      temps: Array.from({ length: 12 }, () => -127),
      boardTemp: 28,
      heatsinkTemp: 27,
      capVoltage: 320,
      capAdcRaw: 32.0,
      current: 0.2,
      capacitanceF: 0.012,
      activeCount: 0,
    },
    calib: {
      running: false,
      mode: "none",
      intervalMs: 500,
      startMs: 0,
      startEpoch: 0,
      samples: [],
      maxSamples: 1200,
      capacity: 2048,
      timer: null,
      targetC: 120,
      wireIndex: 1,
      saved: false,
      savedMs: 0,
      savedEpoch: 0,
      history: seedCalibHistory(),
      historyCounter: 0,
    },
    wireTest: {
      running: false,
      mode: "energy",
      purpose: "none",
      targetC: 120,
      activeWire: 1,
      ntcTempC: 25,
      activeTempC: 25,
      packetMs: 0,
      frameMs: 0,
      updatedMs: 0,
    },
  };
  function updateSimulation() {
    const now = nowMs();
    const last = state.sim.lastMs || now;
    const dt = clamp((now - last) / 1000, 0, 2);
    state.sim.lastMs = now;

    const t = now / 1000;
    const running =
      state.device.state === "Running" ||
      state.wireTest.running ||
      state.calib.running;
    const ambient = 24 + 1.2 * Math.sin(t / 30);
    const target = resolveWireTargetC();

    let activeCount = 0;
    for (let i = 1; i <= 10; i++) {
      const key = "output" + i;
      const on = !!state.outputs[key];
      if (on) activeCount++;
      const desired =
        running && on && Number.isFinite(target) ? target : ambient;
      const rate = on ? 0.9 : 0.45;
      const blend = 1 - Math.exp(-rate * dt);
      const cur = state.sim.wireTemps[i - 1];
      const next = cur + (desired - cur) * blend;
      const wobble = 0.3 * Math.sin(t * 0.7 + i);
      state.sim.wireTemps[i - 1] = clamp(next + wobble, 5, WIRE_TARGET_MAX_C);
    }

    state.sim.heatsinkTemp =
      ambient +
      (running && Number.isFinite(target) ? (target - ambient) * 0.35 : 0) +
      0.6 * Math.sin(t / 7);
    state.sim.boardTemp =
      ambient +
      (running && Number.isFinite(target) ? (target - ambient) * 0.2 : 0) +
      0.4 * Math.sin(t / 9);

    state.sim.temps[0] = state.sim.boardTemp;
    state.sim.temps[1] = state.sim.heatsinkTemp;
    state.sim.temps[2] = ambient + 0.8 * Math.sin(t / 11);
    for (let i = 3; i < state.sim.temps.length; i++) {
      state.sim.temps[i] = -127;
    }

    const baseCurrent = running ? 5 + activeCount * 0.4 : 0.15;
    const current = clamp(baseCurrent + 1.2 * Math.sin(t / 2.5), 0, 100);
    const capVoltage = 320 + 6 * Math.sin(t / 5) - current * 0.5;
    const adcRaw = Math.round((capVoltage / 400) * 4095);

    state.sim.current = current;
    state.sim.capVoltage = capVoltage;
    state.sim.capAdcRaw = adcRaw / 100;
    state.sim.capacitanceF = 0.012 + 0.0015 * Math.sin(t / 13);
    state.sim.activeCount = activeCount;
  }

  function startSession(now) {
    state.session.running = true;
    state.session.startMs = now;
    state.session.energyWh = 0;
    state.session.durationS = 0;
    state.session.peakPowerW = 0;
    state.session.peakCurrentA = 0;
    state.session.lastUpdateMs = now;
  }

  function finishSession(now) {
    const duration = Math.max(0, state.session.durationS);
    const session = {
      start_ms: state.session.startMs,
      duration_s: Math.round(duration),
      energy_Wh: Number(state.session.energyWh.toFixed(2)),
      peakPower_W: Number(state.session.peakPowerW.toFixed(1)),
      peakCurrent_A: Number(state.session.peakCurrentA.toFixed(2)),
    };
    state.session.lastSession = session;
    state.session.history.unshift(session);
    state.session.totals.totalEnergy_Wh += session.energy_Wh;
    state.session.totals.totalSessions += 1;
    state.session.totals.totalSessionsOk += 1;
    state.session.running = false;
    state.session.lastUpdateMs = now;
  }

  function updateSession(running, powerW, currentA) {
    const now = nowMs();
    if (running && !state.session.running) {
      startSession(now);
    }
    if (!running && state.session.running) {
      finishSession(now);
    }

    if (state.session.running) {
      const dt = clamp((now - state.session.lastUpdateMs) / 1000, 0, 5);
      state.session.durationS += dt;
      state.session.energyWh += (powerW * dt) / 3600;
      state.session.peakPowerW = Math.max(state.session.peakPowerW, powerW);
      state.session.peakCurrentA = Math.max(state.session.peakCurrentA, currentA);
    }
    state.session.lastUpdateMs = now;

    if (state.session.running) {
      return {
        valid: true,
        running: true,
        energy_Wh: state.session.energyWh,
        duration_s: Math.round(state.session.durationS),
        peakPower_W: state.session.peakPowerW,
        peakCurrent_A: state.session.peakCurrentA,
      };
    }
    if (state.session.lastSession) {
      const s = state.session.lastSession;
      return {
        valid: true,
        running: false,
        energy_Wh: s.energy_Wh,
        duration_s: s.duration_s,
        peakPower_W: s.peakPower_W,
        peakCurrent_A: s.peakCurrent_A,
      };
    }
    return { valid: false, running: false };
  }

  function setDeviceState(next) {
    if (state.device.state === next) return;
    const now = nowMs();
    const wasRunning = state.device.state === "Running";
    state.device.state = next;
    state.device.lastChangeMs = now;

    if (wasRunning && next !== "Running") {
      finishSession(now);
    }
    if (!wasRunning && next === "Running") {
      startSession(now);
    }

    if (next === "Shutdown") {
      state.relay = false;
      for (let i = 1; i <= 10; i++) {
        state.outputs["output" + i] = false;
      }
    }
  }

  function ensureRunOutputs() {
    const anyOn = Object.values(state.outputs).some(Boolean);
    if (anyOn) return;
    for (let i = 1; i <= 10; i++) {
      const key = "output" + i;
      if (state.outputAccess[key]) {
        state.outputs[key] = i % 2 === 1;
      }
    }
  }
  function buildLoadControls() {
    return {
      ledFeedback: state.controls.ledFeedback,
      acFrequency: state.controls.acFrequency,
      chargeResistor: state.controls.chargeResistor,
      deviceId: state.controls.deviceId,
      wifiSSID: state.controls.wifiSSID,
      wireOhmPerM: state.controls.wireOhmPerM,
      wireGauge: state.controls.wireGauge,
      buzzerMute: state.controls.buzzerMute,
      tempTripC: state.controls.tempTripC,
      tempWarnC: state.controls.tempWarnC,
      idleCurrentA: state.controls.idleCurrentA,
      wireTauSec: state.controls.wireTauSec,
      wireKLoss: state.controls.wireKLoss,
      wireThermalC: state.controls.wireThermalC,
      floorThicknessMm: state.controls.floorThicknessMm,
      floorMaterial: state.controls.floorMaterial,
      floorMaterialCode: state.controls.floorMaterialCode,
      floorMaxC: state.controls.floorMaxC,
      nichromeFinalTempC: state.controls.nichromeFinalTempC,
      mixFrameMs: state.controls.mixFrameMs,
      mixRefOnMs: state.controls.mixRefOnMs,
      mixRefResOhm: state.controls.mixRefResOhm,
      mixBoostK: state.controls.mixBoostK,
      mixBoostMs: state.controls.mixBoostMs,
      mixPreDeltaC: state.controls.mixPreDeltaC,
      mixHoldUpdateMs: state.controls.mixHoldUpdateMs,
      mixHoldGain: state.controls.mixHoldGain,
      mixMinOnMs: state.controls.mixMinOnMs,
      mixMaxOnMs: state.controls.mixMaxOnMs,
      mixMaxAvgMs: state.controls.mixMaxAvgMs,
      timingMode: state.controls.timingMode,
      timingProfile: state.controls.timingProfile,
      currLimit: state.controls.currLimit,
      capacitanceF: state.sim.capacitanceF,
      manualMode: state.controls.manualMode,
      fanSpeed: state.fanSpeed,
      relay: state.relay,
      ready: state.device.state === "Idle",
      off: state.device.state === "Shutdown",
      outputs: { ...state.outputs },
      outputAccess: { ...state.outputAccess },
      wireRes: { ...state.controls.wireRes },
    };
  }

  function buildMonitorSnapshot() {
    updateSimulation();

    const targetC = resolveWireTargetC();
    const running = state.device.state === "Running";
    const powerW = state.sim.capVoltage * state.sim.current;
    const session = updateSession(running, powerW, state.sim.current);

    const snap = {
      capVoltage: state.sim.capVoltage,
      capAdcRaw: state.sim.capAdcRaw,
      current: state.sim.current,
      capacitanceF: state.sim.capacitanceF,
      temperatures: state.sim.temps.slice(),
      boardTemp: state.sim.boardTemp,
      heatsinkTemp: state.sim.heatsinkTemp,
      wireTemps: state.sim.wireTemps.map((t) => Math.round(t)),
      ready: state.device.state === "Idle",
      off: state.device.state === "Shutdown",
      ac: state.acPresent,
      relay: state.relay || running,
      outputs: { ...state.outputs },
      fanSpeed: state.fanSpeed,
      wifiSta: true,
      wifiConnected: true,
      wifiRssi: -55,
      eventUnread: {
        warn: state.events.unread.warn,
        error: state.events.unread.error,
      },
      sessionTotals: {
        totalEnergy_Wh: state.session.totals.totalEnergy_Wh,
        totalSessions: state.session.totals.totalSessions,
        totalSessionsOk: state.session.totals.totalSessionsOk,
      },
      session,
    };

    if (Number.isFinite(targetC)) {
      snap.wireTargetC = targetC;
    }

    return snap;
  }
  function addCalibSample() {
    if (!state.calib.running) return;
    const tMs = nowMs() - state.calib.startMs;
    const sample = makeCalibSample(tMs, state.calib.samples.length, state.calib.targetC);
    state.calib.samples.push(sample);
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

  function buildCalibMeta() {
    return {
      running: state.calib.running,
      mode: state.calib.mode,
      count: state.calib.samples.length,
      capacity: state.calib.capacity,
      interval_ms: state.calib.intervalMs,
      start_ms: state.calib.startMs,
      start_epoch: state.calib.startEpoch,
      saved: state.calib.saved,
      saved_ms: state.calib.savedMs,
      saved_epoch: state.calib.savedEpoch,
      target_c: state.calib.targetC,
      wire_index: state.calib.wireIndex,
    };
  }

  function saveCalibHistory() {
    if (!state.calib.samples.length) return;
    const meta = buildCalibMeta();
    const epoch = meta.start_epoch || Math.floor(Date.now() / 1000);
    const name = `calib_${epoch}_${state.calib.historyCounter++}.json`;
    state.calib.history.unshift({
      name,
      start_epoch: epoch,
      meta,
      samples: state.calib.samples.slice(),
    });
  }

  function handleCalibStatus() {
    return buildCalibMeta();
  }

  function handleCalibData(url) {
    const params = new URL(url, window.location.origin).searchParams;
    const offset = parseInt(params.get("offset") || "0", 10);
    const count = parseInt(params.get("count") || "200", 10);
    const slice = state.calib.samples.slice(offset, offset + count);
    return {
      meta: buildCalibMeta(),
      samples: slice,
    };
  }

  function handleCalibStart(body) {
    const mode = body.mode || "model";
    const interval = body.interval_ms || 500;
    const maxSamples = body.max_samples || 1200;
    const targetC = body.target_c || 120;
    const wireIndex = body.wire_index || 1;
    const epoch = body.epoch || Math.floor(Date.now() / 1000);

    state.calib.running = true;
    state.calib.mode = mode;
    state.calib.intervalMs = interval;
    state.calib.startMs = nowMs();
    state.calib.startEpoch = epoch;
    state.calib.samples = [];
    state.calib.targetC = targetC;
    state.calib.wireIndex = wireIndex;
    state.calib.saved = false;
    state.calib.savedMs = 0;
    state.calib.savedEpoch = 0;
    state.calib.maxSamples = maxSamples;

    for (let i = 0; i < Math.min(20, maxSamples); i++) {
      addCalibSample();
    }
    startCalibTimer();

    if (mode === "model") {
      state.wireTest.running = true;
      state.wireTest.mode = "energy";
      state.wireTest.purpose = "model_cal";
      state.wireTest.targetC = targetC;
      state.wireTest.activeWire = wireIndex;
      state.wireTest.updatedMs = nowMs();
      state.outputs["output" + wireIndex] = true;
    } else {
      state.wireTest.running = false;
      state.wireTest.purpose = "none";
    }

    return { status: "ok", running: true };
  }

  function handleCalibStop() {
    stopCalibTimer();
    state.calib.running = false;
    state.calib.saved = true;
    state.calib.savedMs = nowMs();
    state.calib.savedEpoch = Math.floor(Date.now() / 1000);
    if (state.wireTest.purpose === "model_cal") {
      state.wireTest.running = false;
      state.wireTest.purpose = "none";
    }
    saveCalibHistory();
    return { status: "ok", running: false, saved: true };
  }

  function handleCalibClear() {
    stopCalibTimer();
    state.calib.running = false;
    state.calib.samples = [];
    state.calib.saved = false;
    state.calib.savedMs = 0;
    state.calib.savedEpoch = 0;
    state.calib.history = [];
    return { status: "ok", cleared: true, file_removed: true };
  }
  function handleWireTestStatus() {
    updateSimulation();
    const wt = state.wireTest;
    if (wt.running) {
      const idx = clamp(wt.activeWire || 1, 1, 10);
      wt.activeTempC = state.sim.wireTemps[idx - 1];
      wt.ntcTempC = state.sim.heatsinkTemp;
      wt.packetMs = 140 + 30 * Math.sin(nowMs() / 800);
      wt.frameMs = 320 + 40 * Math.sin(nowMs() / 900);
      wt.updatedMs = nowMs();
    } else {
      wt.activeTempC = NaN;
      wt.ntcTempC = NaN;
      wt.packetMs = 0;
      wt.frameMs = 0;
    }
    return {
      running: wt.running,
      target_c: wt.targetC,
      active_wire: wt.activeWire,
      ntc_temp_c: wt.ntcTempC,
      active_temp_c: wt.activeTempC,
      packet_ms: wt.packetMs,
      frame_ms: wt.frameMs,
      updated_ms: wt.updatedMs,
      mode: wt.mode,
      purpose: wt.purpose,
    };
  }

  function handleWireTestStart(body) {
    const target = body.target_c || 120;
    const idx = body.wire_index || 1;
    state.wireTest.running = true;
    state.wireTest.mode = "energy";
    state.wireTest.purpose = "wire_test";
    state.wireTest.targetC = target;
    state.wireTest.activeWire = idx;
    state.wireTest.updatedMs = nowMs();
    state.outputs["output" + idx] = true;
    return { status: "ok", running: true };
  }

  function handleWireTestStop() {
    state.wireTest.running = false;
    state.wireTest.purpose = "none";
    state.wireTest.packetMs = 0;
    state.wireTest.frameMs = 0;
    return { status: "ok", running: false };
  }

  function handleCalibModelSuggest() {
    const baseTau = Number(state.controls.wireTauSec) || 45;
    const baseK = Number(state.controls.wireKLoss) || 0.3;
    const baseC = Number(state.controls.wireThermalC) || baseTau * baseK;
    const maxPower = Math.max(
      200,
      ...state.calib.samples.map((s) => (s.v || 0) * (s.i || 0))
    );
    return {
      wire_tau: baseTau * 1.05,
      wire_k_loss: baseK * 0.97,
      wire_c: baseC * 1.02,
      max_power_w: maxPower,
    };
  }

  function handleCalibModelSave(body) {
    if (Number.isFinite(body.wire_tau)) state.controls.wireTauSec = body.wire_tau;
    if (Number.isFinite(body.wire_k_loss)) state.controls.wireKLoss = body.wire_k_loss;
    if (Number.isFinite(body.wire_c)) state.controls.wireThermalC = body.wire_c;
    return { status: "ok", applied: true };
  }

  function handleHistoryList() {
    return {
      items: state.calib.history.map((h) => ({
        name: h.name,
        start_epoch: h.start_epoch,
      })),
    };
  }

  function handleHistoryFile(url) {
    const params = new URL(url, window.location.origin).searchParams;
    const name = params.get("name");
    const entry = state.calib.history.find((h) => h.name === name);
    if (!entry) return { error: "not_found" };
    return {
      meta: entry.meta,
      samples: entry.samples,
    };
  }

  function handleLastEvent(url) {
    const params = new URL(url, window.location.origin).searchParams;
    if (params.get("mark_read") === "1") {
      state.events.unread.warn = 0;
      state.events.unread.error = 0;
    }
    return {
      state: state.device.state,
      last_error: state.events.last_error,
      last_stop: state.events.last_stop,
      warnings: state.events.warnings,
      errors: state.events.errors,
      unread: {
        warn: state.events.unread.warn,
        error: state.events.unread.error,
      },
    };
  }
  function handleControl(body) {
    const action = body.action;
    const target = body.target;
    const value = body.value;

    if (action === "get" && target === "status") {
      return { state: state.device.state };
    }

    if (action === "set") {
      if (target === "systemStart") {
        setDeviceState("Running");
        ensureRunOutputs();
        state.relay = true;
        return { status: "ok", applied: true, state: "Running" };
      }
      if (target === "systemShutdown") {
        setDeviceState("Shutdown");
        return { status: "ok", applied: true, state: "Shutdown" };
      }
      if (target === "abortAuto") {
        setDeviceState("Idle");
        return { status: "ok", applied: true, state: "Idle" };
      }
      if (target === "systemReset") {
        setDeviceState("Idle");
        return { status: "ok", applied: true };
      }
      if (target === "reboot") {
        setDeviceState("Shutdown");
        return { status: "ok", applied: true };
      }
      if (target === "mode") {
        state.controls.manualMode = !!value;
        return { status: "ok", applied: true };
      }
      if (target === "relay") {
        state.relay = !!value;
        return { status: "ok", applied: true };
      }
      if (target === "fanSpeed") {
        state.fanSpeed = clamp(Number(value) || 0, 0, 100);
        return { status: "ok", applied: true };
      }
      if (target === "ledFeedback") {
        state.controls.ledFeedback = !!value;
        return { status: "ok", applied: true };
      }
      if (target === "buzzerMute") {
        state.controls.buzzerMute = !!value;
        return { status: "ok", applied: true };
      }
      if (target === "userCredentials" && value) {
        if (value.newId) state.controls.deviceId = value.newId;
        return { status: "ok", applied: true };
      }
      if (target === "adminCredentials" && value) {
        if (value.wifiSSID) state.controls.wifiSSID = value.wifiSSID;
        return { status: "ok", applied: true };
      }

      const outMatch = /^output(\d+)$/i.exec(target);
      if (outMatch) {
        const idx = clamp(Number(outMatch[1]), 1, 10);
        state.outputs["output" + idx] = !!value;
        return { status: "ok", applied: true };
      }
      const accessMatch = /^Access(\d+)$/i.exec(target);
      if (accessMatch) {
        const idx = clamp(Number(accessMatch[1]), 1, 10);
        state.outputAccess["output" + idx] = !!value;
        return { status: "ok", applied: true };
      }
      const wireResMatch = /^wireRes(\d+)$/i.exec(target);
      if (wireResMatch) {
        const idx = clamp(Number(wireResMatch[1]), 1, 10);
        state.controls.wireRes[String(idx)] = Number(value);
        return { status: "ok", applied: true };
      }

      if (target === "floorMaterial") {
        const code = floorMaterialToCode(value);
        state.controls.floorMaterialCode = code;
        state.controls.floorMaterial = floorMaterialFromCode(code);
        return { status: "ok", applied: true };
      }
      if (Object.prototype.hasOwnProperty.call(state.controls, target)) {
        const num = Number(value);
        state.controls[target] = Number.isFinite(num) ? num : value;
        return { status: "ok", applied: true };
      }
    }

    return { status: "ok", applied: true };
  }

  function jsonResponse(obj, status = 200) {
    return new Response(JSON.stringify(obj), {
      status,
      headers: { "Content-Type": "application/json" },
    });
  }

  function textResponse(text, status = 200) {
    return new Response(text, {
      status,
      headers: { "Content-Type": "text/plain" },
    });
  }

  function parseBody(body) {
    if (!body) return {};
    if (typeof body === "string") {
      try {
        return JSON.parse(body);
      } catch {
        return {};
      }
    }
    return {};
  }

  async function mockFetch(input, init = {}) {
    const url = typeof input === "string" ? input : input.url;
    const method = (init.method || (typeof input === "object" && input.method) || "GET").toUpperCase();
    const parsed = new URL(url, window.location.origin);
    const path = parsed.pathname;

    try {
      if (path === "/heartbeat") {
        return jsonResponse({ ok: true });
      }
      if (path === "/disconnect" && method === "POST") {
        return jsonResponse({ status: "ok" });
      }
      if (path === "/control" && method === "POST") {
        const body = parseBody(init.body);
        return jsonResponse(handleControl(body));
      }
      if (path === "/load_controls") {
        return jsonResponse(buildLoadControls());
      }
      if (path === "/monitor") {
        return jsonResponse(buildMonitorSnapshot());
      }
      if (path === "/device_log") {
        return textResponse(state.logText || "");
      }
      if (path === "/device_log_clear" && method === "POST") {
        state.logText = "";
        return jsonResponse({ ok: true });
      }
      if (path === "/last_event") {
        return jsonResponse(handleLastEvent(url));
      }
      if (path === "/History.json") {
        return jsonResponse({ history: state.session.history });
      }

      // Calibration status/data
      if (path === "/calib_status") {
        return jsonResponse(handleCalibStatus());
      }
      if (path === "/calib_data") {
        return jsonResponse(handleCalibData(url));
      }
      if (path === "/calib_start" && method === "POST") {
        const body = parseBody(init.body);
        return jsonResponse(handleCalibStart(body));
      }
      if (path === "/calib_stop" && method === "POST") {
        return jsonResponse(handleCalibStop());
      }
      if (path === "/calib_clear" && method === "POST") {
        return jsonResponse(handleCalibClear());
      }
      if (path === "/calib_pi_suggest") {
        return jsonResponse(handleCalibModelSuggest());
      }
      if (path === "/calib_pi_save" && method === "POST") {
        const body = parseBody(init.body);
        return jsonResponse(handleCalibModelSave(body));
      }
      if (path === "/calib_history_list") {
        return jsonResponse(handleHistoryList());
      }
      if (path === "/calib_history_file") {
        const res = handleHistoryFile(url);
        if (res && res.error) {
          return jsonResponse(res, 404);
        }
        return jsonResponse(res);
      }

      // Wire test
      if (path === "/wire_test_status") {
        return jsonResponse(handleWireTestStatus());
      }
      if (path === "/wire_test_start" && method === "POST") {
        const body = parseBody(init.body);
        return jsonResponse(handleWireTestStart(body));
      }
      if (path === "/wire_test_stop" && method === "POST") {
        return jsonResponse(handleWireTestStop());
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
    console.info("[mock] Admin mock enabled.");
    return true;
  }

  window.enableCalibrationMock = enableMock;
  window.enableAdminMock = enableMock;

  // Auto-enable unless explicitly disabled.
  if (window.USE_MOCK !== false) {
    enableMock();
  }
})();
