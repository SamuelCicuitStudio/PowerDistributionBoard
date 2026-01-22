import { qs, qsa } from "../../core/dom.js";

export function initHistoryOverlay() {
  const overlay = qs("[data-history-overlay]");
  if (!overlay) return null;

  const contentRoot = overlay.closest(".content");
  const closeBtn = qs("[data-history-close]", overlay);
  const body = qs("[data-history-body]", overlay);
  const historyBtn = qs("[data-live-history]");

  const setOpen = (open) => {
    overlay.classList.toggle("is-open", open);
    overlay.setAttribute("aria-hidden", String(!open));
    if (contentRoot) {
      contentRoot.classList.toggle("is-history", open);
    }
  };

  const close = () => setOpen(false);

  if (historyBtn) {
    historyBtn.addEventListener("click", () => setOpen(true));
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

  const setRows = (rows = []) => {
    if (!body) return;
    body.innerHTML = "";
    if (!rows.length) {
      const row = document.createElement("tr");
      const cell = document.createElement("td");
      cell.colSpan = 5;
      cell.className = "history-empty";
      cell.textContent = "No sessions yet";
      row.appendChild(cell);
      body.appendChild(row);
      return;
    }
    rows.forEach((item) => {
      const row = document.createElement("tr");
      [item.time, item.duration, item.energy, item.peakP, item.peakI].forEach(
        (value) => {
          const cell = document.createElement("td");
          cell.textContent = value;
          row.appendChild(cell);
        },
      );
      body.appendChild(row);
    });
  };

  return { setRows, open: () => setOpen(true), close };
}
