// ───────────────────────────────────────────────────────────────
// 📁 UI TAB LOGIC — Handles sidebar tab switching and manual mode visibility
// ───────────────────────────────────────────────────────────────
const tabs = document.querySelectorAll(".tab");
const contents = document.querySelectorAll(".content");
let lastState = "Shutdown";
let stateStream = null;
let statePollTimer = null;

// Hide Manual tab by default (index 1)
document.querySelector(".sidebar .tab:nth-child(2)").style.display = "none";

// Switch between tabs based on index
function switchTab(index) {
  tabs.forEach((tab, i) => {
    tab.classList.toggle("active", i === index);
    contents[i].classList.toggle("active", i === index);
  });
}

// Toggle between Auto and Manual mode
function toggleMode() {
  const isManual = document.getElementById("modeToggle").checked;
  const dot = document.querySelector(".status-dot");

  // UI update: show/hide manual tab
  document.querySelector(".sidebar .tab:nth-child(2)").style.display = isManual
    ? "block"
    : "none";
  switchTab(isManual ? 1 : 0);

  // Update status dot
  dot.title = isManual ? "Manual Mode" : "Auto Mode";
  dot.style.backgroundColor = isManual ? "#ffa500" : "#00ff80";
  dot.style.boxShadow = `0 0 6px ${dot.style.backgroundColor}`;

  // Notify backend
  sendControlCommand("set", "mode", isManual);
}

// ───────────────────────────────────────────────────────────────
// 💡 LT TOGGLE — Sends LED feedback toggle state to the server
// ───────────────────────────────────────────────────────────────
function toggleLT() {
  const isOn = document.getElementById("ltToggle").checked;
  sendControlCommand("set", "ledFeedback", isOn);
  console.log(`LT Toggle switched to ${isOn ? "ON" : "OFF"}`);
}

// ───────────────────────────────────────────────────────────────
// ⚡ SYSTEM CONTROLS — Trigger start and shutdown events
// ───────────────────────────────────────────────────────────────
function startSystem() {
  sendControlCommand("set", "systemStart", true);
}

function shutdownSystem() {
  sendControlCommand("set", "systemShutdown", true);
}

// ───────────────────────────────────────────────────────────────
// 👤 USER MENU — Toggles user dropdown and auto-hides on outside click
// ───────────────────────────────────────────────────────────────
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

// ───────────────────────────────────────────────────────────────
// 🎛️ MANUAL OUTPUT SCROLLING — Enables horizontal scroll with mouse
// ───────────────────────────────────────────────────────────────
const manualScrollArea = document.querySelector(".manual-outputs");
manualScrollArea?.addEventListener("wheel", function (e) {
  if (!e.shiftKey) {
    e.preventDefault();
    manualScrollArea.scrollBy({ left: e.deltaY, behavior: "smooth" });
  }
});

// ───────────────────────────────────────────────────────────────
// 🔄 LOAD CONTROLS — Fetch initial UI control states and populate interface
// ───────────────────────────────────────────────────────────────
async function loadControls() {
  try {
    const res = await fetch("/load_controls");
    const data = await res.json();
    console.log("Fetched config:", data); // Debug info

    // ── Update LT toggle switch ──
    const ltToggle = document.getElementById("ltToggle");
    if (ltToggle) {
      ltToggle.checked = !!data.ledFeedback;
      console.log(`LT toggle set to: ${ltToggle.checked}`);
    } else {
      console.warn("LT toggle element not found in DOM!");
    }

    // ── Update Ready / OFF LED indicators ──
    const readyLed = document.getElementById("readyLed");
    const offLed = document.getElementById("offLed");

    if (readyLed)
      readyLed.style.backgroundColor = data.ready ? "limegreen" : "gray";
    else console.warn("Ready LED not found in DOM!");

    if (offLed) offLed.style.backgroundColor = data.off ? "red" : "gray";
    else console.warn("OFF LED not found in DOM!");

    // ── Render manual output switches ──
    const manualOutputs = document.getElementById("manualOutputs");
    manualOutputs.innerHTML = "";

    const access = data.outputAccess || {};
    const states = data.outputs || {};

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

// ───────────────────────────────────────────────────────────────
// 🔘 OUTPUT SWITCH TOGGLE HANDLER — Called on each checkbox change
// ───────────────────────────────────────────────────────────────
function handleOutputToggle(index, checkbox) {
  const led = checkbox.parentElement.nextElementSibling;
  const isOn = checkbox.checked;

  led.classList.toggle("active", isOn); // Visual feedback
  sendControlCommand("set", `output${index}`, isOn); // Send command
}

// ───────────────────────────────────────────────────────────────
// 💡 LED FEEDBACK — Toggles indicator next to LT switch
// ───────────────────────────────────────────────────────────────
function toggleLED(input) {
  const led = input.parentElement.nextElementSibling;
  led.classList.toggle("active", input.checked);
}

// ───────────────────────────────────────────────────────────────
// 👤 USER MODAL — Open, close and save user credentials
// ───────────────────────────────────────────────────────────────
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
        alert("✅ " + data.status);
        closeUserModal();
      } else if (data.error) {
        alert("⚠️ " + data.error);
      }
    })
    .catch((err) => {
      console.error("Credential update failed:", err);
      alert("Error communicating with device.");
    });
}

