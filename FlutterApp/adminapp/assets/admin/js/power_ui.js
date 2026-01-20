  // ========================================================
  // ===============        POWER BUTTON UI      ============
  // ========================================================

  function setPowerUI(state, extras = {}) {
    const btn = powerEl();
    const labelEl = powerText();
    if (!btn || !labelEl) {
      lastState = state || lastState;
      updateStatusBarState();
      return;
    }

    btn.classList.remove(
      "state-off",
      "state-idle",
      "state-ready",
      "state-error"
    );

    let label = (state || "Shutdown").toUpperCase();
    let cls = "state-off";

    if (state === "Shutdown") {
      label = "OFF";
      cls = "state-off";
    } else if (state === "Idle") {
      label = "IDLE";
      cls = "state-idle";
    } else if (state === "Running") {
      label = extras.ready === true ? "READY" : "RUN";
      cls = "state-ready";
    } else if (state === "Error") {
      label = "ERROR";
      cls = "state-error";
    }

    btn.classList.add(cls);
    labelEl.textContent = label;
    lastState = state;
    updateStatusBarState();
  }

  function onPowerClick() {
    if (lastState === "Shutdown") {
      startSystem();
    } else {
      shutdownSystem();
    }
  }

  async function pollDeviceState() {
    try {
      const res = await fetch("/control", {
        method: "POST",
        headers: cborHeaders(),
        body: encodeCbor({ action: "get", target: "status" }),
      });
      if (res.status === 401) {
        noteAuthFailure();
        return;
      }
      if (!res.ok) return;
      resetAuthFailures();
      const data = await readCbor(res, {});
      if (data && data.state) {
        setPowerUI(data.state);
      }
    } catch (err) {
      console.warn("Status poll failed:", err);
    }
  }

  function startStatePolling() {
    if (statePollTimer) return;
    pollDeviceState();
    statePollTimer = setInterval(pollDeviceState, 2000);
  }

  function stopStatePolling() {
    if (statePollTimer) {
      clearInterval(statePollTimer);
      statePollTimer = null;
    }
  }

  function startStateStream() {
    if (stateStream) return;
    try {
      stateStream = new EventSource("/state_stream");
      let gotStateEvent = false;

      const handleStateEvent = (ev) => {
        try {
          const data = decodeSsePayload(ev) || {};
          if (data.state) {
            setPowerUI(data.state);
            if (!gotStateEvent) {
              stopStatePolling();
              gotStateEvent = true;
            }
          }
        } catch (e) {
          console.warn("State stream parse error:", e);
        }
      };

      // Server emits event name "state"
      stateStream.addEventListener("state", handleStateEvent);
      // Fallback if server ever sends unnamed messages
      stateStream.onmessage = handleStateEvent;

      stateStream.onerror = () => {
        if (stateStream) {
          stateStream.close();
          stateStream = null;
        }
        startStatePolling();
      };
    } catch (err) {
      console.warn("State stream failed to start:", err);
      startStatePolling();
    }
  }

  function startEventStream() {
    if (eventStream) return;
    try {
      eventStream = new EventSource("/event_stream");
      const handleEvent = (ev) => {
        try {
          const data = decodeSsePayload(ev) || {};
          if (data && data.unread) {
            handleEventNotice(data);
          }
        } catch (e) {
          console.warn("Event stream parse error:", e);
        }
      };
      eventStream.addEventListener("event", handleEvent);
      eventStream.onmessage = handleEvent;
      eventStream.onerror = () => {
        if (eventStream) {
          eventStream.close();
          eventStream = null;
        }
      };
    } catch (err) {
      console.warn("Event stream failed to start:", err);
    }
  }

  function initPowerButton() {
    const btn = powerEl();
    if (btn) {
      btn.addEventListener("click", onPowerClick);
    }
    // Initial state via SSE (with polling fallback)
    startStateStream();
    startEventStream();
    startStatePolling(); // will be stopped when SSE opens
  }

