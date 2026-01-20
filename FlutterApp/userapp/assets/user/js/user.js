// UI tab logic - handles sidebar tab switching
const tabs = document.querySelectorAll(".tab");
const contents = document.querySelectorAll(".content");
let lastState = "Shutdown";
let stateStream = null;
let statePollTimer = null;
let heartbeatTimer = null;
let setupRunAllowed = true;
let setupConfigOk = true;
let setupCalibOk = true;
let lastMonitor = null;

function redirectToLogin() {
  if (window.pbClearToken) window.pbClearToken();
  if (window.pbLoginUrl) {
    window.location.href = window.pbLoginUrl();
  } else {
    window.location.href = "login.html";
  }
}

if (window.pbGetToken && !window.pbGetToken()) {
  redirectToLogin();
}

// Hide Manual tab by default (index 1)
const manualTab = document.querySelector(".sidebar .tab:nth-child(2)");
if (manualTab) manualTab.style.display = "none";

// Switch between tabs based on index
function switchTab(index) {
  tabs.forEach((tab, i) => {
    tab.classList.toggle("active", i === index);
    contents[i].classList.toggle("active", i === index);
  });
}

function setText(id, value) {
  const el = document.getElementById(id);
  if (!el) return;
  el.textContent = value;
}

function formatNumber(value, digits = 1) {
  const num = Number(value);
  if (!Number.isFinite(num)) return "--";
  return num.toFixed(digits);
}

function formatDuration(seconds) {
  const total = Math.max(0, Math.round(Number(seconds) || 0));
  const mins = Math.floor(total / 60);
  const hrs = Math.floor(mins / 60);
  const remMins = mins % 60;
  const remSecs = total % 60;
  const pad2 = (n) => String(n).padStart(2, "0");
  if (hrs > 0) {
    return `${pad2(hrs)}:${pad2(remMins)}:${pad2(remSecs)}`;
  }
  return `${pad2(remMins)}:${pad2(remSecs)}`;
}

function setState(next) {
  if (!next || next === lastState) return;
  lastState = next;
  updateStateUi();
  applyControlLocks();
  if (next === "Running" || next === "Shutdown") {
    const btn = document.getElementById("confirmCoolBtn");
    if (btn) btn.classList.remove("is-confirmed");
  }
}

function updateStateUi() {
  const dot = document.querySelector(".status-dot");
  const stateValue = document.getElementById("stateValue");
  const stateDetail = document.getElementById("stateDetail");

  if (stateValue) stateValue.textContent = lastState || "--";

  if (stateDetail) {
    if (!lastMonitor) {
      stateDetail.textContent = "--";
    } else {
      const ac = lastMonitor.ac ? "AC On" : "AC Off";
      const relay = lastMonitor.relay ? "Relay On" : "Relay Off";
      stateDetail.textContent = `${ac} / ${relay}`;
    }
  }

  if (dot) {
    dot.classList.remove(
      "state-idle",
      "state-running",
      "state-error",
      "state-shutdown"
    );
    if (lastState === "Idle") dot.classList.add("state-idle");
    else if (lastState === "Running") dot.classList.add("state-running");
    else if (lastState === "Error") dot.classList.add("state-error");
    else if (lastState === "Shutdown") dot.classList.add("state-shutdown");
  }
}

function updateSetupNotice() {
  const banner = document.getElementById("setupNotice");
  const textEl = document.getElementById("setupNoticeText");
  if (!banner) return;
  if (setupRunAllowed) {
    banner.style.display = "none";
    return;
  }
  let msg = "Ask an admin to finish setup before running.";
  if (!setupConfigOk) {
    msg = "Configuration is incomplete. Ask an admin to update settings.";
  } else if (!setupCalibOk) {
    msg = "Calibration is incomplete. Ask an admin to run calibration.";
  }
  if (textEl) textEl.textContent = msg;
  banner.style.display = "flex";
}

