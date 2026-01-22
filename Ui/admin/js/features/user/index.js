import { qs, qsa } from "../../core/dom.js";

const DEFAULT_DEVICE_ID = "PDB-T37H6-911N0";

export function initUserTab() {
  const panel = qs('[data-tab-panel="user"]');
  if (!panel) return null;

  const outputList = qs("[data-output-list]", panel);
  const deviceId = qs("[data-user-device-id]", panel);
  const saveBtn = qs('[data-action="save"]', panel);
  const resetBtn = qs('[data-action="reset"]', panel);
  const curPw = qs("#user-cur-pw", panel);
  const newPw = qs("#user-new-pw", panel);

  const outputs = Array.from({ length: 10 }, (_, index) => ({
    id: index + 1,
    allowed: index === 0,
  }));

  const renderOutputs = () => {
    if (!outputList) return;
    outputList.innerHTML = "";
    outputs.forEach((output) => {
      const row = document.createElement("div");
      row.className = "user-access-item";
      row.setAttribute("role", "listitem");
      row.innerHTML = `
        <span>Allow Output ${output.id}</span>
        <label class="user-switch" aria-label="Allow Output ${output.id}">
          <input type="checkbox" ${
            output.allowed ? "checked" : ""
          } data-id="${output.id}">
          <span class="user-slider"></span>
        </label>
      `;
      outputList.appendChild(row);
    });

    qsa('input[type="checkbox"]', outputList).forEach((checkbox) => {
      checkbox.addEventListener("change", (event) => {
        const id = Number(event.target.dataset.id);
        const item = outputs.find((entry) => entry.id === id);
        if (item) item.allowed = event.target.checked;
        showToast(
          `Output ${id} ${event.target.checked ? "enabled" : "disabled"}`,
          "success",
        );
      });
    });
  };

  const showToast = (message, state = "success") => {
    window.__toast?.show?.(message, state);
  };

  if (deviceId) {
    deviceId.value = DEFAULT_DEVICE_ID;
    deviceId.readOnly = true;
  }

  if (saveBtn) {
    saveBtn.addEventListener("click", () => {
      const allowed = outputs.filter((o) => o.allowed).map((o) => o.id);
      const ok = Math.random() > 0.2;
      if (ok) {
        showToast(
          `Saved. Outputs allowed: ${allowed.length ? allowed.join(", ") : "none"}`,
          "success",
        );
      } else {
        showToast("Failed to save settings", "error");
      }
      if (curPw) curPw.value = "";
      if (newPw) newPw.value = "";
    });
  }

  if (resetBtn) {
    resetBtn.addEventListener("click", () => {
      const confirm = window.__confirm?.open;
      if (!confirm) {
        return;
      }
      confirm("Reset this form?", () => {
        if (curPw) curPw.value = "";
        if (newPw) newPw.value = "";
        if (deviceId) deviceId.value = DEFAULT_DEVICE_ID;
        outputs.forEach((o, index) => {
          o.allowed = index === 0;
        });
        renderOutputs();
        showToast("Form reset", "success");
      });
    });
  }

  renderOutputs();

  return { renderOutputs };
}
