const MOCK_USER = "admin";
const MOCK_PASS = "admin123";

const form = document.getElementById("loginForm");
const usernameEl = document.getElementById("username");
const passwordEl = document.getElementById("password");
const statusEl = document.querySelector("[data-login-status]");
const infoBtn = document.querySelector("[data-login-info-btn]");
const infoPanel = document.querySelector("[data-login-info-panel]");
const toggleBtn = document.getElementById("togglePassword");

function setStatus(message, state = "err") {
  if (!statusEl) return;
  statusEl.textContent = message || "";
  statusEl.dataset.state = state;
}

function clearStatus() {
  setStatus("", "ok");
}

function goToFailed(message) {
  const url = new URL("login_failed.html", window.location.href);
  if (message) url.searchParams.set("reason", message);
  const base = new URLSearchParams(window.location.search).get("base");
  if (base) url.searchParams.set("base", base);
  window.location.href = url.toString();
}

function redirectToAdmin() {
  const base = new URLSearchParams(window.location.search).get("base");
  const next = "admin.html" + (base ? "?base=" + encodeURIComponent(base) : "");
  window.location.href = next;
}

function mockLogin(username, password) {
  if (username === MOCK_USER && password === MOCK_PASS) {
    redirectToAdmin();
    return true;
  }
  goToFailed("Incorrect username or password.");
  return false;
}

async function tryServerLogin(username, password) {
  const headers = window.pbCborHeaders
    ? window.pbCborHeaders()
    : { "Content-Type": "application/json", Accept: "application/json" };
  const body = window.pbEncodeCbor
    ? window.pbEncodeCbor({ username, password })
    : JSON.stringify({ username, password });

  const response = await fetch("/connect", {
    method: "POST",
    headers,
    body,
  });

  let payload = null;
  if (window.pbReadCbor) {
    payload = await window.pbReadCbor(response);
  } else {
    try {
      payload = await response.json();
    } catch (e) {
      payload = null;
    }
  }

  return { response, payload: payload || {} };
}

async function submitLogin() {
  const username = usernameEl?.value.trim() || "";
  const password = passwordEl?.value.trim() || "";

  if (!username || !password) {
    setStatus("Please enter both username and password.");
    return;
  }

  clearStatus();

  try {
    const { response, payload } = await tryServerLogin(username, password);
    if (!response.ok) {
      if (response.status === 404 || response.status === 0) {
        mockLogin(username, password);
        return;
      }
      const message = payload.error || "Login failed.";
      goToFailed(message);
      return;
    }
    if (!payload.token) {
      goToFailed("Missing session token.");
      return;
    }
    if (payload.role !== "admin") {
      if (window.pbClearToken) window.pbClearToken();
      goToFailed("Admin credentials required.");
      return;
    }
    if (window.pbSetToken) window.pbSetToken(payload.token);
    redirectToAdmin();
  } catch (err) {
    console.warn("Login request failed, using mock:", err);
    mockLogin(username, password);
  }
}

function bindForm() {
  if (form) {
    form.addEventListener("submit", (event) => {
      event.preventDefault();
      submitLogin();
    });
  }

  [usernameEl, passwordEl].forEach((el) => {
    if (!el) return;
    el.addEventListener("keydown", (e) => {
      if (e.key === "Enter") submitLogin();
    });
  });
}

function bindPasswordToggle() {
  if (!toggleBtn || !passwordEl) return;

  const setVisible = (visible) => {
    passwordEl.type = visible ? "text" : "password";
    toggleBtn.classList.toggle("is-showing", visible);
    toggleBtn.setAttribute("aria-pressed", visible ? "true" : "false");
  };

  const down = (event) => {
    setVisible(true);
    event.preventDefault();
    passwordEl.focus({ preventScroll: true });
  };

  const up = () => setVisible(false);

  toggleBtn.addEventListener("pointerdown", down);
  toggleBtn.addEventListener("pointerup", up);
  toggleBtn.addEventListener("pointercancel", up);
  toggleBtn.addEventListener("pointerleave", up);

  toggleBtn.addEventListener("keydown", (event) => {
    if (event.code === "Space" || event.code === "Enter") down(event);
  });
  toggleBtn.addEventListener("keyup", (event) => {
    if (event.code === "Space" || event.code === "Enter") up();
  });

  passwordEl.addEventListener("blur", up);
  document.addEventListener("visibilitychange", () => {
    if (document.visibilityState !== "visible") up();
  });
}

function bindDeviceInfo() {
  if (!infoBtn || !infoPanel) return;

  const togglePanel = () => {
    const isOpen = infoPanel.classList.contains("is-open");
    infoPanel.classList.toggle("is-open", !isOpen);
    infoPanel.setAttribute("aria-hidden", String(isOpen));
  };

  infoBtn.addEventListener("click", (event) => {
    event.stopPropagation();
    togglePanel();
  });

  document.addEventListener("click", (event) => {
    if (!infoPanel.classList.contains("is-open")) return;
    if (infoPanel.contains(event.target) || infoBtn.contains(event.target)) return;
    infoPanel.classList.remove("is-open");
    infoPanel.setAttribute("aria-hidden", "true");
  });
}

async function populateDeviceInfo() {
  try {
    const response = await fetch("/device_info", {
      cache: "no-store",
      headers: window.pbCborHeaders ? window.pbCborHeaders() : undefined,
    });
    if (!response.ok) return;
    const data = window.pbReadCbor ? await window.pbReadCbor(response) : null;
    if (!data) return;
    const idEl = document.getElementById("infoDeviceId");
    const swEl = document.getElementById("infoSwVer");
    const hwEl = document.getElementById("infoHwVer");
    if (idEl && data.deviceId !== undefined) {
      idEl.textContent = "Device ID: " + data.deviceId;
    }
    if (swEl && data.sw !== undefined) {
      swEl.textContent = "SW Version: " + data.sw;
    }
    if (hwEl && data.hw !== undefined) {
      hwEl.textContent = "HW Version: " + data.hw;
    }
  } catch (e) {
    console.warn("device_info fetch failed", e);
  }
}

bindForm();
bindPasswordToggle();
bindDeviceInfo();
populateDeviceInfo();