function updateStatusCards(data) {
  if (!data) return;
  lastMonitor = data;

  const wireTarget = Number(data.wireTargetC);
  setText(
    "wireTargetValue",
    Number.isFinite(wireTarget) ? `${formatNumber(wireTarget, 1)} C` : "--"
  );
  setText(
    "wireTargetSub",
    data.floor && data.floor.active ? "Floor control" : "Wire control"
  );

  const floor = data.floor || {};
  const floorTemp = Number(floor.temp_c);
  const floorTarget = Number(floor.target_c);
  setText(
    "floorTempValue",
    Number.isFinite(floorTemp) ? `${formatNumber(floorTemp, 1)} C` : "--"
  );
  if (Number.isFinite(floorTarget)) {
    setText("floorTargetValue", `Target ${formatNumber(floorTarget, 1)} C`);
  } else if (Number.isFinite(wireTarget)) {
    setText("floorTargetValue", `Wire ${formatNumber(wireTarget, 1)} C`);
  } else {
    setText("floorTargetValue", "--");
  }

  const session = data.session || {};
  if (session.valid) {
    const energy = Number(session.energy_Wh);
    const duration = Number(session.duration_s);
    setText(
      "sessionEnergyValue",
      Number.isFinite(energy) ? `${formatNumber(energy, 1)} Wh` : "--"
    );
    setText(
      "sessionTimeValue",
      Number.isFinite(duration) ? formatDuration(duration) : "--"
    );
  } else {
    setText("sessionEnergyValue", "--");
    setText("sessionTimeValue", "--");
  }

  updateStateUi();
}

function updateCooldownNotice(data) {
  const notice = document.getElementById("cooldownNotice");
  const textEl = document.getElementById("cooldownNoticeText");
  if (!notice) return;

  const wait = data && data.ambientWait ? data.ambientWait : null;
  if (wait && wait.active) {
    notice.style.display = "flex";
    let msg = "Waiting for wires to cool. Press Confirm Cool if safe.";
    if (wait.tol_c !== undefined) {
      msg += ` Tol ${formatNumber(wait.tol_c, 1)} C.`;
    }
    if (textEl) textEl.textContent = msg;
  } else {
    notice.style.display = "none";
  }
}

function applyControlLocks() {
  const startBtn = document.getElementById("startBtn");
  const stopBtn = document.getElementById("stopBtn");
  const confirmBtn = document.getElementById("confirmCoolBtn");

  const runAllowed = !!setupRunAllowed;
  const disableStart =
    !runAllowed || lastState === "Running" || lastState === "Error";
  const disableStop = lastState === "Shutdown";
  const disableConfirm = !runAllowed || lastState === "Running";

  if (startBtn) startBtn.classList.toggle("disabled", disableStart);
  if (stopBtn) stopBtn.classList.toggle("disabled", disableStop);
  if (confirmBtn) confirmBtn.classList.toggle("disabled", disableConfirm);
}

// LT toggle - sends LED feedback toggle state to the server
function toggleLT() {
  const isOn = document.getElementById("ltToggle").checked;
  sendControlCommand("set", "ledFeedback", isOn);
  console.log(`LT Toggle switched to ${isOn ? "ON" : "OFF"}`);
}

// System controls - trigger start and shutdown events
function startSystem() {
  if (!setupRunAllowed) {
    let msg = "Setup is incomplete. Ask an admin to finish setup.";
    if (!setupConfigOk) {
      msg = "Configuration is incomplete. Ask an admin to update settings.";
    } else if (!setupCalibOk) {
      msg = "Calibration is incomplete. Ask an admin to run calibration.";
    }
    alert(msg);
    return;
  }
  sendControlCommand("set", "systemStart", true);
}

function shutdownSystem() {
  sendControlCommand("set", "systemShutdown", true);
}

async function confirmWiresCool() {
  if (!setupRunAllowed) {
    alert("Setup is incomplete. Ask an admin to finish setup.");
    return;
  }
  const resp = await sendControlCommand("set", "confirmWiresCool", true);
  if (resp && resp.error) {
    alert("Failed to confirm wires cool.");
    return;
  }
  const btn = document.getElementById("confirmCoolBtn");
  if (btn) btn.classList.add("is-confirmed");
}

// User menu - toggles user dropdown and auto-hides on outside click
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

// Manual output scrolling - enables horizontal scroll with mouse
const manualScrollArea = document.querySelector(".manual-outputs");
manualScrollArea?.addEventListener("wheel", function (e) {
  if (!e.shiftKey) {
    e.preventDefault();
    manualScrollArea.scrollBy({ left: e.deltaY, behavior: "smooth" });
  }
});

