import { qs, qsa } from "../../core/dom.js";

function renderList(listNode, items, emptyLabel) {
  if (!listNode) return;
  listNode.innerHTML = "";
  if (!items.length) {
    const empty = document.createElement("div");
    empty.className = "alert-empty";
    empty.textContent = emptyLabel;
    listNode.appendChild(empty);
    return;
  }

  items.forEach((item) => {
    const card = document.createElement("div");
    card.className = "alert-card";
    const message = document.createElement("span");
    message.className = "alert-message";
    message.textContent = item.message || "--";
    const meta = document.createElement("span");
    meta.className = "alert-meta";
    meta.textContent = item.time || "--";
    card.append(message, meta);
    listNode.appendChild(card);
  });
}

function updateOverlay(overlay, items, label) {
  if (!overlay) return;
  const current = qs("[data-alert-current]", overlay);
  const currentMeta = qs("[data-alert-current-meta]", overlay);
  const list = qs("[data-alert-list]", overlay);

  const currentItem = items[0];
  if (current) {
    current.textContent = currentItem ? currentItem.message : `No ${label}`;
  }
  if (currentMeta) {
    currentMeta.textContent = currentItem ? currentItem.time : "--";
  }

  const history = items.slice(1, 11);
  renderList(list, history, `No ${label.toLowerCase()} yet`);
}

export function initAlerts() {
  const warnOverlay = qs('[data-alert-overlay="warn"]');
  const errOverlay = qs('[data-alert-overlay="err"]');
  const warnTrigger = qs("[data-sb='warn-pill']");
  const errTrigger = qs("[data-sb='err-pill']");
  const liveErrTrigger = qs("[data-live-error]");
  const contentRoot = qs(".content");

  if (!warnOverlay && !errOverlay) return null;

  const closeAll = () => {
    qsa(".alert-overlay.is-open").forEach((node) => {
      node.classList.remove("is-open");
      node.setAttribute("aria-hidden", "true");
    });
    if (contentRoot) {
      contentRoot.classList.remove("is-alert");
    }
  };

  const openOverlay = (overlay) => {
    if (!overlay) return;
    closeAll();
    overlay.classList.add("is-open");
    overlay.setAttribute("aria-hidden", "false");
    if (contentRoot) {
      contentRoot.classList.add("is-alert");
    }
  };

  const setupOverlay = (overlay) => {
    if (!overlay) return;
    overlay.addEventListener("click", (event) => {
      if (event.target === overlay) {
        closeAll();
      }
    });
    overlay.querySelectorAll("[data-alert-close]").forEach((btn) => {
      btn.addEventListener("click", closeAll);
    });
  };

  setupOverlay(warnOverlay);
  setupOverlay(errOverlay);

  if (warnTrigger) {
    warnTrigger.addEventListener("click", () => {
      if (warnOverlay && warnOverlay.classList.contains("is-open")) {
        closeAll();
        return;
      }
      openOverlay(warnOverlay);
    });
  }
  if (errTrigger) {
    errTrigger.addEventListener("click", () => {
      if (errOverlay && errOverlay.classList.contains("is-open")) {
        closeAll();
        return;
      }
      openOverlay(errOverlay);
    });
  }
  if (liveErrTrigger) {
    liveErrTrigger.addEventListener("click", () => {
      if (errOverlay && errOverlay.classList.contains("is-open")) {
        closeAll();
        return;
      }
      openOverlay(errOverlay);
    });
  }

  document.addEventListener("keydown", (event) => {
    if (event.key === "Escape") closeAll();
  });

  document.addEventListener("click", (event) => {
    const tabButton = event.target.closest(".tab");
    if (tabButton) {
      closeAll();
    }
  });

  return {
    setWarnings: (items = []) =>
      updateOverlay(
        warnOverlay,
        Array.isArray(items) ? items : [],
        "warnings",
      ),
    setErrors: (items = []) =>
      updateOverlay(
        errOverlay,
        Array.isArray(items) ? items : [],
        "errors",
      ),
    openWarnings: () => openOverlay(warnOverlay),
    openErrors: () => openOverlay(errOverlay),
    close: closeAll,
  };
}
