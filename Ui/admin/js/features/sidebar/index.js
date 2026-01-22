import { qs } from "../../core/dom.js";

export function initSidebarActions() {
  const resetBtn = qs("[data-sidebar-action='reset']");
  if (resetBtn) {
    resetBtn.addEventListener("click", () => {
      const confirm = window.__confirm?.open;
      if (!confirm) return;
      confirm("Reset device?", () => {
        window.__toast?.show?.("Reset command queued", "success");
      });
    });
  }
}
