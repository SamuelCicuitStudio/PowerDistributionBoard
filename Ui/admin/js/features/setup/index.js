import { qs, qsa } from "../../core/dom.js";

function clamp(value, min, max) {
  return Math.min(Math.max(value, min), max);
}

export function initSetupWizard() {
  const root = qs("[data-setup-wizard]");
  if (!root) return null;

  const steps = qsa("[data-setup-step]", root);
  const stepButtons = qsa("[data-setup-stepper]", root);
  const stepLabel = qs("[data-setup-step-label]", root);
  const stepTitle = qs("[data-setup-step-title]", root);
  const progress = qs("[data-setup-progress]", root);
  const backBtn = qs("[data-setup-back]", root);
  const nextBtn = qs("[data-setup-next]", root);
  const skipBtn = qs("[data-setup-skip]", root);
  const cancelBtn = qs("[data-setup-cancel]", root);
  const closeBtn = qs("[data-setup-close]", root);

  let activeIndex = 0;

  const setOpen = (open) => {
    root.classList.toggle("is-open", open);
    root.setAttribute("aria-hidden", String(!open));
  };

  const updateControls = () => {
    if (!steps.length) return;
    const step = steps[activeIndex];
    const title = step?.dataset.stepTitle || `Step ${activeIndex + 1}`;
    const skippable = step?.dataset.stepSkippable === "true";

    if (stepLabel) {
      stepLabel.textContent = `Step ${activeIndex + 1} of ${steps.length}`;
    }
    if (stepTitle) {
      stepTitle.textContent = title;
    }
    if (progress) {
      const pct = ((activeIndex + 1) / steps.length) * 100;
      progress.style.width = `${pct}%`;
    }
    if (backBtn) {
      backBtn.disabled = activeIndex === 0;
    }
    if (skipBtn) {
      skipBtn.style.display = skippable ? "inline-flex" : "none";
    }
    if (nextBtn) {
      nextBtn.textContent = activeIndex === steps.length - 1 ? "Finish" : "Next";
    }
  };

  const setStep = (index) => {
    if (!steps.length) return;
    const nextIndex = clamp(index, 0, steps.length - 1);
    steps.forEach((step, i) => {
      step.classList.toggle("is-active", i === nextIndex);
    });
    stepButtons.forEach((btn, i) => {
      btn.classList.toggle("is-active", i === nextIndex);
      btn.classList.toggle("is-complete", i < nextIndex);
    });
    activeIndex = nextIndex;
    updateControls();
  };

  const open = (index = 0) => {
    setOpen(true);
    setStep(index);
  };

  const close = () => {
    setOpen(false);
  };

  backBtn?.addEventListener("click", () => setStep(activeIndex - 1));
  nextBtn?.addEventListener("click", () => {
    if (activeIndex === steps.length - 1) {
      close();
      return;
    }
    setStep(activeIndex + 1);
  });
  skipBtn?.addEventListener("click", () => setStep(activeIndex + 1));
  cancelBtn?.addEventListener("click", close);
  closeBtn?.addEventListener("click", close);

  stepButtons.forEach((btn, i) => {
    btn.addEventListener("click", () => setStep(i));
  });

  document.addEventListener("keydown", (event) => {
    if (!root.classList.contains("is-open")) return;
    if (event.key === "Escape") close();
  });

  updateControls();

  return { open, close, setStep };
}
