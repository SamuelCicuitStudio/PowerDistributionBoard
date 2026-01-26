import { qs } from "../../core/dom.js";

function nonEmpty(value) {
  return String(value || "").trim();
}

export function initAdminTab() {
  const panel = qs('[data-tab-panel="admin"]');
  if (!panel) return null;

  const t = (key, vars, fallback) => {
    if (window.__i18n?.t) {
      const value = window.__i18n.t(key, vars);
      if (value && value !== key) return value;
    }
    return fallback ?? key;
  };

  const showToast = (message, state = "success") => {
    window.__toast?.show?.(message, state);
  };

  const adminForm = qs('[data-admin-form="credentials"]', panel);
  const stationForm = qs('[data-admin-form="station"]', panel);
  const apForm = qs('[data-admin-form="ap"]', panel);

  if (adminForm) {
    const currentPw = qs("#admin-cur-pw", adminForm);
    const newUser = qs("#admin-user", adminForm);
    const newPw = qs("#admin-new-pw", adminForm);
    adminForm.addEventListener("click", (event) => {
      const saveBtn = event.target.closest('[data-action="save"]');
      if (!saveBtn) return;
      const cur = nonEmpty(currentPw?.value);
      if (!cur) {
        showToast(
          t("admin.toast.passwordRequired", null, "Admin: current password required"),
          "error",
        );
        return;
      }
      const keepCurrent = t("admin.toast.keepCurrent", null, "keep current");
      const username = nonEmpty(newUser?.value) || keepCurrent;
      const password = nonEmpty(newPw?.value) || keepCurrent;
      showToast(
        t(
          "admin.toast.saved",
          { username, password },
          `Admin saved (${username}, ${password})`,
        ),
        "success",
      );
      if (currentPw) currentPw.value = "";
      if (newPw) newPw.value = "";
    });
  }

  if (stationForm) {
    const ssid = qs("#admin-sta-ssid", stationForm);
    const pw = qs("#admin-sta-pw", stationForm);
    stationForm.addEventListener("click", (event) => {
      const saveBtn = event.target.closest('[data-action="save"]');
      if (!saveBtn) return;
      const keepCurrent = t("admin.toast.keepCurrent", null, "keep current");
      const ssidValue = nonEmpty(ssid?.value) || keepCurrent;
      const pwValue = nonEmpty(pw?.value) || keepCurrent;
      showToast(
        t(
          "admin.toast.stationSaved",
          { ssid: ssidValue },
          `Wi-Fi station saved (${ssidValue})`,
        ),
        "success",
      );
      if (pw) pw.value = "";
    });
  }

  if (apForm) {
    const ssid = qs("#admin-ap-ssid", apForm);
    const pw = qs("#admin-ap-pw", apForm);
    apForm.addEventListener("click", (event) => {
      const saveBtn = event.target.closest('[data-action="save"]');
      if (!saveBtn) return;
      const keepCurrent = t("admin.toast.keepCurrent", null, "keep current");
      const ssidValue = nonEmpty(ssid?.value) || keepCurrent;
      const pwValue = nonEmpty(pw?.value) || keepCurrent;
      showToast(
        t(
          "admin.toast.apSaved",
          { ssid: ssidValue },
          `Wi-Fi AP saved (${ssidValue})`,
        ),
        "success",
      );
      if (pw) pw.value = "";
    });
  }

  return {};
}
