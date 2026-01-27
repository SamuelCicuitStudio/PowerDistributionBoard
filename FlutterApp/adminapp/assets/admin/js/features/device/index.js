import { qs, qsa } from "../../core/dom.js";

function labelFor(root, id) {
  const label = qs(`label[for="${id}"]`, root);
  return label?.textContent?.trim() || id;
}

export function initDeviceTab() {
  const panel = qs('[data-tab-panel="device"]');
  if (!panel) return null;
  const root = qs(".device-tab", panel);
  if (!root) return null;

  const showToast = (message, state = "success") => {
    window.__toast?.show?.(message, state);
  };
  const t = (key, vars, fallback) => {
    if (window.__i18n?.t) {
      const value = window.__i18n.t(key, vars);
      if (value && value !== key) return value;
    }
    return fallback ?? key;
  };

  const fields = qsa("input, select, textarea", root).filter((el) => el.id);
  fields.forEach((field) => {
    field.addEventListener("change", () => {
      const label = labelFor(root, field.id);
      showToast(t("device.toast.updated", { label }, `${label} updated.`));
    });
  });

  return {};
}
