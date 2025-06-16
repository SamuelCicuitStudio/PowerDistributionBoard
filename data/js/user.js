// Tab switching logic
const tabs = document.querySelectorAll(".tab");
const contents = document.querySelectorAll(".content");
document.querySelector('.sidebar .tab:nth-child(2)').style.display = "none"; // Hide manual tab initially

function switchTab(index) {
  tabs.forEach((tab, i) => {
    tab.classList.toggle("active", i === index);
    contents[i].classList.toggle("active", i === index);
  });
}

// Toggle between Auto and Manual mode
function toggleMode() {
  const dot = document.querySelector('.status-dot');
  const isManual = document.getElementById('modeToggle').checked;

  document.querySelector('.sidebar .tab:nth-child(2)').style.display = isManual ? "block" : "none";
  switchTab(isManual ? 1 : 0);

  dot.title = isManual ? "Manual Mode" : "Auto Mode";
  dot.style.backgroundColor = isManual ? "#ffa500" : "#00ff80";
  dot.style.boxShadow = `0 0 6px ${isManual ? "#ffa500" : "#00ff80"}`;
}

// LT Toggle logic
function toggleLT() {
  alert("LT Toggle switched to " + (document.getElementById('ltToggle').checked ? "ON" : "OFF"));
}

// Start/Shutdown system
function startSystem() {
  alert("System Started");
}

function shutdownSystem() {
  alert("System Shutdown");
}

// User menu toggle
function toggleUserMenu() {
  const menu = document.getElementById("userMenu");
  menu.style.display = (menu.style.display === "block") ? "none" : "block";
}

// Hide menu if clicking outside
document.addEventListener("click", function (e) {
  const menu = document.getElementById("userMenu");
  const icon = document.querySelector(".user-icon");
  if (!icon.contains(e.target) && !menu.contains(e.target)) {
    menu.style.display = "none";
  }
});

// Scroll logic for manual outputs container
const manualScrollArea = document.querySelector('.manual-outputs');
manualScrollArea.addEventListener('wheel', function (e) {
  if (!e.shiftKey) {
    e.preventDefault();
    manualScrollArea.scrollLeft += e.deltaY;
  }
});

// Manual outputs generation
const manualOutputs = document.getElementById("manualOutputs");
for (let i = 1; i <= 9; i++) {
  const item = document.createElement("div");
  item.className = "manual-item";
  item.innerHTML = `
    <span>Output ${i}</span>
    <label class="switch">
      <input type="checkbox" onchange="toggleLED(this)">
      <span class="slider"></span>
    </label>
    <div class="led"></div>
  `;
  manualOutputs.appendChild(item);
}

// LED toggle handler
function toggleLED(input) {
  const led = input.parentElement.nextElementSibling;
  led.classList.toggle("active", input.checked);
}

// User modal open/close/save
function closeUserModal() {
  document.getElementById("userModal").style.display = "none";
}

function saveUserSettings() {
  const currentPassword = document.getElementById("currentPassword").value;
  const newPassword = document.getElementById("newPassword").value;
  const newId = document.getElementById("newId").value;

  // Placeholder logic — replace with actual save
  alert(`Submitted:\nCurrent: ${currentPassword}\nNew: ${newPassword}\nID: ${newId}`);
  closeUserModal();
}

// Ensure event listeners bind after DOM is ready
window.addEventListener("DOMContentLoaded", () => {
  const editBtn = document.getElementById("userMenu").querySelector("button:nth-child(1)");
  editBtn.addEventListener("click", () => {
    document.getElementById("userModal").style.display = "flex";
    document.getElementById("userMenu").style.display = "none";
  });
});
