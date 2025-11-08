// ========================================================
// ===============      TAB SWITCHING      ===============
// ========================================================
const tabs = document.querySelectorAll(".tab");
const contents = document.querySelectorAll(".content");
// ---- Power button state machine ----
const powerEl = () => document.getElementById("powerButton");
const powerText = () => document.getElementById("powerLabel");
let lastState = "Shutdown"; // default
// --- Confirmation modal controller ---
let pendingConfirm = null;
function isManualMode() {
  const t = document.getElementById("modeToggle");
  return !!(t && t.checked);
}
function sleep(ms) {
  return new Promise((r) => setTimeout(r, ms));
}
function approxEqual(a, b, eps = 0.05) {
  const na = Number(a),
    nb = Number(b);
  if (!Number.isFinite(na) || !Number.isFinite(nb)) return false;
  return Math.abs(na - nb) <= eps;
}

// Poll /load_controls until what we saved is visible there (or we time out)
async function waitUntilApplied(expected, timeoutMs = 2000, stepMs = 120) {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    const res = await fetch("/load_controls", { cache: "no-store" });
    if (res.ok) {
      const data = await res.json();

      // Check targetRes
      let ok = true;
      if (expected.targetRes != null) {
        ok &&= approxEqual(data.targetRes, expected.targetRes, 0.1);
      }

      // Check each wireRes we just set
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
        await loadControls(); // render canonical values from server
        return true;
      }
    }
    await sleep(stepMs);
  }
  // Fallback: refresh once even if we didn’t see the match in time
  await loadControls();
  return false;
}

function setModeDot(isManual) {
  const dot = document.querySelector(".status-dot");
  if (!dot) return;
  dot.title = isManual ? "Manual Mode" : "Auto Mode";
  dot.style.backgroundColor = isManual ? "#ffa500" : "#00ff80";
  dot.style.boxShadow = `0 0 6px ${dot.style.backgroundColor}`;
}
// Keep the last loaded config so Cancel can restore UI
let lastLoadedControls = null;

function setField(id, val) {
  const el = document.getElementById(id);
  if (el) el.value = (val ?? "") === null ? "" : val;
}

function getFloat(id) {
  const v = parseFloat(document.getElementById(id)?.value);
  return Number.isFinite(v) ? v : undefined;
}

function getInt(id) {
  const v = parseInt(document.getElementById(id)?.value);
  return Number.isFinite(v) ? v : undefined;
}

/**
 * Ensure manual mode is asserted and auto loop is not Running.
 * Call this once before sending ANY manual-changing command.
 */
async function ensureManualTakeover(source = "manual-action") {
  // 1) Flip UI + notify backend if not already manual
  if (!isManualMode()) {
    const t = document.getElementById("modeToggle");
    if (t) t.checked = true;
    setModeDot(true);
    await sendControlCommand("set", "mode", true);
  }

  // 2) If auto is currently Running, abort it (soft stop preferred)
  if (lastState === "Running") {
    const resp = await sendControlCommand("set", "abortAuto", true);
    if (!resp || resp.error) {
      // Fallback to your existing shutdown if backend doesn't implement abortAuto
      await shutdownSystem();
    }
    setPowerUI("Idle");
  }
  console.log(`Manual takeover by: ${source}`);
}
function readFloat(id) {
  const v = parseFloat(document.getElementById(id).value);
  return Number.isFinite(v) ? v : null;
}

function resetNichromeInputs() {
  for (let i = 1; i <= 10; i++) {
    const id = `r${String(i).padStart(2, "0")}Ohm`;
    const el = document.getElementById(id);
    if (el) el.value = "";
  }
  const tEl = document.getElementById("targetOhm");
  if (tEl) tEl.value = "";
}

/** Gather the 11 fields, validate, and send one aggregated control */
async function saveNichrome() {
  // optional: ensure manual takeover if saving affects control loops
  // await ensureManualTakeover("nichrome");

  const payload = {};
  for (let i = 1; i <= 10; i++) {
    const id = `r${String(i).padStart(2, "0")}Ohm`;
    const v = readFloat(id);
    if (v === null || v < 0) {
      alert(`Invalid value for ${id}`);
      return;
    }
    payload[`r${String(i).padStart(2, "0")}`] = v;
  }
  const tgt = readFloat("targetOhm");
  if (tgt === null || tgt < 0) {
    alert("Invalid Target Resistance");
    return;
  }
  payload.target = tgt;

  const res = await sendControlCommand("set", "nichromeConfig", payload);
  if (res && !res.error) {
    console.log("[Nichrome] Saved.");
  }
}

function openConfirm(kind) {
  pendingConfirm = kind;

  const modal = document.getElementById("confirmModal");
  const title = document.getElementById("confirmTitle");
  const message = document.getElementById("confirmMessage");
  const okBtn = document.getElementById("confirmOkBtn");

  if (kind === "reset") {
    title.textContent = "Confirm Reset";
    message.textContent = "This will reset the device (soft reset). Proceed?";
    okBtn.textContent = "Yes, Reset";
    okBtn.classList.add("danger");
  } else if (kind === "reboot") {
    title.textContent = "Confirm Reboot";
    message.textContent = "The device will restart. Continue?";
    okBtn.textContent = "Yes, Reboot";
    okBtn.classList.add("danger");
  } else {
    title.textContent = "Confirm Action";
    message.textContent = "Are you sure?";
    okBtn.textContent = "Confirm";
    okBtn.classList.remove("danger");
  }

  modal.style.display = "flex"; // center with existing .user-modal styles
}

