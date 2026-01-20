  // ========================================================
  // ===============      CONTROL ENDPOINT     ==============
  // ========================================================

  /**
   * Core helper for /control endpoint.
   * @param {string} action
   * @param {string} target
   * @param {any} [value]
   * @returns {Promise<any>}
   */
  async function sendControlCommand(action, target, value) {
    const payload = { action, target };
    if (value !== undefined) payload.value = value;
    if (action === "set") {
      payload.epoch = Math.floor(Date.now() / 1000);
    }

    try {
      const res = await fetch("/control", {
        method: "POST",
        headers: cborHeaders(),
        body: encodeCbor(payload),
      });

      const data = await readCbor(res, {});

      if (res.status === 401) {
        noteAuthFailure();
      }
      if (!res.ok) {
        console.warn("Control error:", res.status, data.error || data);
        return { error: data.error || "HTTP " + res.status };
      }
      resetAuthFailures();

      const applied = data.applied === true || data.status === "ok";
      if (applied) {
        console.log(`[ack] ${action} '${target}' -> applied`);
        return { ok: true, ...data };
      }

      if (data.state) {
        console.log("[state] State:", data.state);
        return { ok: true, state: data.state };
      }

      const errMsg = data.error || "unknown_error";
      console.warn("[ack-fail]", errMsg);
      return { error: errMsg };
    } catch (err) {
      console.error("Control request failed:", err);
      return { error: String(err) };
    }
  }

  function setMuteUI(muted) {
    const btn = document.getElementById("muteBtn");
    const icon = document.getElementById("muteIcon");
    if (!btn || !icon) return;

    btn.classList.toggle("muted", !!muted);
    icon.src = muted ? "icons/mute-2-256.png" : "icons/volume-up-4-256.png";
    icon.alt = muted ? "Muted" : "Sound";
  }

  async function toggleMute() {
    isMuted = !isMuted;
    setMuteUI(isMuted);
    await sendControlCommand("set", "buzzerMute", isMuted);
  }

