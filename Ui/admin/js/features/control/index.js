import { qs, qsa } from "../../core/dom.js";

export function initControlTab() {
  const panel = qs('[data-tab-panel="control"]');
  if (!panel) return null;

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
      row.innerHTML = `
        <b>Output ${output.id}</b>
        <div class="control-out-right">
          <label class="control-switch" aria-label="Toggle Output ${output.id}">
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
        showToast(`Output ${id} ${event.target.checked ? "ON" : "OFF"}`);
      });
    });
  };

  if (fanSlider && fanValue) {
    const sync = () => {
      fanValue.textContent = `${fanSlider.value}%`;
    };
    fanSlider.addEventListener("input", sync);
    fanSlider.addEventListener("change", () => {
      showToast(`Fan speed set to ${fanSlider.value}%`);
    });
    sync();
  }

  if (relayToggle) {
    relayToggle.addEventListener("change", () => {
      showToast(`Relay ${relayToggle.checked ? "ON" : "OFF"}`);
    });
  }

  renderOutputs();

  return { renderOutputs };
}
