// ========================================================
// ===============      TAB SWITCHING      ===============
// ========================================================
const tabs = document.querySelectorAll(".tab");
const contents = document.querySelectorAll(".content");

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
    wifiPassword
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
  container.addEventListener("wheel", (e) => {
    if (e.deltaY !== 0) {
      e.preventDefault();
      container.scrollLeft += e.deltaY * 0.3; // smoothness multiplier
    }
  }, { passive: false });
}

function toggleUserMenu() {
  const menu = document.getElementById("userMenu");
  menu.style.display = (menu.style.display === "block") ? "none" : "block";
}

document.addEventListener("click", function (e) {
  const menu = document.getElementById("userMenu");
  const icon = document.querySelector(".user-icon");
  if (!icon.contains(e.target) && !menu.contains(e.target)) {
    menu.style.display = "none";
  }
});

function toggleMode() {
  const isManual = document.getElementById("modeToggle").checked;
  const dot = document.querySelector(".status-dot");

  dot.title = isManual ? "Manual Mode" : "Auto Mode";
  dot.style.backgroundColor = isManual ? "#ffa500" : "#00ff80";
  dot.style.boxShadow = `0 0 6px ${dot.style.backgroundColor}`;

  sendControlCommand("set", "mode", isManual);
}

function toggleLT() {
  const isOn = document.getElementById("ltToggle").checked;
  sendControlCommand("set", "ledFeedback", isOn);
}

function handleOutputToggle(index, checkbox) {
  const led = checkbox.parentElement.nextElementSibling;
  const isOn = checkbox.checked;

  led.classList.toggle("active", isOn);  // Visual feedback
  sendControlCommand("set", `output${index}`, isOn);  // Send command
}

function startSystem() {
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

function startHeartbeat(intervalMs = 3000) {
  setInterval(() => {
    fetch("/heartbeat")
      .then(res => res.text())
      .then(text => {
        if (text !== "alive") {
          console.warn("Unexpected heartbeat:", text);
          window.location.href = "http://192.168.4.1/login";
        }
      })
      .catch(err => {
        console.error("Heartbeat error:", err);
        window.location.href = "http://192.168.4.1/login";
      });
  }, intervalMs);
}

document.getElementById("relayToggle").addEventListener("change", function () {
  sendControlCommand("set", "relay", this.checked);
});

document.getElementById("bypassToggle").addEventListener("change", function () {
  sendControlCommand("set", "bypass", this.checked);
});

document.getElementById("fanSlider").addEventListener("input", function () {
  sendControlCommand("set", "fanSpeed", parseInt(this.value));
});

function toggleOutput(index, state) {
  sendControlCommand("set", `output${index}`, state);
}


function updateOutputAccess(index, newState) {
  // Send only the changed output's access value
  sendControlCommand("set", `Access${index}`, newState);
}




function sendControlCommand(action, target, value) {
  const payload = { action, target };
  if (value !== undefined) payload.value = value;

  fetch("/control", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(payload)
  })
    .then(res => res.json())
    .then(data => {
      if (data.status === "ok") {
        console.log(`[✔] '${action}' on '${target}' succeeded`);
      } else if (data.state) {
        console.log(`[ℹ] State: ${data.state}`);
      } else if (data.error) {
        console.warn(`[✖] ${data.error}`);
      }
    })
    .catch(err => console.error("Control error:", err));
}

