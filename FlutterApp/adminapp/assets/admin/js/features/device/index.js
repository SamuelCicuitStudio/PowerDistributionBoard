import { qs, qsa } from "../../core/dom.js";

const DEFAULTS = {
  acFrequency: 250,
  chargeResistor: 68,
  currLimit: 8.5,
  currentSource: "1",
  tempWarnC: 65,
  tempTripC: 90,
  modelParamTarget: "floor",
  modelTau: 42,
  modelK: 0.85,
  modelC: 1.25,
  ntcBeta: 3950,
  ntcT0C: 25,
  ntcR0: 10000,
  ntcFixedRes: 10000,
  presenceMinDropV: 0.2,
  floorThicknessMm: 30,
  nichromeFinalTempC: 38,
  ntcGateIndex: 2,
  floorMaterial: "wood",
  floorMaxC: 35,
  floorSwitchMarginC: 1.5,
  wireOhmPerM: 5.2,
  r01ohm: 8.2,
  r02ohm: 8.1,
  r03ohm: 8.0,
  r04ohm: 7.9,
  r05ohm: 7.8,
  r06ohm: 7.7,
  r07ohm: 7.6,
  r08ohm: 7.5,
  r09ohm: 7.4,
  r10ohm: 7.3,
  wireGauge: 16,
};

function applyDefaults(root) {
  Object.entries(DEFAULTS).forEach(([id, value]) => {
    const el = qs(`#${id}`, root);
    if (!el) return;
    if (el.tagName === "SELECT") {
      el.value = String(value);
    } else {
      el.value = value;
    }
  });
}

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

  applyDefaults(root);
  showToast(
    t(
      "device.toast.connected",
      null,
      "Device connected (mock). Values loaded.",
    ),
  );

  const fields = qsa("input, select, textarea", root).filter((el) => el.id);
  fields.forEach((field) => {
    field.addEventListener("change", () => {
      const label = labelFor(root, field.id);
      showToast(t("device.toast.updated", { label }, `${label} updated.`));
    });
  });

  const saveBtn = qs('[data-action="save"]', root);
  const resetBtn = qs('[data-action="reset"]', root);

  saveBtn?.addEventListener("click", () => {
    showToast(t("device.toast.saved", null, "Device settings saved (mock)."));
  });

  resetBtn?.addEventListener("click", () => {
    applyDefaults(root);
    showToast(t("device.toast.reset", null, "Device settings reset (mock)."));
  });

  return { applyDefaults };
}
