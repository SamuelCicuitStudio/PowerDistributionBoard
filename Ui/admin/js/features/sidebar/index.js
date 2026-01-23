import { qs } from "../../core/dom.js";

export function initSidebarActions() {
  const resetBtn = qs("[data-sidebar-action='reset']");
  const t = (key, vars, fallback) => {
    if (window.__i18n?.t) {
      const value = window.__i18n.t(key, vars);
      if (value && value !== key) return value;
    }
    return fallback ?? key;
  };
  if (resetBtn) {
    resetBtn.addEventListener("click", () => {
      const confirm = window.__confirm?.open;
      if (!confirm) return;
      confirm(t("sidebar.confirm.reset", null, "Reset device?"), () => {
        window.__toast?.show?.(
          t("sidebar.toast.resetQueued", null, "Reset command queued"),
          "success",
        );
      });
    });
  }
}
