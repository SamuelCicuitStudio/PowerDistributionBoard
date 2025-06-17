// === UI TAB LOGIC ===
const tabs = document.querySelectorAll(".tab");
const contents = document.querySelectorAll(".content");
document.querySelector('.sidebar .tab:nth-child(2)').style.display = "none"; // Hide Manual by default

function switchTab(index) {
  tabs.forEach((tab, i) => {
    tab.classList.toggle("active", i === index);
    contents[i].classList.toggle("active", i === index);
  });
}

function toggleMode() {
  const isManual = document.getElementById('modeToggle').checked;
  const dot = document.querySelector('.status-dot');

  // UI updates
  document.querySelector('.sidebar .tab:nth-child(2)').style.display = isManual ? "block" : "none";
  switchTab(isManual ? 1 : 0);

  dot.title = isManual ? "Manual Mode" : "Auto Mode";
  dot.style.backgroundColor = isManual ? "#ffa500" : "#00ff80";
  dot.style.boxShadow = `0 0 6px ${dot.style.backgroundColor}`;

  // Send to server
  sendControlCommand("set", "mode", isManual);
}

// === LT TOGGLE ===
function toggleLT() {
  const isOn = document.getElementById('ltToggle').checked;

  // Send the proper control command to the server
  sendControlCommand("set", "ledFeedback", isOn);

  // Optional: show feedback
  console.log(`LT Toggle switched to ${isOn ? "ON" : "OFF"}`);
}
// === SYSTEM CONTROLS ===
function startSystem() {
  sendControlCommand("set", "systemStart", true);
}

function shutdownSystem() {
  sendControlCommand("set", "systemShutdown", true);
}

// === USER MENU ===
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

// === MANUAL OUTPUT SCROLLING ===
const manualScrollArea = document.querySelector('.manual-outputs');
manualScrollArea?.addEventListener('wheel', function (e) {
  if (!e.shiftKey) {
    e.preventDefault();
    manualScrollArea.scrollBy({ left: e.deltaY, behavior: 'smooth' });
  }
});

// === MANUAL OUTPUTS LOADER ===
async function loadControls() {
  try {
    const res = await fetch("/load_controls");
    const data = await res.json();

    console.log("Fetched config:", data);  // <== Confirm this in browser console

    // === LT Toggle update ===
    const ltToggle = document.getElementById("ltToggle");
    if (ltToggle) {
      ltToggle.checked = !!data.ledFeedback;  // Cast to boolean just in case
      console.log(`LT toggle set to: ${ltToggle.checked}`);
    } else {
      console.warn("LT toggle element not found in DOM!");
    }

    // === Manual outputs code unchanged ===
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
            <input type="checkbox" ${isChecked ? "checked" : ""} onchange="handleOutputToggle(${outputIndex}, this)">
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


// Called by dynamically generated switches
function handleOutputToggle(index, checkbox) {
  const led = checkbox.parentElement.nextElementSibling;
  const isOn = checkbox.checked;

  // Update visual feedback
  led.classList.toggle("active", isOn);

  // Send control command
  sendControlCommand("set", `output${index}`, isOn);
}
// === LED FEEDBACK ===
function toggleLED(input) {
  const led = input.parentElement.nextElementSibling;
  led.classList.toggle("active", input.checked);
}

// === USER MODAL ===
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

// === GAUGE RENDERING ===
function updateGauge(id, value, unit, maxValue) {
  const display = document.getElementById(id);
  const stroke = display.closest('svg').querySelector('path.gauge-fg');

  const percent = Math.min((value / maxValue) * 100, 100);
  stroke.setAttribute("stroke-dasharray", `${percent}, 100`);
  display.textContent = `${value}${unit}`;
}

// === HEARTBEAT PINGER ===
function startHeartbeat(intervalMs = 3000) {
  setInterval(() => {
    fetch("/heartbeat")
      .then(res => res.text())
      .then(text => {
        if (text !== "alive") {
          console.warn("Unexpected heartbeat:", text);
          window.location.href = "/";  // fallback in case of invalid response
        }
      })
      .catch(err => {
        console.error("Heartbeat error:", err);
        window.location.href = "/";  // fallback on network error
      });
  }, intervalMs);
}

// === LIVE DATA POLLER ===
function startMonitorPolling(intervalMs = 1000) {
  setInterval(() => {
    fetch("/monitor")
      .then(res => res.json())
      .then(data => {
        updateGauge("voltageValue", data.capVoltage, "V", 400);
        updateGauge("currentValue", data.current, "A", 100);

        const temps = data.temperatures || [];
        if (temps[0]) updateGauge("temp1Value", temps[0], "°C", 150);
        if (temps[1]) updateGauge("temp2Value", temps[1], "°C", 150);
        if (temps[2]) updateGauge("temp3Value", temps[2], "°C", 150);
        if (temps[3]) updateGauge("temp4Value", temps[3], "°C", 150);
      })
      .catch(err => console.error("Monitor error:", err));
  }, intervalMs);
}

// === UNIFIED CONTROL ===
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
// === DISCONNECT FUNCTION ===

function disconnectDevice() {
  fetch("/disconnect", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ action: "disconnect" }),
    redirect: "follow"
  })
  .then(response => {
    if (response.redirected) {
      window.location.href = response.url; // Redirect to login page
    } else {
      return response.json().then(data => {
        alert(data.error || "Unexpected response");
      });
    }
  })
  .catch(err => {
    console.error("Disconnect failed:", err);
    window.location.href = "/login.html"; // Fallback redirect
  });
}

window.addEventListener("DOMContentLoaded", () => {
  loadControls();
  startHeartbeat(); // Uncomment if needed
  startMonitorPolling();

  // 🔧 Bind disconnect button
  const disconnectBtn = document.getElementById("disconnectBtn");
  if (disconnectBtn) {
    disconnectBtn.addEventListener("click", disconnectDevice);
  } else {
    console.warn("⚠️ disconnectBtn not found in DOM");
  }

  const editBtn = document.getElementById("userMenu")?.querySelector("button:nth-child(1)");
  editBtn?.addEventListener("click", () => {
    document.getElementById("userModal").style.display = "flex";
    document.getElementById("userMenu").style.display = "none";
  });
});
