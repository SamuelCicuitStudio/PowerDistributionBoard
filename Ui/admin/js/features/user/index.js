import { qs, qsa } from "../../core/dom.js";

const DEFAULT_DEVICE_ID = "PDB-T37H6-911N0";

export function initUserTab() {
  const panel = qs('[data-tab-panel="user"]');
  if (!panel) return null;

  const t = (key, vars, fallback) => {
    if (window.__i18n?.t) {
      const value = window.__i18n.t(key, vars);
      if (value && value !== key) return value;
    }
    return fallback ?? key;
  };

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
      const labelText = t(
        "user.outputs.allow",
        { index: output.id },
        `Allow Output ${output.id}`,
      );
      const ariaText = t(
        "user.outputs.allowAria",
        { index: output.id },
        `Allow Output ${output.id}`,
      );
      row.innerHTML = `
        <span>${labelText}</span>
        <label class="user-switch" aria-label="${ariaText}">
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
        const stateLabel = event.target.checked
          ? t("user.outputs.state.enabled", null, "enabled")
          : t("user.outputs.state.disabled", null, "disabled");
        showToast(
          t(
            "user.outputs.toast",
            { index: id, state: stateLabel },
            `Output ${id} ${stateLabel}`,
          ),
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
        const list = allowed.length ? allowed.join(", ") : t("user.outputs.none", null, "none");
        showToast(
          t(
            "user.save.success",
            { list },
            `Saved. Outputs allowed: ${list}`,
          ),
          "success",
        );
      } else {
        showToast(
          t("user.save.fail", null, "Failed to save settings"),
          "error",
        );
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
      confirm(t("user.reset.confirm", null, "Reset this form?"), () => {
        if (curPw) curPw.value = "";
        if (newPw) newPw.value = "";
        if (deviceId) deviceId.value = DEFAULT_DEVICE_ID;
        outputs.forEach((o, index) => {
          o.allowed = index === 0;
        });
        renderOutputs();
        showToast(t("user.reset.done", null, "Form reset"), "success");
      });
    });
  }

  renderOutputs();

  document.addEventListener("language:change", () => {
    renderOutputs();
  });

  return { renderOutputs };
}