function closeConfirm() {
  const modal = document.getElementById("confirmModal");
  modal.style.display = "none";
  pendingConfirm = null;
}

function bindConfirmModal() {
  const cancelBtn = document.getElementById("confirmCancelBtn");
  const okBtn = document.getElementById("confirmOkBtn");
  const modal = document.getElementById("confirmModal");

  if (cancelBtn) cancelBtn.onclick = closeConfirm;
  if (okBtn) {
    okBtn.onclick = () => {
      if (pendingConfirm === "reset") {
        // now we finally hit the server:
        resetSystem(); // already defined in your file
      } else if (pendingConfirm === "reboot") {
        rebootSystem(); // already defined in your file
      }
      closeConfirm();
    };
  }
  // Click outside closes
  if (modal) {
    modal.addEventListener("click", (e) => {
      if (e.target === modal) closeConfirm();
    });
  }
  // ESC closes
  document.addEventListener("keydown", (e) => {
    if (e.key === "Escape") closeConfirm();
  });
}

// ensure the handlers exist after DOM loads
document.addEventListener("DOMContentLoaded", bindConfirmModal);

function setPowerUI(state, extras = {}) {
  const btn = powerEl();
  if (!btn) return;

  // clear state classes
  btn.classList.remove("state-off", "state-idle", "state-ready", "state-error");

  // map backend state + signals to UI class/label
  // state comes from /control get status: Shutdown | Idle | Running | Error
  // extras.ready/off (bool) come from /load_controls if you want to refine
  let label = state.toUpperCase();
  let cls = "state-off";

  if (state === "Shutdown") {
    label = "OFF";
    cls = "state-off";
  } else if (state === "Idle") {
    label = "IDLE";
    cls = "state-idle";
  } else if (state === "Running") {
    // prioritize hardware "ready" if provided; else treat Running as ready
    label = extras.ready === true ? "READY" : "RUN";
    cls = "state-ready";
  } else if (state === "Error") {
    label = "ERROR";
    cls = "state-error";
  }

  btn.classList.add(cls);
  powerText().textContent = label;
  lastState = state;
}

// click → toggle between OFF and ON
function onPowerClick() {
  if (lastState === "Shutdown") {
    // OFF -> ON
    startSystem(); // already implemented in your code
  } else {
    // IDLE/RUNNING/ERROR -> OFF
    shutdownSystem(); // already implemented in your code
  }
}

// poll status from backend
async function pollDeviceState() {
  try {
    const res = await fetch("/control", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ action: "get", target: "status" }),
    });
    const data = await res.json(); // { state: "Idle" | "Running" | "Shutdown" | "Error" }
    setPowerUI(data.state);
  } catch (e) {
    console.warn("status poll failed:", e);
  }
}

// Optional: fuse with /load_controls to reflect READY/OFF LEDs too
// (call this at the end of your existing loadControls())
function applyReadyOffFlagsToPower(readyBool, offBool) {
  // If OFF LED is asserted, force OFF look immediately
  if (offBool) return setPowerUI("Shutdown", { ready: false });
  // If READY LED asserted while Running, prefer READY label
  if (lastState === "Running") setPowerUI("Running", { ready: !!readyBool });
}

// bootstrapping
function initPowerButton() {
  const btn = powerEl();
  if (btn) btn.addEventListener("click", onPowerClick);
  pollDeviceState();
  setInterval(pollDeviceState, 1000);
}
document.addEventListener("DOMContentLoaded", initPowerButton);

function switchTab(index) {
  tabs.forEach((tab, i) => {
    tab.classList.toggle("active", i === index);
    contents[i].classList.toggle("active", i === index);
  });
}
function saveUserSettings() {
  const current = document.getElementById("userCurrentPassword").value;
  const newPass = document.getElementById("userNewPassword").value;
  const newId = document.getElementById("userDeviceId").value;

  sendControlCommand("set", "userCredentials", { current, newPass, newId });
}

function resetUserSettings() {
  document.getElementById("userCurrentPassword").value = "";
  document.getElementById("userNewPassword").value = "";
  document.getElementById("userDeviceId").value = "";
}

function saveAdminSettings() {
  const username = document.getElementById("adminUsername").value;
  const password = document.getElementById("adminPassword").value;
  const wifiSSID = document.getElementById("wifiSSID").value;
  const wifiPassword = document.getElementById("wifiPassword").value;

  sendControlCommand("set", "adminCredentials", {
    username,
    password,
    wifiSSID,
    wifiPassword,
  });
}
function resetAdminSettings() {
  document.getElementById("adminCurrentPassword").value = "";
  document.getElementById("adminUsername").value = "";
  document.getElementById("adminPassword").value = "";
  document.getElementById("wifiSSID").value = "";
  document.getElementById("wifiPassword").value = "";
}

