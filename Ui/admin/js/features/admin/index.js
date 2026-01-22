import { qs } from "../../core/dom.js";

function nonEmpty(value) {
  return String(value || "").trim();
}

export function initAdminTab() {
  const panel = qs('[data-tab-panel="admin"]');
  if (!panel) return null;

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
        showToast("Admin: current password required", "error");
        return;
      }
      const username = nonEmpty(newUser?.value) || "keep current";
      const password = nonEmpty(newPw?.value) || "keep current";
      showToast(`Admin saved (${username}, ${password})`, "success");
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
      const ssidValue = nonEmpty(ssid?.value) || "keep current";
      const pwValue = nonEmpty(pw?.value) || "keep current";
      showToast(`Wi-Fi station saved (${ssidValue})`, "success");
      if (pw) pw.value = "";
    });
  }

  if (apForm) {
    const ssid = qs("#admin-ap-ssid", apForm);
    const pw = qs("#admin-ap-pw", apForm);
    apForm.addEventListener("click", (event) => {
      const saveBtn = event.target.closest('[data-action="save"]');
      if (!saveBtn) return;
      const ssidValue = nonEmpty(ssid?.value) || "keep current";
      const pwValue = nonEmpty(pw?.value) || "keep current";
      showToast(`Wi-Fi AP saved (${ssidValue})`, "success");
      if (pw) pw.value = "";
    });
  }

  return {};
}
