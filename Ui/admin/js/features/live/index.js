import { qs, qsa } from "../../core/dom.js";
import { initLiveChart } from "./chart.js";

function setOpen(contentRoot, overlay, open) {
  if (!contentRoot || !overlay) return;
  overlay.classList.toggle("is-open", open);
  overlay.setAttribute("aria-hidden", String(!open));
  contentRoot.classList.toggle("is-live", open);
}

export function initLiveOverlay() {
  const overlay = qs("[data-live-overlay]");
  if (!overlay) return;

  const contentRoot = overlay.closest(".content");
  const liveButtons = qsa("[data-sb='live-btn']");
  const closeBtn = qs("[data-live-close]", overlay);
  const liveShell = qs("[data-live-wires]", overlay);
  const wireTargets = qsa(
    ".wire-line[data-wire], .wire-dot[data-wire], .chart-pill.wire[data-wire]",
    overlay,
  );

  const applySelection = () => {
    if (!liveShell) return;
    const raw = liveShell.dataset.liveWires || "";
    const selected = raw
      .split(",")
      .map((value) => value.trim())
      .filter(Boolean);
    wireTargets.forEach((target) => {
      const id = target.dataset.wire;
      target.classList.toggle("is-selected", selected.includes(id));
    });
  };

  liveButtons.forEach((button) => {
    button.addEventListener("click", () => setOpen(contentRoot, overlay, true));
  });

  if (closeBtn) {
    closeBtn.addEventListener("click", () =>
      setOpen(contentRoot, overlay, false),
    );
  }

  overlay.addEventListener("click", (event) => {
    if (event.target === overlay) {
      setOpen(contentRoot, overlay, false);
    }
  });

  document.addEventListener("keydown", (event) => {
    if (event.key === "Escape") {
      setOpen(contentRoot, overlay, false);
    }
  });

  document.addEventListener("click", (event) => {
    const tabButton = event.target.closest(".tab");
    if (tabButton) {
      setOpen(contentRoot, overlay, false);
    }
  });

  applySelection();
  initLiveChart();
}