function renderAllOutputs(containerId, isControlMode = false) {
  const container = document.getElementById(containerId);
  container.innerHTML = "";

  for (let i = 1; i <= 10; i++) {
    const block = document.createElement("div");
    block.className = "manual-item";

    if (!isControlMode) {
      block.classList.add("access-style");
    }

    block.innerHTML = isControlMode
      ? `
        <span>Output ${i}</span>
        <label class="switch">
          <input type="checkbox" onchange="handleOutputToggle(${i}, this)">
          <span class="slider"></span>
        </label>
        <div class="led" id="manualLed${i}"></div>
      `
      : `
        <span>Allow Output ${i}</span>
        <label class="switch">
          <input type="checkbox" id="accessToggle${i}" onchange="updateOutputAccess(${i}, this.checked)">
          <span class="slider"></span>
        </label>`;

    container.appendChild(block);
  }
}

function enableDragScroll(containerId) {
  const container = document.getElementById(containerId);
  let isDown = false;
  let startX, scrollLeft;

  // Enable smooth scroll via CSS (safety)
  container.style.scrollBehavior = "smooth";

  // Drag scroll
  container.addEventListener("mousedown", (e) => {
    isDown = true;
    container.classList.add("dragging");
    startX = e.pageX - container.offsetLeft;
    scrollLeft = container.scrollLeft;
  });

  container.addEventListener("mouseleave", () => {
    isDown = false;
    container.classList.remove("dragging");
  });

  container.addEventListener("mouseup", () => {
    isDown = false;
    container.classList.remove("dragging");
  });

  container.addEventListener("mousemove", (e) => {
    if (!isDown) return;
    e.preventDefault();
    const x = e.pageX - container.offsetLeft;
    const walk = (x - startX) * 2;
    container.scrollLeft = scrollLeft - walk;
  });

  // Smooth horizontal wheel scroll
  container.addEventListener(
    "wheel",
    (e) => {
      if (e.deltaY !== 0) {
        e.preventDefault();
        container.scrollLeft += e.deltaY * 0.3; // smoothness multiplier
      }
    },
    { passive: false }
  );
}

function toggleUserMenu() {
  const menu = document.getElementById("userMenu");
  menu.style.display = menu.style.display === "block" ? "none" : "block";
}

document.addEventListener("click", function (e) {
  const menu = document.getElementById("userMenu");
  const icon = document.querySelector(".user-icon");
  if (!icon.contains(e.target) && !menu.contains(e.target)) {
    menu.style.display = "none";
  }
});

async function toggleMode() {
  const isManual = document.getElementById("modeToggle").checked;
  setModeDot(isManual);
  await sendControlCommand("set", "mode", isManual);

  // If user just switched to Manual while Running → stop the loop
  if (isManual && lastState === "Running") {
    const r = await sendControlCommand("set", "abortAuto", true);
    if (!r || r.error) await shutdownSystem();
    setPowerUI("Idle");
  }
}

function toggleLT() {
  const isOn = document.getElementById("ltToggle").checked;
  sendControlCommand("set", "ledFeedback", isOn);
}

async function handleOutputToggle(index, checkbox) {
  await ensureManualTakeover(`output${index}`);
  const led = checkbox.parentElement.nextElementSibling;
  const isOn = checkbox.checked;
  led.classList.toggle("active", isOn);
  sendControlCommand("set", `output${index}`, isOn);
}

async function startSystem() {
  if (isManualMode()) {
    openAlert(
      "Manual mode is ON",
      "Switch to Auto before starting the cycle.",
      "warning"
    );
    return;
  }

  sendControlCommand("set", "systemStart", true);
}

function shutdownSystem() {
  sendControlCommand("set", "systemShutdown", true);
}

function resetSystem() {
  sendControlCommand("set", "systemReset", true);
}

function rebootSystem() {
  sendControlCommand("set", "reboot", true);
}

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

document
  .getElementById("relayToggle")
  .addEventListener("change", async function () {
    await ensureManualTakeover("relay");
    sendControlCommand("set", "relay", this.checked);
  });

document
  .getElementById("bypassToggle")
  .addEventListener("change", async function () {
    await ensureManualTakeover("bypass");
    sendControlCommand("set", "bypass", this.checked);
  });

document
  .getElementById("fanSlider")
  .addEventListener("input", async function () {
    await ensureManualTakeover("fan");
    sendControlCommand("set", "fanSpeed", parseInt(this.value));
  });

function toggleOutput(index, state) {
  sendControlCommand("set", `output${index}`, state);
}

function updateOutputAccess(index, newState) {
  // Send only the changed output's access value
  sendControlCommand("set", `Access${index}`, newState);
}

/**
 * @param {string} action
 * @param {string} target
 * @param {any} [value]
 * @returns {Promise<any>}
 */
