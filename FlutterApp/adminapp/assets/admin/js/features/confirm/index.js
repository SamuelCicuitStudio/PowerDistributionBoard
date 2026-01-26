import { qs } from "../../core/dom.js";

export function initConfirm() {
  const confirmBar = qs("[data-confirm]");
  if (!confirmBar) return null;

  const text = qs("[data-confirm-text]", confirmBar);
  const yesBtn = qs("[data-confirm-yes]", confirmBar);
  const noBtn = qs("[data-confirm-no]", confirmBar);
  let onConfirm = null;

  const close = () => {
    confirmBar.classList.remove("is-visible");
    confirmBar.setAttribute("aria-hidden", "true");
    onConfirm = null;
  };

  const open = (message, handler) => {
    if (text) text.textContent = message;
    onConfirm = handler || null;
    confirmBar.classList.add("is-visible");
    confirmBar.setAttribute("aria-hidden", "false");
  };

  if (yesBtn) {
    yesBtn.addEventListener("click", () => {
      if (typeof onConfirm === "function") {
        onConfirm();
      }
      close();
    });
  }

  if (noBtn) {
    noBtn.addEventListener("click", close);
  }

  return { open, close };
}
