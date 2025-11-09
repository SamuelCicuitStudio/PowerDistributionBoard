(function () {
  if (window.__ADMIN_JS_LOADED__) {
    console.warn("admin.js already loaded; skipping duplicate execution.");
    return;
  }
  window.__ADMIN_JS_LOADED__ = true;

  ("use strict");

  // ========================================================
  // ===============        TAB SWITCHING        ============
  // ========================================================
  const tabs = document.querySelectorAll(".tab");
  const contents = document.querySelectorAll(".content");

  function switchTab(index) {
    tabs.forEach((tab, i) => {
      const isActive = i === index;
      tab.classList.toggle("active", isActive);
      if (contents[i]) {
        contents[i].classList.toggle("active", isActive);
      }
    });
  }

  // ========================================================
  // ===============   GLOBAL HELPERS / STATE   =============
  // ========================================================

  const powerEl = () => document.getElementById("powerButton");
  const powerText = () => document.getElementById("powerLabel");

  let lastState = "Shutdown"; // Device state from backend
  let pendingConfirm = null; // For confirm modal
  let lastLoadedControls = null; // Snapshot from /load_controls
  let isMuted = false; // Buzzer mute cached state

  function isManualMode() {
    const t = document.getElementById("modeToggle");
    return !!(t && t.checked);
  }

  function sleep(ms) {
    return new Promise((resolve) => setTimeout(resolve, ms));
  }

  function approxEqual(a, b, eps = 0.05) {
    const na = Number(a);
    const nb = Number(b);
    if (!Number.isFinite(na) || !Number.isFinite(nb)) return false;
    return Math.abs(na - nb) <= eps;
  }

  function setModeDot(isManual) {
    const dot = document.querySelector(".status-dot");
    if (!dot) return;
    const color = isManual ? "#ffa500" : "#00ff80";
    dot.title = isManual ? "Manual Mode" : "Auto Mode";
    dot.style.backgroundColor = color;
    dot.style.boxShadow = "0 0 6px " + color;
  }

  function setField(id, val) {
    const el = document.getElementById(id);
    if (!el) return;
    if (val === undefined || val === null || Number.isNaN(val)) {
      el.value = "";
    } else {
      el.value = val;
    }
  }

  function getFloat(id) {
    const el = document.getElementById(id);
    if (!el) return undefined;
    const v = parseFloat(el.value);
    return Number.isFinite(v) ? v : undefined;
  }

  function getInt(id) {
    const el = document.getElementById(id);
    if (!el) return undefined;
    const v = parseInt(el.value, 10);
    return Number.isFinite(v) ? v : undefined;
  }

  // ========================================================
  // ===============        CONFIRM MODAL        ============
  // ========================================================

  function openConfirm(kind) {
    pendingConfirm = kind || null;

    const modal = document.getElementById("confirmModal");
    const title = document.getElementById("confirmTitle");
    const message = document.getElementById("confirmMessage");
    const okBtn = document.getElementById("confirmOkBtn");
    const cancelBtn = document.getElementById("confirmCancelBtn");

    if (!modal || !title || !message || !okBtn) return;

    // Always show cancel for confirmations
    if (cancelBtn) cancelBtn.style.display = "";

    okBtn.classList.remove("danger", "warning", "success");

    if (kind === "reset") {
      title.textContent = "Confirm Reset";
      message.textContent = "This will reset the device (soft reset). Proceed?";
      okBtn.textContent = "Yes, Reset";
      okBtn.classList.add("danger");
    } else if (kind === "reboot") {
      title.textContent = "Confirm Reboot";
      message.textContent = "The device will reboot. Continue?";
      okBtn.textContent = "Yes, Reboot";
      okBtn.classList.add("danger");
    } else {
      title.textContent = "Confirm Action";
      message.textContent = "Are you sure?";
      okBtn.textContent = "Confirm";
      okBtn.classList.add("warning");
    }

    modal.style.display = "flex";
  }

  function closeConfirm() {
    const modal = document.getElementById("confirmModal");
    if (modal) modal.style.display = "none";
    pendingConfirm = null;
  }

  function bindConfirmModal() {
    const modal = document.getElementById("confirmModal");
    const okBtn = document.getElementById("confirmOkBtn");
    const cancelBtn = document.getElementById("confirmCancelBtn");

    if (!modal || !okBtn) return;

    if (cancelBtn) {
      cancelBtn.addEventListener("click", () => {
        pendingConfirm = null;
        closeConfirm();
      });
    }

    okBtn.addEventListener("click", () => {
      if (!pendingConfirm) {
        closeConfirm();
        return;
      }

      if (pendingConfirm === "reset") {
        resetSystem();
      } else if (pendingConfirm === "reboot") {
        rebootSystem();
      }

      pendingConfirm = null;
      closeConfirm();
    });

    // Click outside to close
    modal.addEventListener("click", (e) => {
      if (e.target === modal) {
        pendingConfirm = null;
        closeConfirm();
      }
    });

    // ESC key closes
    document.addEventListener("keydown", (e) => {
      if (e.key === "Escape") {
        pendingConfirm = null;
        closeConfirm();
      }
    });
  }

  // Generic alert that reuses confirm modal with no pendingConfirm
  function openAlert(title, message, variant = "warning") {
    pendingConfirm = null;

    const modal = document.getElementById("confirmModal");
    const titleEl = document.getElementById("confirmTitle");
    const messageEl = document.getElementById("confirmMessage");
    const okBtn = document.getElementById("confirmOkBtn");
    const cancelBtn = document.getElementById("confirmCancelBtn");

    if (!modal || !titleEl || !messageEl || !okBtn) return;

    // Hide cancel for alerts
    if (cancelBtn) cancelBtn.style.display = "none";

    titleEl.textContent = title || "Notice";
    messageEl.textContent = message || "";
    okBtn.textContent = "OK";

    okBtn.classList.remove("danger", "warning", "success");
    if (variant === "danger") okBtn.classList.add("danger");
    else if (variant === "success") okBtn.classList.add("success");
    else okBtn.classList.add("warning");

    modal.style.display = "flex";
  }

  // ========================================================
  // ===============     MANUAL TAKEOVER LOGIC    ===========
  // ========================================================

  /**
   * Ensure manual mode is asserted and auto loop is not Running.
   * Call this before sending ANY manual-changing command.
   */
  async function ensureManualTakeover(source) {
    // 1) Flip UI + backend to manual if needed
    if (!isManualMode()) {
      const t = document.getElementById("modeToggle");
      if (t) t.checked = true;
      setModeDot(true);
      await sendControlCommand("set", "mode", true);
    }

    // 2) If auto loop was Running, try abort; fallback to shutdown
    if (lastState === "Running") {
      const resp = await sendControlCommand("set", "abortAuto", true);
      if (!resp || resp.error) {
        await shutdownSystem();
      }
      setPowerUI("Idle");
    }

    console.log("Manual takeover by:", source || "unknown");
  }

  // ========================================================
  // ===============        POWER BUTTON UI      ============
  // ========================================================

  function setPowerUI(state, extras = {}) {
    const btn = powerEl();
    const labelEl = powerText();
    if (!btn || !labelEl) {
      lastState = state || lastState;
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
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ action: "get", target: "status" }),
      });
      if (!res.ok) return;
      const data = await res.json();
      if (data && data.state) {
        setPowerUI(data.state);
      }
    } catch (err) {
      console.warn("Status poll failed:", err);
    }
  }

  function initPowerButton() {
    const btn = powerEl();
    if (btn) {
      btn.addEventListener("click", onPowerClick);
    }
    // Initial state + periodic refresh
    pollDeviceState();
    setInterval(pollDeviceState, 1000);
  }

  // ========================================================
  // ===============  USER / ADMIN MENU & TABS  =============
  // ========================================================

  function toggleUserMenu() {
    const menu = document.getElementById("userMenu");
    if (!menu) return;
    menu.style.display = menu.style.display === "block" ? "none" : "block";
  }

  document.addEventListener("click", (e) => {
    const menu = document.getElementById("userMenu");
    const icon = document.querySelector(".user-icon");
    if (!menu || !icon) return;
    if (!icon.contains(e.target) && !menu.contains(e.target)) {
      menu.style.display = "none";
    }
  });

  // ========================================================
  // ===============  OUTPUT RENDERING HELPERS  =============
  // ========================================================

  function renderAllOutputs(containerId, isControlMode) {
    const container = document.getElementById(containerId);
    if (!container) return;

    container.innerHTML = "";

    for (let i = 1; i <= 10; i++) {
      const block = document.createElement("div");
      block.className = "manual-item";
      if (!isControlMode) block.classList.add("access-style");

      if (isControlMode) {
        block.innerHTML = `
          <span>Output ${i}</span>
          <label class="switch">
            <input type="checkbox" onchange="handleOutputToggle(${i}, this)">
            <span class="slider"></span>
          </label>
          <div class="led" id="manualLed${i}"></div>
        `;
      } else {
        block.innerHTML = `
          <span>Allow Output ${i}</span>
          <label class="switch">
            <input type="checkbox" id="accessToggle${i}" onchange="updateOutputAccess(${i}, this.checked)">
            <span class="slider"></span>
          </label>
        `;
      }

      container.appendChild(block);
    }
  }

  function enableDragScroll(containerId) {
    const container = document.getElementById(containerId);
    if (!container) return;

    let isDown = false;
    let startX = 0;
    let scrollLeft = 0;

    container.style.scrollBehavior = "smooth";

    container.addEventListener("mousedown", (e) => {
      isDown = true;
      container.classList.add("dragging");
      startX = e.pageX - container.offsetLeft;
      scrollLeft = container.scrollLeft;
    });

    ["mouseleave", "mouseup"].forEach((evt) => {
      container.addEventListener(evt, () => {
        isDown = false;
        container.classList.remove("dragging");
      });
    });

    container.addEventListener("mousemove", (e) => {
      if (!isDown) return;
      e.preventDefault();
      const x = e.pageX - container.offsetLeft;
      const walk = (x - startX) * 2;
      container.scrollLeft = scrollLeft - walk;
    });

    container.addEventListener(
      "wheel",
      (e) => {
        if (e.deltaY !== 0) {
          e.preventDefault();
          container.scrollLeft += e.deltaY * 0.3;
        }
      },
      { passive: false }
    );
  }

  // ========================================================
  // ===============      MODE / LT TOGGLES      ============
  // ========================================================

  async function toggleMode() {
    const isManual = isManualMode();
    setModeDot(isManual);

    await sendControlCommand("set", "mode", isManual);

    if (isManual && lastState === "Running") {
      const r = await sendControlCommand("set", "abortAuto", true);
      if (!r || r.error) await shutdownSystem();
      setPowerUI("Idle");
    }
  }

  function toggleLT() {
    const ltToggle = document.getElementById("ltToggle");
    const isOn = !!(ltToggle && ltToggle.checked);
    sendControlCommand("set", "ledFeedback", isOn);
  }

  // ========================================================
  // ===============    MANUAL OUTPUT CONTROL    ============
  // ========================================================

  async function handleOutputToggle(index, checkbox) {
    await ensureManualTakeover("output" + index);
    const isOn = !!checkbox.checked;
    const led = checkbox.parentElement.nextElementSibling;
    if (led) led.classList.toggle("active", isOn);
    sendControlCommand("set", "output" + index, isOn);
  }

  function updateOutputAccess(index, newState) {
    sendControlCommand("set", "Access" + index, !!newState);
  }

  function toggleOutput(index, state) {
    sendControlCommand("set", "output" + index, !!state);
  }

  // ========================================================
  // ===============     SYSTEM CONTROL CMDS     ============
  // ========================================================

  async function startSystem() {
    if (isManualMode()) {
      openAlert(
        "Manual mode is ON",
        "Switch to Auto before starting the cycle.",
        "warning"
      );
      return;
    }
    await sendControlCommand("set", "systemStart", true);
  }

  async function shutdownSystem() {
    await sendControlCommand("set", "systemShutdown", true);
  }

  async function resetSystem() {
    await sendControlCommand("set", "systemReset", true);
  }

  async function rebootSystem() {
    await sendControlCommand("set", "reboot", true);
  }

  // ========================================================
  // ===============       HEARTBEAT / LOGIN     ============
  // ========================================================

  function startHeartbeat(intervalMs = 1500) {
    setInterval(() => {
      fetch("/heartbeat")
        .then((res) => res.text())
        .then((text) => {
          if (text !== "alive") {
            console.warn("Unexpected heartbeat:", text);
            window.location.href = "http://192.168.4.1/login";
          }
        })
        .catch((err) => {
          console.error("Heartbeat error:", err);
          window.location.href = "http://192.168.4.1/login";
        });
    }, intervalMs);
  }

  function disconnectDevice() {
    fetch("/disconnect", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ action: "disconnect" }),
      redirect: "follow",
    })
      .then((response) => {
        if (response.redirected) {
          window.location.href = response.url;
        } else {
          return response.json().then((data) => {
            openAlert("Disconnect", data.error || "Unexpected response");
          });
        }
      })
      .catch((err) => {
        console.error("Disconnect failed:", err);
        window.location.href = "/login.html";
      });
  }

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

    try {
      const res = await fetch("/control", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(payload),
      });

      let data = {};
      try {
        data = await res.json();
      } catch {
        data = {};
      }

      if (!res.ok) {
        console.warn("Control error:", res.status, data.error || data);
        return { error: data.error || "HTTP " + res.status };
      }

      if (data.status === "ok" || data.status === "queued") {
        console.log(`[✔] ${action} '${target}' -> ${data.status}`);
      } else if (data.state) {
        console.log("[ℹ] State:", data.state);
      } else if (data.error) {
        console.warn("[✖]", data.error);
      }

      return data;
    } catch (err) {
      console.error("Control request failed:", err);
      return { error: String(err) };
    }
  }

  // ========================================================
  // ===============         MUTE BUTTON        =============
  // ========================================================

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

  // ========================================================
  // ===============  DEVICE + NICHROME SETTINGS ============
  // ========================================================

  async function waitUntilApplied(expected, timeoutMs = 2000, stepMs = 120) {
    const deadline = Date.now() + timeoutMs;

    while (Date.now() < deadline) {
      try {
        const res = await fetch("/load_controls", { cache: "no-store" });
        if (!res.ok) break;
        const data = await res.json();

        let ok = true;

        if (expected.targetRes != null) {
          if (!approxEqual(data.targetRes, expected.targetRes, 0.1)) {
            ok = false;
          }
        }

        if (expected.wireRes) {
          const wr = data.wireRes || {};
          for (const [idx, val] of Object.entries(expected.wireRes)) {
            if (!approxEqual(wr[String(idx)], val, 0.05)) {
              ok = false;
              break;
            }
          }
        }

        if (ok) {
          await loadControls();
          return true;
        }
      } catch (err) {
        console.warn("waitUntilApplied error:", err);
        break;
      }

      await sleep(stepMs);
    }

    await loadControls();
    return false;
  }

  async function saveDeviceAndNichrome() {
    const cmds = [];
    const expected = { wireRes: {}, targetRes: null };

    // Core device settings
    const desiredV = getFloat("desiredVoltage");
    if (desiredV !== undefined) cmds.push(["set", "desiredVoltage", desiredV]);

    const acFreq = getInt("acFrequency");
    if (acFreq !== undefined) cmds.push(["set", "acFrequency", acFreq]);

    const chargeR = getFloat("chargeResistor");
    if (chargeR !== undefined) cmds.push(["set", "chargeResistor", chargeR]);

    const dcV = getFloat("dcVoltage");
    if (dcV !== undefined) cmds.push(["set", "dcVoltage", dcV]);

    const onTime = getInt("onTime");
    if (onTime !== undefined) cmds.push(["set", "onTime", onTime]);

    const offTime = getInt("offTime");
    if (offTime !== undefined) cmds.push(["set", "offTime", offTime]);

    // Wire resistances R01..R10 -> wireRes1..wireRes10
    for (let i = 1; i <= 10; i++) {
      const id = "r" + String(i).padStart(2, "0") + "ohm";
      const val = getFloat(id);
      if (val !== undefined) {
        cmds.push(["set", "wireRes" + i, val]);
        expected.wireRes[String(i)] = val;
      }
    }

    // Target resistance
    const tgt = getFloat("rTarget");
    if (tgt !== undefined) {
      cmds.push(["set", "targetRes", tgt]);
      expected.targetRes = tgt;
    }

    const wireOhmPerM = getFloat("wireOhmPerM");
    if (wireOhmPerM !== undefined) {
      cmds.push(["set", "wireOhmPerM", wireOhmPerM]);
    }

    // Send sequentially to preserve ordering
    for (const [a, t, v] of cmds) {
      await sendControlCommand(a, t, v);
    }

    await waitUntilApplied(expected, 2000, 120);
  }

  function resetDeviceAndNichrome() {
    if (!lastLoadedControls) {
      loadControls();
      return;
    }

    const data = lastLoadedControls;

    setField("desiredVoltage", data.desiredVoltage);
    setField("acFrequency", data.acFrequency);
    setField("chargeResistor", data.chargeResistor);
    setField("dcVoltage", data.dcVoltage);
    setField("onTime", data.onTime);
    setField("offTime", data.offTime);
    if (data.wireOhmPerM !== undefined) {
      setField("wireOhmPerM", data.wireOhmPerM);
    }

    const wr = data.wireRes || {};
    for (let i = 1; i <= 10; i++) {
      const key = String(i);
      setField("r" + key.padStart(2, "0") + "ohm", wr[key]);
    }

    setField("rTarget", data.targetRes);
  }

  // ========================================================
  // ===============        LOAD CONTROLS        ============
  // ========================================================

  async function loadControls() {
    try {
      const res = await fetch("/load_controls", { cache: "no-store" });
      if (!res.ok) throw new Error("HTTP " + res.status);
      const data = await res.json();

      console.log("Fetched /load_controls:", data);

      lastLoadedControls = data;

      // LT toggle
      const ltToggle = document.getElementById("ltToggle");
      if (ltToggle) ltToggle.checked = !!data.ledFeedback;

      // Buzzer mute initial sync (accept bool, 0/1, "true"/"false")
      if (data.buzzerMute !== undefined) {
        const muted =
          data.buzzerMute === true ||
          data.buzzerMute === 1 ||
          data.buzzerMute === "1" ||
          data.buzzerMute === "true";
        isMuted = muted;
        setMuteUI(isMuted);
        console.log(
          "[Mute] State from /load_controls:",
          data.buzzerMute,
          "=> isMuted =",
          isMuted
        );
      }

      // Ready / Off LEDs
      const readyLed = document.getElementById("readyLed");
      const offLed = document.getElementById("offLed");
      if (readyLed)
        readyLed.style.backgroundColor = data.ready ? "limegreen" : "gray";
      if (offLed) offLed.style.backgroundColor = data.off ? "red" : "gray";

      // Device numeric fields
      setField("desiredVoltage", data.desiredVoltage);
      setField("acFrequency", data.acFrequency);
      setField("chargeResistor", data.chargeResistor);
      setField("dcVoltage", data.dcVoltage);
      setField("onTime", data.onTime);
      setField("offTime", data.offTime);
      if (data.wireOhmPerM !== undefined) {
        setField("wireOhmPerM", data.wireOhmPerM);
      }

      // Nichrome R01..R10 from wireRes{1..10}
      const wireRes = data.wireRes || {};
      for (let i = 1; i <= 10; i++) {
        const v = wireRes[String(i)];
        setField("r" + String(i).padStart(2, "0") + "ohm", v);
      }

      // Target
      setField("rTarget", data.targetRes);

      // Output states
      const states = data.outputs || {};
      for (let i = 1; i <= 10; i++) {
        const checked = !!states["output" + i];
        const itemSel = "#manualOutputs .manual-item:nth-child(" + i + ")";
        const checkbox = document.querySelector(
          itemSel + ' input[type="checkbox"]'
        );
        const led = document.querySelector(itemSel + " .led");
        if (checkbox) checkbox.checked = checked;
        if (led) led.classList.toggle("active", checked);
      }

      // Output Access flags
      const access = data.outputAccess || {};
      for (let i = 1; i <= 10; i++) {
        const checkbox = document.querySelector(
          "#userAccessGrid .manual-item:nth-child(" +
            i +
            ') input[type="checkbox"]'
        );
        if (checkbox) checkbox.checked = !!access["output" + i];
      }

      // Relay mirror
      const relayFromServer = !!data.relay;
      const relayToggle = document.getElementById("relayToggle");
      if (relayToggle) relayToggle.checked = relayFromServer;
      if (typeof setDot === "function") setDot("relay", relayFromServer);

      // Apply power button look from LEDs if desired
      applyReadyOffFlagsToPower(data.ready, data.off);
    } catch (err) {
      console.error("Failed to load controls:", err);
    }
  }

  function applyReadyOffFlagsToPower(readyBool, offBool) {
    if (offBool) {
      setPowerUI("Shutdown", { ready: false });
    } else if (lastState === "Running") {
      setPowerUI("Running", { ready: !!readyBool });
    }
  }

  // ========================================================
  // ===============    USER / ADMIN SETTINGS    ============
  // ========================================================

  function saveUserSettings() {
    const current = document.getElementById("userCurrentPassword").value;
    const newPass = document.getElementById("userNewPassword").value;
    const newId = document.getElementById("userDeviceId").value;

    sendControlCommand("set", "userCredentials", {
      current,
      newPass,
      newId,
    }).then((res) => {
      if (res && !res.error) {
        openAlert("User Settings", "User credentials updated.", "success");
        resetUserSettings();
      } else if (res && res.error) {
        openAlert("User Settings", res.error, "danger");
      }
    });
  }

  function resetUserSettings() {
    setField("userCurrentPassword", "");
    setField("userNewPassword", "");
    setField("userDeviceId", "");
  }

  function saveAdminSettings() {
    const current = document.getElementById("adminCurrentPassword").value;
    const username = document.getElementById("adminUsername").value;
    const password = document.getElementById("adminPassword").value;
    const wifiSSID = document.getElementById("wifiSSID").value;
    const wifiPassword = document.getElementById("wifiPassword").value;

    sendControlCommand("set", "adminCredentials", {
      current,
      username,
      password,
      wifiSSID,
      wifiPassword,
    }).then((res) => {
      if (res && !res.error) {
        openAlert("Admin Settings", "Admin settings updated.", "success");
        resetAdminSettings();
      } else if (res && res.error) {
        openAlert("Admin Settings", res.error, "danger");
      }
    });
  }

  function resetAdminSettings() {
    setField("adminCurrentPassword", "");
    setField("adminUsername", "");
    setField("adminPassword", "");
    setField("wifiSSID", "");
    setField("wifiPassword", "");
  }

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
    const offset = 2.6;
    const l = 3;
    const h = 2.5;

    LIVE.markers = [
      // AC (red) bottom-left
      {
        id: "ac",
        color: "red",
        x: 8 + h,
        y: 81 + offset,
        ax: 16 + l,
        ay: 81 + offset,
      },
      // Relay (yellow) bottom-right
      {
        id: "relay",
        color: "yellow",
        x: 92 - h,
        y: 81 + offset,
        ax: 84 - l,
        ay: 81 + offset,
      },
      // Left side outputs 6..10
      {
        id: "o6",
        color: "cyan",
        x: 8 + h,
        y: 11 + offset,
        ax: 13 + l,
        ay: 11 + offset,
      },
      {
        id: "o7",
        color: "cyan",
        x: 8 + h,
        y: 23 + offset,
        ax: 13 + l,
        ay: 23 + offset,
      },
      {
        id: "o8",
        color: "cyan",
        x: 8 + h,
        y: 35 + offset,
        ax: 13 + l,
        ay: 35 + offset,
      },
      {
        id: "o9",
        color: "cyan",
        x: 8 + h,
        y: 47 + offset,
        ax: 13 + l,
        ay: 47 + offset,
      },
      {
        id: "o10",
        color: "cyan",
        x: 8 + h,
        y: 59 + offset,
        ax: 13 + l,
        ay: 59 + offset,
      },
      // Right side outputs 5..1
      {
        id: "o5",
        color: "cyan",
        x: 92 - h,
        y: 11 + offset,
        ax: 87 - l,
        ay: 11 + offset,
      },
      {
        id: "o4",
        color: "cyan",
        x: 92 - h,
        y: 23 + offset,
        ax: 87 - l,
        ay: 23 + offset,
      },
      {
        id: "o3",
        color: "cyan",
        x: 92 - h,
        y: 35 + offset,
        ax: 87 - l,
        ay: 35 + offset,
      },
      {
        id: "o2",
        color: "cyan",
        x: 92 - h,
        y: 47 + offset,
        ax: 87 - l,
        ay: 47 + offset,
      },
      {
        id: "o1",
        color: "cyan",
        x: 92 - h,
        y: 59 + offset,
        ax: 87 - l,
        ay: 59 + offset,
      },
    ];

    LIVE.tempMarkers = [
      // near left outputs 6..10
      { wire: 6, x: 20, y: 11 },
      { wire: 7, x: 20, y: 23 },
      { wire: 8, x: 20, y: 35 },
      { wire: 9, x: 20, y: 47 },
      { wire: 10, x: 20, y: 59 },
      // near right outputs 5..1
      { wire: 5, x: 80, y: 11 },
      { wire: 4, x: 80, y: 23 },
      { wire: 3, x: 80, y: 35 },
      { wire: 2, x: 80, y: 47 },
      { wire: 1, x: 80, y: 59 },
    ];
  })();

  function liveRender() {
    const svg = document.querySelector("#liveTab .live-overlay");
    if (!svg) return;
    LIVE.svg = svg;
    svg.innerHTML = "";

    const ns = "http://www.w3.org/2000/svg";

    // Markers: traces + dots
    for (const m of LIVE.markers) {
      const line = document.createElementNS(ns, "line");
      line.setAttribute("class", "trace");
      line.setAttribute("x1", m.x);
      line.setAttribute("y1", m.y);
      line.setAttribute("x2", m.ax);
      line.setAttribute("y2", m.ay);
      svg.appendChild(line);

      const dot = document.createElementNS(ns, "circle");
      dot.setAttribute("class", "dot " + m.color + " off");
      dot.setAttribute("r", 3.2);
      dot.setAttribute("cx", m.x);
      dot.setAttribute("cy", m.y);
      dot.dataset.id = m.id;
      svg.appendChild(dot);
    }

    // Temperature badges
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

  function setDot(id, on) {
    if (!LIVE.svg) return;
    const c = LIVE.svg.querySelector('circle[data-id="' + id + '"]');
    if (!c) return;
    c.classList.toggle("on", !!on);
    c.classList.toggle("off", !on);
  }

  async function pollLiveOnce() {
    try {
      const res = await fetch("/monitor", { cache: "no-store" });
      if (!res.ok) return;
      const mon = await res.json();

      const outs = mon.outputs || {};
      for (let i = 1; i <= 10; i++) {
        setDot("o" + i, !!outs["output" + i]);
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
              "Wire " + cfg.wire + ": " + t.toFixed(1) + "°C"
            );
          } else {
            badge.removeAttribute("title");
          }

          label.textContent = txt;
        }
      }

      // Relay + AC
      setDot("relay", mon.relay === true);
      setDot("ac", mon.ac === true);

      const relayToggle = document.getElementById("relayToggle");
      if (relayToggle) relayToggle.checked = mon.relay === true;
    } catch (err) {
      console.warn("live poll failed:", err);
    }
  }

  function scheduleLiveInterval() {
    if (LIVE.interval) clearInterval(LIVE.interval);
    const ms = lastState === "Running" ? 250 : 1000;
    LIVE.interval = setInterval(pollLiveOnce, ms);
  }

  // Hook power UI -> live interval
  const _origSetPowerUI = setPowerUI;
  setPowerUI = function (state, extras) {
    _origSetPowerUI(state, extras || {});
    scheduleLiveInterval();
  };

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

  function startMonitorPolling(intervalMs = 400) {
    if (window.__MONITOR_INTERVAL__) {
      clearInterval(window.__MONITOR_INTERVAL__);
    }

    window.__MONITOR_INTERVAL__ = setInterval(async () => {
      try {
        const res = await fetch("/monitor", { cache: "no-store" });
        if (!res.ok) return;
        const data = await res.json();

        // Relay sync
        const serverRelay = data.relay === true;
        setDot("relay", serverRelay);
        const relayToggle = document.getElementById("relayToggle");
        if (relayToggle) relayToggle.checked = serverRelay;

        // AC presence
        const ac = data.ac === true;
        setDot("ac", ac);

        // Voltage gauge (cap voltage or fallback)
        const fallback =
          typeof (lastLoadedControls && lastLoadedControls.dcVoltage) ===
          "number"
            ? lastLoadedControls.dcVoltage
            : 220;
        let shownV = fallback;
        if (ac) {
          const v = parseFloat(data.capVoltage);
          if (Number.isFinite(v)) shownV = v;
        }
        updateGauge("voltageValue", shownV, "V", 400);

        // Current gauge
        let rawCurrent = parseFloat(data.current);
        if (!ac || !Number.isFinite(rawCurrent)) rawCurrent = 0;
        rawCurrent = Math.max(0, Math.min(100, rawCurrent));
        updateGauge("currentValue", rawCurrent, "A", 100);

        // Temperatures (up to 12 sensors)
        const temps = data.temperatures || [];
        for (let i = 0; i < 12; i++) {
          const id = "temp" + (i + 1) + "Value";
          const t = temps[i];
          if (t === -127 || t === undefined) {
            updateGauge(id, "Off", "°C", 150);
          } else {
            updateGauge(id, Number(t), "°C", 150);
          }
        }

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
        }
      } catch (err) {
        console.error("Monitor error:", err);
      }
    }, intervalMs);
  }

  // ========================================================
  // ===============      AUTO LOGOUT HOOKS     =============
  // ========================================================

  function sendInstantLogout() {
    try {
      const payload = new Blob([JSON.stringify({ action: "disconnect" })], {
        type: "application/json",
      });
      navigator.sendBeacon("/disconnect", payload);
    } catch (e) {
      fetch("/disconnect", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ action: "disconnect" }),
      });
    }
  }

  window.addEventListener("pagehide", sendInstantLogout);
  document.addEventListener("visibilitychange", () => {
    if (document.visibilityState === "hidden") {
      sendInstantLogout();
    }
  });

  // ========================================================
  // ===============        DOM READY INIT      =============
  // ========================================================

  document.addEventListener("DOMContentLoaded", () => {
    bindConfirmModal();

    renderAllOutputs("manualOutputs", true);
    renderAllOutputs("userAccessGrid", false);

    enableDragScroll("manualOutputs");
    enableDragScroll("userAccessGrid");

    initPowerButton();
    liveRender();
    scheduleLiveInterval();

    startHeartbeat();
    startMonitorPolling();
    loadControls();

    // Disconnect button
    const disconnectBtn = document.getElementById("disconnectBtn");
    if (disconnectBtn) {
      disconnectBtn.addEventListener("click", disconnectDevice);
    }

    // Relay toggle (manual)
    const relayToggle = document.getElementById("relayToggle");
    if (relayToggle) {
      relayToggle.addEventListener("change", async () => {
        await ensureManualTakeover("relay");
        sendControlCommand("set", "relay", relayToggle.checked);
      });
    }

    // Bypass toggle
    const bypassToggle = document.getElementById("bypassToggle");
    if (bypassToggle) {
      bypassToggle.addEventListener("change", async () => {
        await ensureManualTakeover("bypass");
        sendControlCommand("set", "bypass", bypassToggle.checked);
      });
    }

    // Fan slider manual control
    const fanSlider = document.getElementById("fanSlider");
    if (fanSlider) {
      fanSlider.addEventListener("input", async () => {
        await ensureManualTakeover("fan");
        const speed = parseInt(fanSlider.value, 10) || 0;
        sendControlCommand("set", "fanSpeed", speed);
      });
    }
  });

  // ========================================================
  // ===============      EXPORT GLOBAL API     =============
  // ========================================================

  window.switchTab = switchTab;
  window.toggleUserMenu = toggleUserMenu;
  window.toggleMode = toggleMode;
  window.toggleLT = toggleLT;
  window.toggleMute = toggleMute;
  window.handleOutputToggle = handleOutputToggle;
  window.updateOutputAccess = updateOutputAccess;
  window.openConfirm = openConfirm;
  window.saveUserSettings = saveUserSettings;
  window.resetUserSettings = resetUserSettings;
  window.saveAdminSettings = saveAdminSettings;
  window.resetAdminSettings = resetAdminSettings;
  window.saveDeviceAndNichrome = saveDeviceAndNichrome;
  window.resetDeviceAndNichrome = resetDeviceAndNichrome;
  window.startSystem = startSystem;
  window.shutdownSystem = shutdownSystem;
  window.resetSystem = resetSystem;
  window.rebootSystem = rebootSystem;
  window.loadControls = loadControls;
})();