async function sendControlCommand(action, target, value) {
  const payload = { action, target };
  if (value !== undefined) payload.value = value;

  const res = await fetch("/control", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(payload),
  });
  const data = await res.json();

  if (data.status === "ok") {
    console.log(`[✔] '${action}' on '${target}' succeeded`);
  } else if (data.state) {
    console.log(`[ℹ] State: ${data.state}`);
  } else if (data.error) {
    console.warn(`[✖] ${data.error}`);
  }
  return data;
}

let isMuted = false;

function setMuteUI(muted) {
  const btn = document.getElementById("muteBtn");
  const icon = document.getElementById("muteIcon");
  if (!btn || !icon) return;

  btn.classList.toggle("muted", muted);
  icon.src = muted ? "icons/mute-2-256.png" : "icons/volume-up-4-256.png";
  icon.alt = muted ? "Muted" : "Sound";
}

function toggleMute() {
  isMuted = !isMuted;
  setMuteUI(isMuted);
  // Persist + apply on device (ESP32 will NVS-store and update buzzer behavior)
  sendControlCommand("set", "buzzerMute", isMuted);
}
async function saveDeviceAndNichrome() {
  const cmds = [];

  // (keep your existing reads)
  const v = getFloat("desiredVoltage");
  if (v !== undefined) cmds.push(["set", "desiredVoltage", v]);
  const f = getInt("acFrequency");
  if (f !== undefined) cmds.push(["set", "acFrequency", f]);
  const cr = getFloat("chargeResistor");
  if (cr !== undefined) cmds.push(["set", "chargeResistor", cr]);
  const dc = getFloat("dcVoltage");
  if (dc !== undefined) cmds.push(["set", "dcVoltage", dc]);
  const on = getInt("onTime");
  if (on !== undefined) cmds.push(["set", "onTime", on]);
  const off = getInt("offTime");
  if (off !== undefined) cmds.push(["set", "offTime", off]);
  // Wire resistances R01..R10
  const expected = { wireRes: {}, targetRes: null };
  for (let i = 1; i <= 10; i++) {
    const id = `r${String(i).padStart(2, "0")}ohm`;
    const val = getFloat(id);
    if (val !== undefined) {
      cmds.push(["set", `wireRes${i}`, val]); // backend expects wireResX
      expected.wireRes[String(i)] = val;
    }
  }

  // Target resistance
  const tgt = getFloat("rTarget");
  if (tgt !== undefined) {
    cmds.push(["set", "targetRes", tgt]); // backend expects targetRes
    expected.targetRes = tgt;
  }
  const wireOhmPerM = getFloat("wireOhmPerM");
  if (wireOhmPerM !== undefined && !Number.isNaN(wireOhmPerM)) {
    cmds.push(["set", "wireOhmPerM", wireOhmPerM]);
  }

  // Send sequentially (server answers 202 "queued", worker applies later)
  for (const [a, t, v] of cmds) {
    await sendControlCommand(a, t, v);
  }

  // <-- The important bit: wait until /load_controls shows the changes
  await waitUntilApplied(expected, 2000, 120);
}

function resetDeviceAndNichrome() {
  // If we have a cached config, restore it; otherwise re-fetch
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
  setField("wireOhmPerM", data.wireOhmPerM ?? "");

  const wr = data.wireRes || {};
  for (let i = 1; i <= 10; i++) {
    setField(`r${String(i).padStart(2, "0")}ohm`, wr[String(i)]);
  }
  setField("rTarget", data.targetRes);
}

