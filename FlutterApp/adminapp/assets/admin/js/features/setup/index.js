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
  const toast = qs("[data-setup-toast]", root);
  const toastText = qs("[data-setup-toast-text]", root);
  const inputNodes = qsa("input, select, textarea", root);

  let activeIndex = 0;
  let toastTimer = null;
  let demoShown = false;

  const setOpen = (open) => {
    root.classList.toggle("is-open", open);
    root.setAttribute("aria-hidden", String(!open));
  };

  const t = (key, vars, fallback) => {
    if (window.__i18n?.t) {
      const value = window.__i18n.t(key, vars);
      if (value && value !== key) return value;
    }
    return fallback ?? key;
  };

  const normalizeState = (state) => {
    const key = String(state || "").trim().toLowerCase();
    if (!key) return "ok";
    if (key === "warning") return "warn";
    if (key === "error") return "err";
    if (key === "success") return "ok";
    if (key === "info") return "ok";
    if (key === "ok" || key === "warn" || key === "err") return key;
    return "ok";
  };

  const showToast = (message, state = "ok") => {
    if (!toast || !toastText) return;
    toastText.textContent = String(message || "");
    toast.dataset.state = normalizeState(state);
    toast.classList.add("is-visible");
    toast.setAttribute("aria-hidden", "false");
    if (toastTimer) {
      clearTimeout(toastTimer);
    }
    toastTimer = setTimeout(() => {
      toast.classList.remove("is-visible");
      toast.setAttribute("aria-hidden", "true");
    }, 2400);
  };

  const updateControls = () => {
    if (!steps.length) return;
    const step = steps[activeIndex];
    const titleKey = step?.dataset.stepTitleKey;
    const fallbackTitle = step?.dataset.stepTitle || `Step ${activeIndex + 1}`;
    const title = titleKey ? t(titleKey, null, fallbackTitle) : fallbackTitle;
    const skippable = step?.dataset.stepSkippable === "true";

    if (stepLabel) {
      stepLabel.textContent = t(
        "wizard.header.stepLabel",
        { current: activeIndex + 1, total: steps.length },
        `Step ${activeIndex + 1} of ${steps.length}`,
      );
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
      nextBtn.textContent =
        activeIndex === steps.length - 1
          ? t("wizard.nav.finish", null, "Finish")
          : t("wizard.nav.next", null, "Next");
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
    if (!demoShown) {
      demoShown = true;
      showToast(
        t("wizard.toast.ready", null, "Setup wizard ready"),
        "ok",
      );
    }
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

  document.addEventListener("language:change", () => {
    if (!root.classList.contains("is-open")) return;
    updateControls();
  });

  inputNodes.forEach((node) => {
    node.addEventListener("change", () => {
      if (!root.classList.contains("is-open")) return;
      showToast(
        t("wizard.toast.updated", null, "Parameter updated"),
        "ok",
      );
    });
  });

  updateControls();

  return {
    open,
    close,
    setStep,
    notify: showToast,
    isOpen: () => root.classList.contains("is-open"),
  };
}
