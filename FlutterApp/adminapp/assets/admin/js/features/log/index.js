import { qs } from "../../core/dom.js";

export function initLogOverlay() {
  const overlay = qs("[data-log-overlay]");
  if (!overlay) return null;

  const contentRoot = overlay.closest(".content");
  const closeBtn = qs("[data-log-close]", overlay);
  const refreshBtn = qs("[data-log-refresh]", overlay);
  const body = qs("[data-log-body]", overlay);
  const logBtn = qs("[data-live-log]");

  const setOpen = (open) => {
    overlay.classList.toggle("is-open", open);
    overlay.setAttribute("aria-hidden", String(!open));
    if (contentRoot) {
      contentRoot.classList.toggle("is-log", open);
    }
  };

  const close = () => setOpen(false);

  if (logBtn) {
    logBtn.addEventListener("click", () => setOpen(true));
  }
  if (closeBtn) {
    closeBtn.addEventListener("click", close);
  }
  overlay.addEventListener("click", (event) => {
    if (event.target === overlay) close();
  });
  document.addEventListener("keydown", (event) => {
    if (event.key === "Escape") close();
  });
  document.addEventListener("click", (event) => {
    const tabButton = event.target.closest(".tab");
    if (tabButton) close();
  });

  const setLogs = (lines = []) => {
    if (!body) return;
    body.innerHTML = "";
    if (!lines.length) {
      const empty = document.createElement("div");
      empty.className = "log-empty";
      empty.textContent = "No log data";
      body.appendChild(empty);
      return;
    }
    lines.forEach((line) => {
      const entry = document.createElement("div");
      entry.className = "log-entry";
      entry.textContent = line;
      body.appendChild(entry);
    });
  };

  if (refreshBtn) {
    refreshBtn.addEventListener("click", () => {
      window.__log?.refresh?.();
    });
  }

  return { setLogs, open: () => setOpen(true), close, refresh: () => {} };
}