async function loadControls() {
  try {
    const res = await fetch("/load_controls", { cache: "no-store" });
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    const data = await res.json();
    console.log("Fetched config:", data);

    const access = data.outputAccess || {};
    const states = data.outputs || {};

    // --- LT toggle ---
    const ltToggle = document.getElementById("ltToggle");
    if (ltToggle) ltToggle.checked = !!data.ledFeedback;

    // --- Buzzer mute ---
    if (typeof data.buzzerMute === "boolean") {
      // assumes globals isMuted + setMuteUI exist
      isMuted = data.buzzerMute;
      setMuteUI(isMuted);
    }

    // --- Ready / Off LEDs ---
    const readyLed = document.getElementById("readyLed");
    const offLed = document.getElementById("offLed");
    if (readyLed)
      readyLed.style.backgroundColor = data.ready ? "limegreen" : "gray";
    if (offLed) offLed.style.backgroundColor = data.off ? "red" : "gray";

    // Keep a copy for Cancel / fallbacks
    lastLoadedControls = data;

    // --- Device numeric fields ---
    setField("desiredVoltage", data.desiredVoltage);
    setField("acFrequency", data.acFrequency);
    setField("chargeResistor", data.chargeResistor);
    setField("dcVoltage", data.dcVoltage);
    setField("onTime", data.onTime);
    setField("offTime", data.offTime);
    if (data.wireOhmPerM !== undefined) {
      setField("wireOhmPerM", data.wireOhmPerM);
    }

    // --- Nichrome values (canonical IDs: r01ohm..r10ohm, rTarget) ---
    const wireRes = (() => {
      if (data && typeof data.wireRes === "object") return data.wireRes;
      const obj = {};
      if (Array.isArray(data?.wireOhms)) {
        for (let i = 1; i <= 10; i++) obj[String(i)] = data.wireOhms[i - 1];
        return obj;
      }
      if (data?.nichrome && typeof data.nichrome === "object") {
        for (let i = 1; i <= 10; i++) {
          const k = `r${String(i).padStart(2, "0")}`;
          obj[String(i)] = data.nichrome[k];
        }
        return obj;
      }
      return obj;
    })();

    for (let i = 1; i <= 10; i++) {
      setField(`r${String(i).padStart(2, "0")}ohm`, wireRes[String(i)]);
    }

    const targetRes =
      typeof data.targetRes === "number"
        ? data.targetRes
        : typeof data?.nichrome?.target === "number"
        ? data.nichrome.target
        : undefined;
    setField("rTarget", targetRes);

    // --- Manual Control toggles (Outputs tab) ---
    for (let i = 1; i <= 10; i++) {
      const checked = !!states[`output${i}`];
      const itemSel = `#manualOutputs .manual-item:nth-child(${i})`;
      const checkbox = document.querySelector(
        `${itemSel} input[type="checkbox"]`
      );
      const led = document.querySelector(`${itemSel} .led`);
      if (checkbox) checkbox.checked = checked;
      if (led) led.classList.toggle("active", checked);
    }

    // --- Output Access (User Settings tab) ---
    for (let i = 1; i <= 10; i++) {
      const checkbox = document.querySelector(
        `#userAccessGrid .manual-item:nth-child(${i}) input[type="checkbox"]`
      );
      if (checkbox) checkbox.checked = !!access[`output${i}`];
    }

    // --- RELAY: mirror current live state to both tabs ---
    const relayFromServer = !!data.relay;
    const relayToggle = document.getElementById("relayToggle");
    if (relayToggle) relayToggle.checked = relayFromServer; // Manual tab
    if (typeof setDot === "function") setDot("relay", relayFromServer); // Live tab
  } catch (err) {
    console.error("Failed to load controls:", err);
  }
}

function closeUserModal() {
  document.getElementById("userModal").style.display = "none";
}

function saveUserSettings() {
  const currentPassword = document.getElementById("userCurrentPassword").value;
  const newPassword = document.getElementById("userNewPassword").value;
  const newId = document.getElementById("userDeviceId").value;

  fetch("/SetUserCred", {
    method: "POST",
    headers: {
      "Content-Type": "application/json",
    },
    body: JSON.stringify({
      current: currentPassword,
      username: newId,
      password: newPassword,
    }),
  })
    .then((res) => res.json())
    .then((data) => {
      if (data.status) {
        openAlert("✅ " + data.status);
        closeUserModal();
      } else if (data.error) {
        openAlert("⚠️ " + data.error);
      }
    })
    .catch((err) => {
      console.error("Credential update failed:", err);
      openAlert("Error communicating with device.");
    });
}
function parseNum(v) {
  const f = parseFloat(String(v).replace(",", "."));
  return Number.isFinite(f) ? f : null;
}

async function saveNichrome() {
  // send 10 wire values + 1 target via the same /control "set" action
  const ops = [];
  for (let i = 1; i <= 10; i++) {
    const id = `r${String(i).padStart(2, "0")}ohm`;
    const v = parseNum(document.getElementById(id)?.value);
    if (v !== null) {
      // use the NVS key as target to keep naming 1:1 with backend
      const key = `R${String(i).padStart(2, "0")}OHM`;
      ops.push(sendControlCommand("set", key, v));
    }
  }

  const tgtVal = parseNum(document.getElementById("r0xtgt")?.value);
  if (tgtVal !== null) {
    ops.push(sendControlCommand("set", "R0XTGT", tgtVal));
  }

  // fire & await all
  await Promise.all(ops);
}

function resetNichrome() {
  // You can choose to restore defaults or simply clear fields.
  // Here we just clear; a reload will fill from current NVS.
  for (let i = 1; i <= 10; i++) {
    const el = document.getElementById(`r${String(i).padStart(2, "0")}ohm`);
    if (el) el.value = "";
  }
  const tgt = document.getElementById("r0xtgt");
  if (tgt) tgt.value = "";
}

function saveAdminSettings() {
  const currentPassword = document.getElementById("adminCurrentPassword").value;
  const username = document.getElementById("adminUsername").value;
  const password = document.getElementById("adminPassword").value;
  const ssid = document.getElementById("wifiSSID").value;
  const wifiPassword = document.getElementById("wifiPassword").value;

  fetch("/SetAdminCred", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({
      current: currentPassword,
      username,
      password,
      ssid,
      wifiPassword,
    }),
  })
    .then((res) => res.json())
    .then((data) => {
      if (data.status) openAlert("✅ " + data.status);
      else if (data.error) openAlert("⚠️ " + data.error);
    })
    .catch((err) => {
      console.error("Admin settings update failed:", err);
      openAlert("Error communicating with device.");
    });
}

