/* mock.js — Fake ESP32 API so the UI runs without hardware.
 * Include BEFORE admin.js.
 * Toggle: set window.USE_MOCK = true|false before this file (default: true).
 */
(function () {
  const ENABLED = typeof window.USE_MOCK === "boolean" ? window.USE_MOCK : true;
  if (!ENABLED) return;

  const realFetch = window.fetch.bind(window);

  // ----- Simulated device state -----
  const S = {
    deviceState: "Shutdown", // "Shutdown" | "Idle" | "Running" | "Error"
    manualMode: false, // admin.js toggles via /control set mode
    outputs: Array(10).fill(false), // 10 outputs
    outputAccess: Array(10).fill(true),
    relay: false,
    bypass: false,
    ledFeedback: true,
    buzzerMute: false,
    fanSpeed: 0, // 0..255
    desiredVoltage: 325, // V
    acFrequency: 50, // Hz
    chargeResistor: 47, // ohm
    dcVoltage: 325, // V
    onTime: 800, // ms
    offTime: 600, // ms
    temps: new Array(12).fill(26),
    capVoltage: 0,
    current: 0,
    t: 0, // "seconds" counter (sim time)
    tick: null,
  };

  // Helpers
  function outputsMap() {
    const m = {};
    for (let i = 1; i <= 10; i++) m["output" + i] = !!S.outputs[i - 1];
    return m;
  }
  function accessMap() {
    const m = {};
    for (let i = 1; i <= 10; i++) m["output" + i] = !!S.outputAccess[i - 1];
    return m;
  }

  // ----- Simulation step (every 250ms) -----
  function step() {
    S.t += 0.25;

    if (S.deviceState === "Running" && !S.manualMode) {
      S.relay = true;

      // Cap voltage swings between ~0.5..1.0 of desired
      const v = S.desiredVoltage;
      S.capVoltage = +(v * (0.55 + 0.45 * Math.sin(S.t / 2))).toFixed(2);

      // Current has a soft ripple
      S.current = +(5 + 3 * Math.sin(S.t * 1.8)).toFixed(2);

      // Chase animation across outputs
      const idx = Math.floor((S.t * 2) % 10);
      S.outputs = S.outputs.map((_, i) => i === idx || i === (idx + 1) % 10);

      // Fan breathes  <-- FIXED: removed one extra ')'
      S.fanSpeed = Math.max(
        0,
        Math.min(255, Math.floor(128 + 100 * Math.sin(S.t)))
      );
    } else if (S.deviceState === "Idle" || S.manualMode) {
      // Hold-ish state, slight bleed
      S.capVoltage = Math.max(0, +(S.capVoltage - 2).toFixed(2));
      S.current = +(0.2 + 0.1 * Math.sin(S.t)).toFixed(2);
      S.fanSpeed = Math.floor(S.capVoltage / 3);
      // No auto output toggling in manual/idle
    } else {
      // Shutdown
      S.relay = false;
      S.capVoltage = Math.max(0, +(S.capVoltage - 5).toFixed(2));
      S.current = 0;
      S.fanSpeed = 0;
      S.outputs = Array(10).fill(false);
    }

    // Temperatures with gentle offsets
    S.temps = S.temps.map((_, i) => {
      const base = 26 + (i % 3) * 2;
      return +(base + 0.5 * Math.sin(S.t + i)).toFixed(2);
    });
  }
  S.tick = setInterval(step, 250);

  // ----- Response helpers -----
  const J = (obj, code = 200) =>
    new Response(JSON.stringify(obj), {
      status: code,
      headers: { "Content-Type": "application/json" },
    });
  const T = (text, code = 200, type = "text/plain") =>
    new Response(text, { status: code, headers: { "Content-Type": type } });

  // ----- Mocked endpoints -----
  window.fetch = async (input, init = {}) => {
    const url = typeof input === "string" ? input : input.url;
    const method = (init.method || "GET").toUpperCase();

    // Compact /live (optional, for fast polling)
    if (url.endsWith("/live") && method === "GET") {
      const arr = S.outputs.slice();
      let mask = 0,
        allowMask = 0;
      arr.forEach((on, i) => {
        if (on) mask |= 1 << i;
        if (S.outputAccess[i]) allowMask |= 1 << i;
      });
      return J({
        state: S.deviceState,
        relay: S.relay,
        cap: S.capVoltage,
        curr: S.current,
        outs: arr,
        mask,
        allowMask,
        ac: S.capVoltage > 10,
      });
    }

    // /monitor (GET) — live gauges & overlay dots
    if (url.endsWith("/monitor") && method === "GET") {
      return J({
        capVoltage: S.capVoltage,
        current: S.current,
        temperatures: S.temps,
        relay: S.relay,
        fanSpeed: S.fanSpeed,
        ready: S.deviceState === "Running",
        off: S.deviceState === "Shutdown",
        outputs: outputsMap(),
      });
    }

    // /load_controls (GET) — initial config + toggles
    if (url.endsWith("/load_controls") && method === "GET") {
      return J({
        ledFeedback: S.ledFeedback,
        buzzerMute: S.buzzerMute,
        ready: S.deviceState === "Running",
        off: S.deviceState === "Shutdown",
        desiredVoltage: S.desiredVoltage,
        acFrequency: S.acFrequency,
        chargeResistor: S.chargeResistor,
        dcVoltage: S.dcVoltage,
        onTime: S.onTime,
        offTime: S.offTime,
        outputs: outputsMap(),
        outputAccess: accessMap(),
        wireRes: {
          1: 44,
          2: 44,
          3: 44,
          4: 44,
          5: 44,
          6: 44,
          7: 44,
          8: 44,
          9: 44,
          10: 44,
        },
        targetRes: 44,
      });
    }

    // /control (POST)
    if (url.endsWith("/control") && method === "POST") {
      let payload = {};
      try {
        payload = JSON.parse(init.body || "{}");
      } catch {}
      const { action, target, value } = payload || {};

      // ---- GETs ----
      if (action === "get") {
        if (target === "status") return J({ state: S.deviceState }); // used by power button
        if (target === "relay") return J({ value: !!S.relay }); // sometimes used as fallback
        if (/^output\d{1,2}$/.test(target)) {
          const i = parseInt(target.slice(6), 10) - 1;
          return J({ value: !!S.outputs[i] });
        }
        return J({ error: "unsupported get" }, 400);
      }

      // ---- SETs ----
      if (action === "set") {
        let ok = true;
        switch (target) {
          case "systemStart":
            S.deviceState = "Running";
            break;
          case "systemShutdown":
            S.deviceState = "Shutdown";
            break;
          case "abortAuto":
            S.deviceState = "Idle";
            break;
          case "mode":
            S.manualMode = !!value;
            break;
          case "relay":
            S.relay = !!value;
            break;
          case "bypass":
            S.bypass = !!value;
            break;
          case "fanSpeed":
            S.fanSpeed = Math.max(0, Math.min(255, +value || 0));
            break;
          case "buzzerMute":
            S.buzzerMute = !!value;
            break;
          case "ledFeedback":
            S.ledFeedback = !!value;
            break;

          case "desiredVoltage":
          case "acFrequency":
          case "chargeResistor":
          case "dcVoltage":
          case "onTime":
          case "offTime":
            S[target] = +value;
            break;

          // Per-output & access
          default:
            if (/^output\d{1,2}$/.test(target)) {
              const i = parseInt(target.slice(6), 10) - 1;
              if (i >= 0 && i < 10) S.outputs[i] = !!value;
            } else if (/^Access\d{1,2}$/.test(target)) {
              const i = parseInt(target.slice(6), 10) - 1;
              if (i >= 0 && i < 10) S.outputAccess[i] = !!value;
            } else if (/^R\d{2}OHM$/.test(target) || target === "R0XTGT") {
              // accepted (nichrome demo); no state impact needed for UI preview
            } else {
              ok = false;
            }
        }
        return ok ? J({ status: "ok" }) : J({ error: "unknown target" }, 400);
      }

      return J({ error: "unsupported action" }, 400);
    }

    // /heartbeat (GET)
    if (url.endsWith("/heartbeat") && method === "GET") {
      return T("alive");
    }

    // Credentials/Disconnect stubs (optional)
    if (url.endsWith("/SetUserCred") && method === "POST")
      return J({ status: "User cred updated (mock)" });
    if (url.endsWith("/SetAdminCred") && method === "POST")
      return J({ status: "Admin cred updated (mock)" });
    if (url.endsWith("/disconnect") && method === "POST")
      return J({ status: "ok" });

    // Everything else → real network
    return realFetch(input, init);
  };

  console.log(
    "%cMock API enabled",
    "padding:2px 6px;border-radius:4px;background:#1f2937;color:#22d3ee"
  );
})();