// ───────────────────────────────────────────────────────────────
// 🧭 GAUGE RENDERING — Updates visual gauges with live values
// ───────────────────────────────────────────────────────────────
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

// ───────────────────────────────────────────────────────────────
// 📡 LIVE DATA POLLER — Fetches and updates live telemetry
// ───────────────────────────────────────────────────────────────
function startMonitorPolling(intervalMs = 1000) {
  setInterval(() => {
    fetch("/monitor")
      .then((res) => res.json())
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
        updateGauge(
          "temp1Value",
          temps[0] === -127 ? "Off" : parseFloat(temps[0]).toFixed(2),
          "°C",
          150
        );
        updateGauge(
          "temp2Value",
          temps[1] === -127 ? "Off" : parseFloat(temps[1]).toFixed(2),
          "°C",
          150
        );
        updateGauge(
          "temp3Value",
          temps[2] === -127 ? "Off" : parseFloat(temps[2]).toFixed(2),
          "°C",
          150
        );
        updateGauge(
          "temp4Value",
          temps[3] === -127 ? "Off" : parseFloat(temps[3]).toFixed(2),
          "°C",
          150
        );

        const readyLed = document.getElementById("readyLed");
        const offLed = document.getElementById("offLed");

        if (readyLed)
          readyLed.style.backgroundColor = data.ready ? "limegreen" : "gray";
        if (offLed) offLed.style.backgroundColor = data.off ? "red" : "gray";
      })
      .catch((err) => {
        console.error("Monitor error:", err);
      });
  }, intervalMs);
}

// ───────────────────────────────────────────────────────────────
// ❤️ HEARTBEAT PINGER — Ensures server connection is alive
// ───────────────────────────────────────────────────────────────
function startHeartbeat(intervalMs = 1500) {
  setInterval(() => {
    fetch("/heartbeat")
      .then((res) => res.text())
      .then((text) => {
        if (text !== "alive") {
          console.warn("Unexpected heartbeat:", text);
          window.location.href = "http://powerboard.local/login";
        }
      })
      .catch((err) => {
        console.error("Heartbeat error:", err);
        window.location.href = "http://powerboard.local/login";
      });
  }, intervalMs);
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
      lastState = data.state;
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
          lastState = data.state;
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

// ───────────────────────────────────────────────────────────────
// 🛠️ UNIFIED CONTROL — Sends control commands to the backend
// ───────────────────────────────────────────────────────────────
function sendControlCommand(action, target, value) {
  const payload = { action, target };
  if (value !== undefined) payload.value = value;

  fetch("/control", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(payload),
  })
    .then((res) => res.json())
    .then((data) => {
      if (data.status === "ok") {
        console.log(`[✔] '${action}' on '${target}' succeeded`);
      } else if (data.state) {
        console.log(`[ℹ] State: ${data.state}`);
      } else if (data.error) {
        console.warn(`[✖] ${data.error}`);
      }
    })
    .catch((err) => console.error("Control error:", err));
}

// ───────────────────────────────────────────────────────────────
// 🔌 DISCONNECT FUNCTION — Terminates session and redirects
// ───────────────────────────────────────────────────────────────
function disconnectDevice() {
  fetch("/disconnect", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ action: "disconnect" }),
    redirect: "follow",
  })
    .then((response) => {
      if (response.redirected) {
        window.location.href = "http://powerboard.local/login";
      } else {
        return response.json().then((data) => {
          alert(data.error || "Unexpected response");
        });
      }
    })
    .catch((err) => {
      console.error("Disconnect failed:", err);
      window.location.href = "http://powerboard.local/login";
    });
}

// ───────────────────────────────────────────────────────────────
// 🧩 DOM INITIALIZATION — Binds UI actions after DOM load
// ───────────────────────────────────────────────────────────────
window.addEventListener("DOMContentLoaded", () => {
  loadControls();
  //startHeartbeat(); // Uncomment if needed
  startMonitorPolling();
  startStateStream();
  startStatePolling();

  const disconnectBtn = document.getElementById("disconnectBtn");
  if (disconnectBtn) {
    disconnectBtn.addEventListener("click", disconnectDevice);
  } else {
    console.warn("⚠️ disconnectBtn not found in DOM");
  }

  const editBtn = document
    .getElementById("userMenu")
    ?.querySelector("button:nth-child(1)");
  editBtn?.addEventListener("click", () => {
    document.getElementById("userModal").style.display = "flex";
    document.getElementById("userMenu").style.display = "none";
  });
});

// ───────────────────────────────────────────────────────────────
// 🔁 UI VERSION CHECK — Forces UI cache refresh if outdated
// ───────────────────────────────────────────────────────────────
const REQUIRED_VERSION = "v2";

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
}

// Fire on real page teardown (mobile-friendly)
window.addEventListener("pagehide", sendInstantLogout);
// Also fire when the tab just goes to background
document.addEventListener("visibilitychange", () => {
  if (document.visibilityState === "hidden") sendInstantLogout();
});