function resetAdminSettings() {
  document.getElementById("adminCurrentPassword").value = "";
  document.getElementById("adminUsername").value = "";
  document.getElementById("adminPassword").value = "";
  document.getElementById("wifiSSID").value = "";
  document.getElementById("wifiPassword").value = "";
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
          openAlert(data.error || "Unexpected response");
        });
      }
    })
    .catch((err) => {
      console.error("Disconnect failed:", err);
      window.location.href = "/login.html";
    });
}

function updateGauge(id, value, unit, maxValue) {
  const display = document.getElementById(id);
  const stroke = display.closest("svg").querySelector("path.gauge-fg");

  if (value === "Off") {
    stroke.setAttribute("stroke-dasharray", `0, 100`);
    display.textContent = "Off";
    return;
  }

  const percent = Math.min((parseFloat(value) / maxValue) * 100, 100);
  stroke.setAttribute("stroke-dasharray", `${percent}, 100`);
  display.textContent = `${value}${unit}`;
}

function startMonitorPolling(intervalMs = 400) {
  // avoid stacking multiple intervals if this is called twice
  if (window.__monInt) clearInterval(window.__monInt);

  window.__monInt = setInterval(async () => {
    try {
      const res = await fetch("/monitor", { cache: "no-store" });
      const data = await res.json();

      // RELAY: keep Live + Manual in sync on every poll
      const serverRelay = data.relay === true;
      setDot("relay", serverRelay); // Live tab
      const relayToggle = document.getElementById("relayToggle");
      if (relayToggle) relayToggle.checked = serverRelay; // Manual tab

      // Show measured DC only when AC is present; otherwise hold at dcVoltage (or 220)
      const ac = data.ac === true;
      const fallback =
        typeof lastLoadedControls?.dcVoltage === "number"
          ? lastLoadedControls.dcVoltage
          : 220;

      let shownV = fallback;
      if (ac) {
        const v = parseFloat(data.capVoltage);
        shownV = Number.isFinite(v) ? v : fallback;
      }
      updateGauge("voltageValue", shownV.toFixed(2), "V", 400);

      // (Optional) visually dim when AC is off
      const vgCard = document
        .getElementById("voltageValue")
        ?.closest(".gauge-card");
      if (vgCard) vgCard.classList.toggle("muted", !ac);

      // Current: clamp to 0 when AC is false or ADC is noisy
      let rawCurrent = parseFloat(data.current);
      if (!ac || !Number.isFinite(rawCurrent)) rawCurrent = 0;
      const clampedCurrent = Math.max(0, Math.min(100, rawCurrent)).toFixed(2);
      updateGauge("currentValue", clampedCurrent, "A", 100);

      const temps = data.temperatures || [];
      updateGauge(
        "temp1Value",
        temps[0] === -127 ? "Off" : Number(temps[0]).toFixed(2),
        "°C",
        150
      );
      updateGauge(
        "temp2Value",
        temps[1] === -127 ? "Off" : Number(temps[1]).toFixed(2),
        "°C",
        150
      );
      updateGauge(
        "temp3Value",
        temps[2] === -127 ? "Off" : Number(temps[2]).toFixed(2),
        "°C",
        150
      );
      updateGauge(
        "temp4Value",
        temps[3] === -127 ? "Off" : Number(temps[3]).toFixed(2),
        "°C",
        150
      );
      updateGauge(
        "temp5Value",
        temps[4] === -127 ? "Off" : Number(temps[4]).toFixed(2),
        "°C",
        150
      );
      updateGauge(
        "temp6Value",
        temps[5] === -127 ? "Off" : Number(temps[5]).toFixed(2),
        "°C",
        150
      );
      updateGauge(
        "temp7Value",
        temps[6] === -127 ? "Off" : Number(temps[6]).toFixed(2),
        "°C",
        150
      );
      updateGauge(
        "temp8Value",
        temps[7] === -127 ? "Off" : Number(temps[7]).toFixed(2),
        "°C",
        150
      );
      updateGauge(
        "temp9Value",
        temps[8] === -127 ? "Off" : Number(temps[8]).toFixed(2),
        "°C",
        150
      );
      updateGauge(
        "temp10Value",
        temps[9] === -127 ? "Off" : Number(temps[9]).toFixed(2),
        "°C",
        150
      );
      updateGauge(
        "temp11Value",
        temps[10] === -127 ? "Off" : Number(temps[10]).toFixed(2),
        "°C",
        150
      );
      updateGauge(
        "temp12Value",
        temps[11] === -127 ? "Off" : Number(temps[11]).toFixed(2),
        "°C",
        150
      );

      // Ready / Off LEDs + Fan slider (unchanged)
      const readyLed = document.getElementById("readyLed");
      const offLed = document.getElementById("offLed");
      if (readyLed)
        readyLed.style.backgroundColor = data.ready ? "limegreen" : "gray";
      if (offLed) offLed.style.backgroundColor = data.off ? "red" : "gray";

      const fanSlider = document.getElementById("fanSlider");
      if (fanSlider && typeof data.fanSpeed === "number")
        fanSlider.value = data.fanSpeed;
    } catch (err) {
      console.error("Monitor error:", err);
    }
  }, intervalMs);
}

