import { qs } from "../../core/dom.js";

export function initToast() {
  const toast = qs("[data-toast]");
  if (!toast) return null;

  const text = qs("[data-toast-text]", toast);
  let timer = null;

  const show = (message, state = "success") => {
    if (!toast || !text) return;
    text.textContent = message;
    toast.dataset.state = state;
    toast.classList.add("is-visible");
    toast.setAttribute("aria-hidden", "false");
    if (timer) {
      clearTimeout(timer);
    }
    timer = setTimeout(() => {
      toast.classList.remove("is-visible");
      toast.setAttribute("aria-hidden", "true");
    }, 2400);
  };

  return { show };
}