// Load controls - fetch initial UI control states and populate interface
async function loadControls() {
  try {
    const res = await fetch("/load_controls");
    if (res.status === 401) {
      redirectToLogin();
      return;
    }
    const data = await res.json();
    console.log("Fetched config:", data); // Debug info

    setupRunAllowed = data.setupRunAllowed !== false;
    setupConfigOk = data.setupConfigOk !== false;
    setupCalibOk = data.setupCalibOk !== false;
    updateSetupNotice();
    applyControlLocks();

    //  Update LT toggle switch 
    const ltToggle = document.getElementById("ltToggle");
    if (ltToggle) {
      ltToggle.checked = !!data.ledFeedback;
      console.log(`LT toggle set to: ${ltToggle.checked}`);
    } else {
      console.warn("LT toggle element not found in DOM!");
    }

    //  Update Ready / OFF LED indicators 
    const readyLed = document.getElementById("readyLed");
    const offLed = document.getElementById("offLed");

    if (readyLed)
      readyLed.style.backgroundColor = data.ready ? "limegreen" : "gray";
    else console.warn("Ready LED not found in DOM!");

    if (offLed) offLed.style.backgroundColor = data.off ? "red" : "gray";
    else console.warn("OFF LED not found in DOM!");

    //  Render manual output switches 
    const manualOutputs = document.getElementById("manualOutputs");
    manualOutputs.innerHTML = "";

    const access = data.outputAccess || {};
    const states = data.outputs || {};
    const hasManual = Object.values(access).some((value) => value === true);
    if (manualTab) manualTab.style.display = hasManual ? "block" : "none";
    const manualEmpty = document.getElementById("manualEmpty");
    if (manualEmpty) manualEmpty.style.display = hasManual ? "none" : "block";
    if (!hasManual) switchTab(0);

    Object.keys(access).forEach((key, i) => {
      const outputIndex = i + 1;
      const outputName = `output${outputIndex}`;
      const isAccessible = access[key] === true;
      const isChecked = states[outputName] === true;

      if (isAccessible) {
        const item = document.createElement("div");
        item.className = "manual-item";
        item.innerHTML = `
          <span>Output ${outputIndex}</span>
          <label class="switch">
            <input type="checkbox" ${
              isChecked ? "checked" : ""
            } onchange="handleOutputToggle(${outputIndex}, this)">
            <span class="slider"></span>
          </label>
          <div class="led ${isChecked ? "active" : ""}"></div>
        `;
        manualOutputs.appendChild(item);
      }
    });
  } catch (err) {
    console.error("Failed to load controls:", err);
  }
}

// Output switch toggle handler - called on each checkbox change
async function handleOutputToggle(index, checkbox) {
  const led = checkbox.parentElement.nextElementSibling;
  const isOn = checkbox.checked;

  if (led) led.classList.toggle("active", isOn); // Visual feedback
  const resp = await sendControlCommand("set", `output${index}`, isOn); // Send command
  if (resp && resp.error) {
    if (led) led.classList.toggle("active", !isOn);
    checkbox.checked = !isOn;
  }
}

// LED feedback - toggles indicator next to LT switch
function toggleLED(input) {
  const led = input.parentElement.nextElementSibling;
  led.classList.toggle("active", input.checked);
}

// User modal - open, close, and save user credentials
function closeUserModal() {
  document.getElementById("userModal").style.display = "none";
}

function saveUserSettings() {
  const currentPassword = document.getElementById("currentPassword").value;
  const newPassword = document.getElementById("newPassword").value;
  const newId = document.getElementById("newId").value;

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
        alert("OK: " + data.status);
        closeUserModal();
      } else if (data.error) {
        alert("Error: " + data.error);
      }
    })
    .catch((err) => {
      console.error("Credential update failed:", err);
      alert("Error communicating with device.");
    });
}

// Gauge rendering - updates visual gauges with live values
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

function formatTempValue(raw) {
  const num = Number(raw);
  if (!Number.isFinite(num) || num === -127) return "Off";
  return num.toFixed(2);
}