function saveDeviceSettings() {
  const desiredVoltage = parseFloat(
    document.getElementById("desiredVoltage").value
  );
  const acFrequency = parseInt(document.getElementById("acFrequency").value);
  const chargeResistor = parseFloat(
    document.getElementById("chargeResistor").value
  );
  const dcVoltage = parseFloat(document.getElementById("dcVoltage").value);
  const onTime = parseInt(document.getElementById("onTime").value);
  const offTime = parseInt(document.getElementById("offTime").value);

  if (
    isNaN(desiredVoltage) ||
    isNaN(acFrequency) ||
    isNaN(chargeResistor) ||
    isNaN(dcVoltage) ||
    isNaN(onTime) ||
    isNaN(offTime)
  ) {
    openAlert("⚠️ Please enter valid numeric values for all fields.");
    return;
  }

  sendControlCommand("set", "desiredVoltage", desiredVoltage);
  sendControlCommand("set", "acFrequency", acFrequency);
  sendControlCommand("set", "chargeResistor", chargeResistor);
  sendControlCommand("set", "dcVoltage", dcVoltage);
  sendControlCommand("set", "onTime", onTime);
  sendControlCommand("set", "offTime", offTime);
}

function resetDeviceSettings() {
  loadControls(); // Reload from backend to restore defaults
} // ─────────────────────────────────────────────────────────────
// Generic alert modal (reuses #confirmModal) for info/warn/error
// ─────────────────────────────────────────────────────────────
function openAlert(title, message, variant = "warning") {
  // We are not confirming any pending action here
  pendingConfirm = null;

  const modal = document.getElementById("confirmModal");
  const titleEl = document.getElementById("confirmTitle");
  const messageEl = document.getElementById("confirmMessage");
  const okBtn = document.getElementById("confirmOkBtn");
  const cancelBtn = document.getElementById("confirmCancelBtn");

  // Restore Cancel visibility for future confirms
  if (cancelBtn) cancelBtn.style.display = "none"; // hide for simple alerts

  titleEl.textContent = title || "Notice";
  messageEl.textContent = message || "";
  okBtn.textContent = "OK";

  // Style the OK button based on severity
  okBtn.classList.remove("danger", "warning", "success");
  if (variant === "danger") okBtn.classList.add("danger");
  else if (variant === "success") okBtn.classList.add("success");
  else okBtn.classList.add("warning");

  // Clicking OK just closes the modal
  okBtn.onclick = closeConfirm;

  modal.style.display = "flex";
}

// Ensure Cancel is visible again for real confirmations
const _openConfirm_original = openConfirm;
openConfirm = function (kind) {
  const cancelBtn = document.getElementById("confirmCancelBtn");
  if (cancelBtn) cancelBtn.style.display = ""; // show Cancel for confirms
  _openConfirm_original(kind);
};

window.addEventListener("DOMContentLoaded", () => {
  // Existing initializations...
  renderAllOutputs("manualOutputs", true);
  renderAllOutputs("userAccessGrid", false);

  enableDragScroll("manualOutputs");
  enableDragScroll("userAccessGrid");

  //startHeartbeat();
  startMonitorPolling();
  loadControls();

  const disconnectBtn = document.getElementById("disconnectBtn");
  if (disconnectBtn) {
    disconnectBtn.addEventListener("click", disconnectDevice);
  }

  // ➕ Relay Toggle
  const relayToggle = document.getElementById("relayToggle");
  if (relayToggle) {
    relayToggle.addEventListener("change", () => {
      sendControlCommand("set", "relay", relayToggle.checked);
    });
  }

  // ➕ Bypass Toggle
  const bypassToggle = document.getElementById("bypassToggle");
  if (bypassToggle) {
    bypassToggle.addEventListener("change", () => {
      sendControlCommand("set", "bypass", bypassToggle.checked);
    });
  }

  // ➕ Fan Speed Slider
  const fanSlider = document.getElementById("fanSlider");
  if (fanSlider) {
    fanSlider.addEventListener("input", () => {
      const speed = parseInt(fanSlider.value);
      sendControlCommand("set", "fanSpeed", speed);
    });
  }
});

// ===== Live Tab: board overlay =====

// Markers are placed in percent units (0..100) to scale with the image.
// Each marker has a dot at (x,y) and a line to an anchor (ax,ay).
const LIVE = {
  svg: null,

  // output / relay dots
  markers: [
    (offset = 2.6),
    (l = 3),
    (h = 2.5),
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
  ],

  // 👉 Temperature bubble positions (edit x/y to move them)
  // wire = 1..10 maps to wireTemps[wire-1] from /monitor
  tempMarkers: [
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
  ],

  interval: null,
};

