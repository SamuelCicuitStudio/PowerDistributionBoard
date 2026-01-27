import { initI18n } from "./services/i18n.js";
import "./services/session.js";

const form = document.getElementById("loginForm");
const usernameEl = document.getElementById("username");
const passwordEl = document.getElementById("password");
const statusEl = document.querySelector("[data-login-status]");
const infoBtn = document.querySelector("[data-login-info-btn]");
const infoPanel = document.querySelector("[data-login-info-panel]");
const toggleBtn = document.getElementById("togglePassword");

const { t } = initI18n();
document.title = t("login.title");

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

function redirectToAdmin(token, role) {
  const base = new URLSearchParams(window.location.search).get("base");
  const url = new URL("admin.html", window.location.href);
  if (base) url.searchParams.set("base", base);
  if (token) url.searchParams.set("token", token);
  if (role) url.searchParams.set("role", role);
  window.location.href = url.toString();
}

const alreadyConnectedMessages = new Set([
  "Already connected",
  "Deja connecte",
  "Gia connesso",
]);

function isAlreadyConnectedError(message) {
  if (!message) return false;
  return alreadyConnectedMessages.has(message);
}

async function tryServerLogin(username, password) {
  const headers = window.pbCborHeaders
    ? window.pbCborHeaders()
    : { "Content-Type": "application/json", Accept: "application/json" };
  const body = window.pbEncodeCbor
    ? window.pbEncodeCbor({ username, password })
    : JSON.stringify({ username, password });

  const doFetch = window.pbFetch || fetch;
  const response = await doFetch("/connect", {
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

async function handleLogin(username, password, allowRetry) {
  const { response, payload } = await tryServerLogin(username, password);
  if (!response.ok) {
    const message = payload.error || t("login.error.failed");
    if (allowRetry && isAlreadyConnectedError(message)) {
      if (window.pbDisconnect) {
        try {
          await window.pbDisconnect();
        } catch (error) {
          console.warn("Disconnect failed:", error);
        }
      }
      if (window.pbClearToken) window.pbClearToken();
      return handleLogin(username, password, false);
    }
    if (window.pbClearToken) window.pbClearToken();
    goToFailed(message);
    return;
  }
  if (!payload.token) {
    if (window.pbClearToken) window.pbClearToken();
    goToFailed(t("login.error.missingToken"));
    return;
  }
  if (payload.role !== "admin") {
    if (window.pbSetToken) window.pbSetToken(payload.token);
    if (window.pbSetRole) window.pbSetRole(payload.role);
    if (window.pbDisconnect) {
      try {
        await window.pbDisconnect();
      } catch (error) {
        console.warn("Disconnect failed:", error);
      }
    }
    if (window.pbClearToken) window.pbClearToken();
    goToFailed(t("login.error.adminRequired"));
    return;
  }
  if (window.pbSetToken) window.pbSetToken(payload.token);
  if (window.pbSetRole) window.pbSetRole(payload.role);
  redirectToAdmin(payload.token, payload.role);
}

async function submitLogin() {
  const username = usernameEl?.value.trim() || "";
  const password = passwordEl?.value.trim() || "";

  if (!username || !password) {
    setStatus(t("login.error.missingFields"));
    return;
  }

  clearStatus();

  try {
    await handleLogin(username, password, true);
  } catch (err) {
    console.warn("Login request failed:", err);
    goToFailed(t("login.error.failed"));
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
    populateDeviceInfo();
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
    const doFetch = window.pbFetch || fetch;
    const response = await doFetch("/device_info", {
      cache: "no-store",
      headers: window.pbCborHeaders ? window.pbCborHeaders({ body: false }) : undefined,
    });
    if (!response.ok) return;
    const data = window.pbReadCbor ? await window.pbReadCbor(response) : null;
    if (!data) return;
    const idEl = document.getElementById("infoDeviceId");
    const swEl = document.getElementById("infoSwVer");
    const hwEl = document.getElementById("infoHwVer");
    if (idEl && data.deviceId !== undefined) {
      idEl.textContent = t("login.info.deviceId", { value: data.deviceId });
    }
    if (swEl && data.sw !== undefined) {
      swEl.textContent = t("login.info.sw", { value: data.sw });
    }
    if (hwEl && data.hw !== undefined) {
      hwEl.textContent = t("login.info.hw", { value: data.hw });
    }
  } catch (e) {
    console.warn("device_info fetch failed", e);
  }
}

bindForm();
bindPasswordToggle();
bindDeviceInfo();
populateDeviceInfo();