// Live data poller - fetches and updates live telemetry
function startMonitorPolling(intervalMs = 1000) {
  setInterval(() => {
    fetch("/monitor")
      .then((res) => {
        if (res.status === 401) {
          redirectToLogin();
          throw new Error("Not authenticated");
        }
        return res.json();
      })
      .then((data) => {
        const voltage = parseFloat(data.capVoltage).toFixed(2);
        updateGauge("voltageValue", voltage, "V", 400);

        let rawCurrent = parseFloat(data.current);
        if (isNaN(rawCurrent)) rawCurrent = 0;
        const clampedCurrent = Math.max(0, Math.min(100, rawCurrent)).toFixed(
          2
        );
        updateGauge("currentValue", clampedCurrent, "A", 100);

        const temps = data.temperatures || [];
        updateGauge("temp1Value", formatTempValue(temps[0]), "\u00B0C", 150);
        updateGauge("temp2Value", formatTempValue(temps[1]), "\u00B0C", 150);
        updateGauge("temp3Value", formatTempValue(temps[2]), "\u00B0C", 150);
        const floorTemp =
          data.floor && Number.isFinite(Number(data.floor.temp_c))
            ? Number(data.floor.temp_c)
            : temps[3];
        const floorTempNum = Number(floorTemp);
        updateGauge(
          "temp4Value",
          !Number.isFinite(floorTempNum) || floorTempNum === -127
            ? "Off"
            : floorTempNum.toFixed(2),
          "\u00B0C",
          150
        );

        const readyLed = document.getElementById("readyLed");
        const offLed = document.getElementById("offLed");

        if (readyLed)
          readyLed.style.backgroundColor = data.ready ? "limegreen" : "gray";
        if (offLed) offLed.style.backgroundColor = data.off ? "red" : "gray";

        updateStatusCards(data);
        updateCooldownNotice(data);
        if (data.off) {
          setState("Shutdown");
        } else if (data.ready) {
          setState("Idle");
        }
      })
      .catch((err) => {
        console.error("Monitor error:", err);
      });
  }, intervalMs);
}

// Heartbeat pinger - ensures server connection is alive
function startHeartbeat(intervalMs = 1500) {
  if (heartbeatTimer) clearInterval(heartbeatTimer);
  const tick = async () => {
    try {
      const res = await fetch("/heartbeat", { cache: "no-store" });
      if (res.status === 401) {
        redirectToLogin();
      }
    } catch (err) {
      console.warn("Heartbeat failed:", err);
    }
  };
  tick();
  heartbeatTimer = setInterval(tick, intervalMs);
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
      setState(data.state);
    }
  } catch (err) {
    console.warn("User status poll failed:", err);
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
    stateStream.onopen = () => {
      stopStatePolling();
    };
    stateStream.onmessage = (ev) => {
      try {
        const data = JSON.parse(ev.data || "{}");
        if (data.state) {
          setState(data.state);
        }
      } catch (e) {
        console.warn("User state stream parse error:", e);
      }
    };
    stateStream.onerror = () => {
      if (stateStream) {
        stateStream.close();
        stateStream = null;
      }
      startStatePolling();
    };
  } catch (err) {
    console.warn("User state stream failed to start:", err);
    startStatePolling();
  }
}

// Unified control - sends control commands to the backend
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
    console.error("Control error:", err);
    return { error: String(err) };
  }
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
        redirectToLogin();
      } else {
        return response.json().then((data) => {
        alert("Error: " + data.error);
        });
      }
    })
    .catch((err) => {
      console.error("Disconnect failed:", err);
      redirectToLogin();
    });
}

// DOM initialization - binds UI actions after DOM load
window.addEventListener("DOMContentLoaded", () => {
  loadControls();
  updateStateUi();
  applyControlLocks();
  startHeartbeat(4000);
  startMonitorPolling();
  startStateStream();
  startStatePolling();

  const disconnectBtn = document.getElementById("disconnectBtn");
  if (disconnectBtn) {
    disconnectBtn.addEventListener("click", disconnectDevice);
  } else {
    console.warn("disconnectBtn not found in DOM");
  }

  const editBtn = document
    .getElementById("userMenu")
    ?.querySelector("button:nth-child(1)");
  editBtn?.addEventListener("click", () => {
    document.getElementById("userModal").style.display = "flex";
    document.getElementById("userMenu").style.display = "none";
  });
});

// UI version check - forces UI cache refresh if outdated
const REQUIRED_VERSION = "v3";

window.addEventListener("load", () => {
  const localVersion = localStorage.getItem("ui_version");
  if (localVersion !== REQUIRED_VERSION) {
    localStorage.setItem("ui_version", REQUIRED_VERSION);
    alert(
      "Please refresh the page using Ctrl+F5 or clear your browser cache to load the latest interface."
    );
  }
});
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
  try {
    redirectToLogin();
  } catch (e) {
    // ignore navigation errors
  }
}

// Fire on real page teardown (mobile-friendly)
window.addEventListener("pagehide", sendInstantLogout);
// Also fire when the tab just goes to background
document.addEventListener("visibilitychange", () => {
  if (document.visibilityState === "hidden") sendInstantLogout();
});