async function loadControls() {
  try {
    const res = await fetch("/load_controls");
    const data = await res.json();
    console.log("Fetched config:", data);

    const access = data.outputAccess || {};
    const states = data.outputs || {};

    // ── Update LT toggle switch ──
    const ltToggle = document.getElementById("ltToggle");
    if (ltToggle) {
      ltToggle.checked = !!data.ledFeedback;
    }

    // ── Update Ready / OFF LED indicators ──
    const readyLed = document.getElementById("readyLed");
    const offLed = document.getElementById("offLed");
    if (readyLed) readyLed.style.backgroundColor = data.ready ? "limegreen" : "gray";
    if (offLed) offLed.style.backgroundColor = data.off ? "red" : "gray";

    // ── Update Manual Control toggles ──
    for (let i = 1; i <= 10; i++) {
      const checked = !!states[`output${i}`];
      const checkbox = document.querySelector(`#manualOutputs .manual-item:nth-child(${i}) input[type="checkbox"]`);
      const led = document.querySelector(`#manualOutputs .manual-item:nth-child(${i}) .led`);

      if (checkbox) checkbox.checked = checked;
      if (led) led.classList.toggle("active", checked);
    }

    // ── Update Output Access (User Settings tab) ──
    for (let i = 1; i <= 10; i++) {
      const checkbox = document.querySelector(`#userAccessGrid .manual-item:nth-child(${i}) input[type="checkbox"]`);
      if (checkbox) {
        checkbox.checked = !!access[`output${i}`];
      }
    }

    // ── Update Device Settings Tab ──
    document.getElementById("desiredVoltage").value   = data.desiredVoltage ?? "";
    document.getElementById("acFrequency").value      = data.acFrequency ?? "";
    document.getElementById("chargeResistor").value   = data.chargeResistor ?? "";
    document.getElementById("dcVoltage").value        = data.dcVoltage ?? "";
    document.getElementById("onTime").value           = data.onTime ?? "";
    document.getElementById("offTime").value          = data.offTime ?? "";

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
      "Content-Type": "application/json"
    },
    body: JSON.stringify({
      current: currentPassword,
      username: newId,
      password: newPassword
    })
  })
    .then(res => res.json())
    .then(data => {
      if (data.status) {
        alert("✅ " + data.status);
        closeUserModal();
      } else if (data.error) {
        alert("⚠️ " + data.error);
      }
    })
    .catch(err => {
      console.error("Credential update failed:", err);
      alert("Error communicating with device.");
    });
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
      wifiPassword
    })
  })
    .then(res => res.json())
    .then(data => {
      if (data.status) alert("✅ " + data.status);
      else if (data.error) alert("⚠️ " + data.error);
    })
    .catch(err => {
      console.error("Admin settings update failed:", err);
      alert("Error communicating with device.");
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
    redirect: "follow"
  })
    .then(response => {
      if (response.redirected) {
        window.location.href = response.url;
      } else {
        return response.json().then(data => {
          alert(data.error || "Unexpected response");
        });
      }
    })
    .catch(err => {
      console.error("Disconnect failed:", err);
      window.location.href = "/login.html";
    });
}

function updateGauge(id, value, unit, maxValue) {
  const display = document.getElementById(id);
  const stroke = display.closest('svg').querySelector('path.gauge-fg');

  if (value === "Off") {
    stroke.setAttribute("stroke-dasharray", `0, 100`);
    display.textContent = "Off";
    return;
  }

  const percent = Math.min((parseFloat(value) / maxValue) * 100, 100);
  stroke.setAttribute("stroke-dasharray", `${percent}, 100`);
  display.textContent = `${value}${unit}`;
}

function startMonitorPolling(intervalMs = 1000) {
  setInterval(() => {
    fetch("/monitor")
      .then(res => res.json())
      .then(data => {
        const voltage = parseFloat(data.capVoltage).toFixed(2);
        updateGauge("voltageValue", voltage, "V", 400);

        let rawCurrent = parseFloat(data.current);
        if (isNaN(rawCurrent)) rawCurrent = 0;
        const clampedCurrent = Math.max(0, Math.min(100, rawCurrent)).toFixed(2);
        updateGauge("currentValue", clampedCurrent, "A", 100);

        const temps = data.temperatures || [];
        updateGauge("temp1Value", temps[0] === -127 ? "Off" : parseFloat(temps[0]).toFixed(2), "°C", 150);
        updateGauge("temp2Value", temps[1] === -127 ? "Off" : parseFloat(temps[1]).toFixed(2), "°C", 150);
        updateGauge("temp3Value", temps[2] === -127 ? "Off" : parseFloat(temps[2]).toFixed(2), "°C", 150);
        updateGauge("temp4Value", temps[3] === -127 ? "Off" : parseFloat(temps[3]).toFixed(2), "°C", 150);

        const readyLed = document.getElementById("readyLed");
        const offLed = document.getElementById("offLed");

        if (readyLed) readyLed.style.backgroundColor = data.ready ? "limegreen" : "gray";
        if (offLed) offLed.style.backgroundColor = data.off ? "red" : "gray";

        // 🌀 Fan Speed Slider Update
        const fanSlider = document.getElementById("fanSlider");
        if (fanSlider && typeof data.fanSpeed === "number") {
          fanSlider.value = data.fanSpeed;
        }
      })
      .catch(err => { 
        console.error("Monitor error:", err);
      });
  }, intervalMs);
}
function saveDeviceSettings() {
  const desiredVoltage = parseFloat(document.getElementById("desiredVoltage").value);
  const acFrequency = parseInt(document.getElementById("acFrequency").value);
  const chargeResistor = parseFloat(document.getElementById("chargeResistor").value);
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
    alert("⚠️ Please enter valid numeric values for all fields.");
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
}
window.addEventListener("DOMContentLoaded", () => {
  // Existing initializations...
  renderAllOutputs("manualOutputs", true);
  renderAllOutputs("userAccessGrid", false);

  enableDragScroll("manualOutputs");
  enableDragScroll("userAccessGrid");

  startHeartbeat();
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