function liveRender() {
  const svg = document.querySelector("#liveTab .live-overlay");
  if (!svg) return;
  LIVE.svg = svg;
  svg.innerHTML = ""; // reset

  const ns = "http://www.w3.org/2000/svg";

  // Draw traces + dots
  for (const m of LIVE.markers) {
    if (!m || typeof m !== "object" || !m.id) continue; // skip offset/l/h entries

    const line = document.createElementNS(ns, "line");
    line.setAttribute("class", "trace");
    line.setAttribute("x1", m.x);
    line.setAttribute("y1", m.y);
    line.setAttribute("x2", m.ax);
    line.setAttribute("y2", m.ay);
    svg.appendChild(line);

    const dot = document.createElementNS(ns, "circle");
    dot.setAttribute("class", `dot ${m.color} off`);
    dot.setAttribute("r", 3.2);
    dot.setAttribute("cx", m.x);
    dot.setAttribute("cy", m.y);
    dot.dataset.id = m.id;
    svg.appendChild(dot);
  }

  // Draw temperature badges (initially "--")
  if (Array.isArray(LIVE.tempMarkers)) {
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
}

function setDot(id, on) {
  const c = LIVE.svg?.querySelector(`circle[data-id="${id}"]`);
  if (!c) return;
  c.classList.toggle("on", !!on);
  c.classList.toggle("off", !on);
}

// --- Live polling (no backend change) ---
function scheduleLiveInterval() {
  if (LIVE.interval) clearInterval(LIVE.interval);
  const ms = lastState === "Running" ? 250 : 1000; // fast when Running
  LIVE.interval = setInterval(pollLiveOnce, ms);
}

async function pollLiveOnce() {
  try {
    const mon = await fetch("/monitor", { cache: "no-store" }).then((r) =>
      r.json()
    );

    // 10 outputs → Live dots
    const outs = mon?.outputs || {};
    for (let i = 1; i <= 10; i++) {
      setDot(`o${i}`, !!outs[`output${i}`]);
    }

    // Per-wire estimated temperatures from backend
    const wireTemps = mon?.wireTemps || [];

    // Attach tooltip on the state dots (optional, if you like)
    for (let i = 1; i <= 10; i++) {
      const t = wireTemps[i - 1];
      const dot = LIVE.svg?.querySelector(`circle[data-id="o${i}"]`);
      if (!dot) continue;

      if (typeof t === "number" && t > -100) {
        dot.dataset.temp = t;
        dot.setAttribute("title", `Wire ${i}: ${t.toFixed(1)}°C`);
      } else {
        dot.dataset.temp = "";
        dot.removeAttribute("title");
      }
    }

    // Update temp badge circles
    if (LIVE.svg && Array.isArray(LIVE.tempMarkers)) {
      for (const cfg of LIVE.tempMarkers) {
        const badge = LIVE.svg.querySelector(
          `g.temp-badge[data-wire="${cfg.wire}"]`
        );
        if (!badge) continue;

        const label = badge.querySelector("text.temp-label");
        const circle = badge.querySelector("circle.temp-circle");
        if (!label || !circle) continue;

        const t = wireTemps[cfg.wire - 1];
        let txt = "--";

        badge.classList.remove("warn", "hot");

        if (typeof t === "number" && t > -100) {
          const rounded = Math.round(t);
          txt = String(rounded);

          // simple coloring thresholds – adjust as you like
          if (t >= 400) badge.classList.add("hot");
          else if (t >= 250) badge.classList.add("warn");

          badge.setAttribute("title", `Wire ${cfg.wire}: ${t.toFixed(1)}°C`);
        } else {
          badge.removeAttribute("title");
        }

        label.textContent = txt;
      }
    }

    // Relay — mirror to Live + Manual
    const serverRelay = mon.relay === true;
    setDot("relay", serverRelay);
    const relayToggle = document.getElementById("relayToggle");
    if (relayToggle) relayToggle.checked = serverRelay;

    // AC flag
    setDot("ac", mon.ac === true);
  } catch (e) {
    console.warn("live poll failed", e);
  }
}

// Call this wherever your state changes are handled:
const _prevSetPowerUI = setPowerUI;
setPowerUI = function (state, extras = {}) {
  _prevSetPowerUI(state, extras);
  scheduleLiveInterval();
};

// Bootstrapping
document.addEventListener("DOMContentLoaded", () => {
  liveRender(); // your existing SVG builder
  scheduleLiveInterval();
});

function initLiveTab() {
  liveRender();
  // You can also hook this into loadControls() if you prefer fewer fetches.
  if (LIVE.interval) clearInterval(LIVE.interval);
  LIVE.interval = setInterval(pollLiveOnce, 500);
  pollLiveOnce();
}

document.addEventListener("DOMContentLoaded", initLiveTab);
function sendInstantLogout() {
  try {
    const payload = new Blob([JSON.stringify({ action: "disconnect" })], {
      type: "application/json",
    });
    navigator.sendBeacon("/disconnect", payload);
  } catch (e) {
    // Fallback if Beacon fails
    fetch("/disconnect", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ action: "disconnect" }),
    });
  }
}

// Fire on real page teardown (mobile-friendly)
window.addEventListener("pagehide", sendInstantLogout);
// Also fire when the tab just goes to background
document.addEventListener("visibilitychange", () => {
  if (document.visibilityState === "hidden") sendInstantLogout();
});
