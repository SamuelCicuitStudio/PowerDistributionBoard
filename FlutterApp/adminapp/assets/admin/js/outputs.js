  // ========================================================
  // ===============  OUTPUT RENDERING HELPERS  =============
  // ========================================================

  function renderAllOutputs(containerId, isControlMode) {
    const container = document.getElementById(containerId);
    if (!container) return;

    container.innerHTML = "";

    for (let i = 1; i <= 10; i++) {
      const block = document.createElement("div");
      block.className = "manual-item";
      if (!isControlMode) block.classList.add("access-style");

      if (isControlMode) {
        block.title = "Manual control: toggle Output " + i;
        block.innerHTML = `
          <span>Output ${i}</span>
          <label class="switch">
            <input type="checkbox" title="Toggle Output ${i} (manual mode)" aria-label="Toggle Output ${i}" onchange="handleOutputToggle(${i}, this)">
            <span class="slider"></span>
          </label>
          <div class="led" id="manualLed${i}" title="Output ${i} state indicator"></div>
        `;
      } else {
        block.title = "Authorize Output " + i + " for loop/calibration";
        block.innerHTML = `
          <span>Allow Output ${i}</span>
          <label class="switch">
            <input type="checkbox" id="accessToggle${i}" title="Authorize Output ${i} for loop/calibration" aria-label="Authorize Output ${i}" onchange="updateOutputAccess(${i}, this.checked)">
            <span class="slider"></span>
          </label>
        `;
      }

      container.appendChild(block);
    }

    // Ensure tooltips exist for newly created toggles.
    applyTooltips(document);
  }

  function enableDragScroll(containerId) {
    const container = document.getElementById(containerId);
    if (!container) return;

    let isDown = false;
    let startX = 0;
    let scrollLeft = 0;

    container.style.scrollBehavior = "smooth";

    container.addEventListener("mousedown", (e) => {
      isDown = true;
      container.classList.add("dragging");
      startX = e.pageX - container.offsetLeft;
      scrollLeft = container.scrollLeft;
    });

    ["mouseleave", "mouseup"].forEach((evt) => {
      container.addEventListener(evt, () => {
        isDown = false;
        container.classList.remove("dragging");
      });
    });

    container.addEventListener("mousemove", (e) => {
      if (!isDown) return;
      e.preventDefault();
      const x = e.pageX - container.offsetLeft;
      const walk = (x - startX) * 2;
      container.scrollLeft = scrollLeft - walk;
    });

    container.addEventListener(
      "wheel",
      (e) => {
        if (e.deltaY !== 0) {
          e.preventDefault();
          container.scrollLeft += e.deltaY * 0.3;
        }
      },
      { passive: false }
    );
  }

