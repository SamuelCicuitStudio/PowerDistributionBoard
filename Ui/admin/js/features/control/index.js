import { qs, qsa } from "../../core/dom.js";

export function initControlTab() {
  const panel = qs('[data-tab-panel="control"]');
  if (!panel) return null;

  const t = (key, vars, fallback) => {
    if (window.__i18n?.t) {
      const value = window.__i18n.t(key, vars);
      if (value && value !== key) return value;
    }
    return fallback ?? key;
  };

  const outputList = qs("[data-control-output-list]", panel);
  const fanSlider = qs("[data-control-fan]", panel);
  const fanValue = qs("[data-control-fan-value]", panel);
  const relayToggle = qs("[data-control-relay]", panel);

  const outputs = Array.from({ length: 10 }, (_, index) => ({
    id: index + 1,
    on: false,
  }));

  const showToast = (message) => {
    window.__toast?.show?.(message, "success");
  };

  const renderOutputs = () => {
    if (!outputList) return;
    outputList.innerHTML = "";
    outputs.forEach((output) => {
      const row = document.createElement("div");
      row.className = "control-out-item";
      row.setAttribute("role", "listitem");
      const labelText = t(
        "control.outputs.output",
        { index: output.id },
        `Output ${output.id}`,
      );
      const toggleText = t(
        "control.outputs.toggle",
        { index: output.id },
        `Toggle Output ${output.id}`,
      );
      row.innerHTML = `
        <b>${labelText}</b>
        <div class="control-out-right">
          <label class="control-switch" aria-label="${toggleText}">
            <input type="checkbox" ${
              output.on ? "checked" : ""
            } data-id="${output.id}">
            <span class="control-slider"></span>
          </label>
          <span class="control-dot ${
            output.on ? "is-on" : ""
          }" aria-hidden="true"></span>
        </div>
      `;
      outputList.appendChild(row);
    });

    qsa('input[type="checkbox"]', outputList).forEach((checkbox) => {
      checkbox.addEventListener("change", (event) => {
        const id = Number(event.target.dataset.id);
        const item = outputs.find((entry) => entry.id === id);
        if (item) item.on = event.target.checked;
        const dot = event.target.closest(".control-out-right")?.querySelector(
          ".control-dot",
        );
        if (dot) dot.classList.toggle("is-on", event.target.checked);
        const stateLabel = event.target.checked
          ? t("control.state.on", null, "ON")
          : t("control.state.off", null, "OFF");
        showToast(
          t(
            "control.toast.output",
            { index: id, state: stateLabel },
            `Output ${id} ${stateLabel}`,
          ),
        );
      });
    });
  };

  if (fanSlider && fanValue) {
    const sync = () => {
      fanValue.textContent = `${fanSlider.value}%`;
    };
    fanSlider.addEventListener("input", sync);
    fanSlider.addEventListener("change", () => {
      showToast(
        t(
          "control.toast.fan",
          { value: fanSlider.value },
          `Fan speed set to ${fanSlider.value}%`,
        ),
      );
    });
    sync();
  }

  if (relayToggle) {
    relayToggle.addEventListener("change", () => {
      const stateLabel = relayToggle.checked
        ? t("control.state.on", null, "ON")
        : t("control.state.off", null, "OFF");
      showToast(
        t(
          "control.toast.relay",
          { state: stateLabel },
          `Relay ${stateLabel}`,
        ),
      );
    });
  }

  renderOutputs();

  document.addEventListener("language:change", () => {
    renderOutputs();
  });

  return { renderOutputs };
}
