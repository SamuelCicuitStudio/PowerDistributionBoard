import { qs, qsa } from "../../core/dom.js";

function clamp(value, min, max) {
  return Math.min(Math.max(value, min), max);
}

export function initSetupWizard(options = {}) {
  const root = qs("[data-setup-wizard]");
  if (!root) return null;

  const { onStepChange, onOpen, onClose } = options;

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
  const logoutBtn = qs("[data-setup-logout]", root);
  const toast = qs("[data-setup-toast]", root);
  const toastText = qs("[data-setup-toast-text]", root);
  const inputNodes = qsa("input, select, textarea", root);
  const customSelects = [];
  let customSelectsReady = false;

  let activeIndex = 0;
  let toastTimer = null;
  let demoShown = false;
  let locked = false;

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
    if (typeof onStepChange === "function") {
      onStepChange(activeIndex, steps.length);
    }
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
    if (typeof onOpen === "function") onOpen();
  };

  const close = () => {
    if (locked) return;
    setOpen(false);
    if (typeof onClose === "function") onClose();
  };

  const setLocked = (value) => {
    locked = Boolean(value);
    root.dataset.locked = locked ? "true" : "false";
    if (closeBtn) closeBtn.disabled = locked;
    if (cancelBtn) cancelBtn.disabled = locked;
  };

  const handleLogout = async () => {
    if (window.pbDisconnect) {
      try {
        await window.pbDisconnect();
      } catch (error) {
        console.warn("Disconnect failed:", error);
      }
    }
    if (window.pbClearToken) window.pbClearToken();
    const base = new URLSearchParams(window.location.search).get("base");
    const next = "login.html" + (base ? "?base=" + encodeURIComponent(base) : "");
    window.location.href = next;
  };

  const initCustomSelects = () => {
    const selects = qsa("select", root).filter((node) =>
      node.classList.contains("setup-select"),
    );
    selects.forEach((select) => {
      if (select.dataset.customSelect === "true") return;

      let wrap = select.closest(".setup-select-wrap");
      if (!wrap) {
        wrap = document.createElement("div");
        wrap.className = "setup-select-wrap";
        select.parentNode?.insertBefore(wrap, select);
        wrap.appendChild(select);
      }

      let toggle = qs(".setup-select-btn", wrap);
      let menu = qs(".setup-select-menu", wrap);
      let current = qs("[data-setup-select-current]", wrap);

      if (!toggle) {
        toggle = document.createElement("button");
        toggle.type = "button";
        toggle.className = "setup-select-btn";
        toggle.setAttribute("aria-haspopup", "listbox");
        toggle.setAttribute("aria-expanded", "false");

        current = document.createElement("span");
        current.setAttribute("data-setup-select-current", "true");

        const caret = document.createElement("span");
        caret.className = "setup-select-caret";
        caret.setAttribute("aria-hidden", "true");
        caret.innerHTML = `
          <svg
            viewBox="0 0 24 24"
            aria-hidden="true"
            fill="none"
            stroke="currentColor"
            stroke-width="2"
            stroke-linecap="round"
            stroke-linejoin="round"
          >
            <polyline points="6 9 12 15 18 9"></polyline>
          </svg>
        `;

        toggle.append(current, caret);
        wrap.insertBefore(toggle, select);
      } else if (!current) {
        current = document.createElement("span");
        current.setAttribute("data-setup-select-current", "true");
        toggle.prepend(current);
      }

      if (!menu) {
        menu = document.createElement("div");
        menu.className = "setup-select-menu";
        menu.setAttribute("role", "listbox");
        menu.setAttribute("aria-hidden", "true");
        wrap.insertBefore(menu, select);
      }

      const setOpen = (open) => {
        menu.classList.toggle("is-open", open);
        menu.setAttribute("aria-hidden", String(!open));
        toggle.setAttribute("aria-expanded", String(open));
      };

      const updateCurrent = () => {
        const selectedIndex =
          select.selectedIndex >= 0 ? select.selectedIndex : 0;
        const selectedOption = select.options[selectedIndex];
        if (current && selectedOption) {
          current.textContent = selectedOption.textContent.trim();
        }
        const optionButtons = qsa("[data-setup-select-index]", menu);
        optionButtons.forEach((btn) => {
          const isActive = Number(btn.dataset.setupSelectIndex) === selectedIndex;
          btn.classList.toggle("is-active", isActive);
          btn.setAttribute("aria-selected", String(isActive));
        });
        if (toggle) {
          toggle.disabled = select.disabled;
          toggle.setAttribute(
            "aria-disabled",
            select.disabled ? "true" : "false",
          );
        }
      };

      const syncOptions = () => {
        menu.innerHTML = "";
        const options = Array.from(select.options);
        options.forEach((option, index) => {
          const btn = document.createElement("button");
          btn.type = "button";
          btn.dataset.setupSelectIndex = String(index);
          btn.setAttribute("role", "option");
          btn.textContent = option.textContent.trim();
          if (option.disabled) {
            btn.disabled = true;
            btn.classList.add("is-disabled");
          }
          menu.appendChild(btn);
        });
        updateCurrent();
      };

      toggle.addEventListener("click", (event) => {
        event.preventDefault();
        event.stopPropagation();
        const isOpen = menu.classList.contains("is-open");
        customSelects.forEach((item) => {
          if (item.menu !== menu) item.setOpen(false);
        });
        setOpen(!isOpen);
      });

      menu.addEventListener("click", (event) => {
        const btn = event.target.closest("[data-setup-select-index]");
        if (!btn || btn.disabled) return;
        event.preventDefault();
        const index = Number(btn.dataset.setupSelectIndex);
        if (!Number.isNaN(index) && select.selectedIndex !== index) {
          select.selectedIndex = index;
          select.dispatchEvent(new Event("change", { bubbles: true }));
        }
        updateCurrent();
        setOpen(false);
      });

      select.addEventListener("change", updateCurrent);

      select.classList.add("setup-select-native");
      select.dataset.customSelect = "true";

      syncOptions();
      customSelects.push({ wrap, menu, setOpen, syncOptions, updateCurrent });
    });

    if (customSelectsReady) return;
    customSelectsReady = true;

    document.addEventListener("click", (event) => {
      customSelects.forEach((item) => {
        if (!item.wrap.contains(event.target)) {
          item.setOpen(false);
        }
      });
    });

    document.addEventListener("keydown", (event) => {
      if (event.key === "Escape") {
        customSelects.forEach((item) => item.setOpen(false));
      }
    });

    document.addEventListener("language:change", () => {
      customSelects.forEach((item) => item.syncOptions());
    });

    document.addEventListener("setup:controls-loaded", () => {
      customSelects.forEach((item) => item.updateCurrent());
    });
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
  logoutBtn?.addEventListener("click", handleLogout);

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

  initCustomSelects();
  updateControls();

  return {
    open,
    close,
    setStep,
    notify: showToast,
    isOpen: () => root.classList.contains("is-open"),
    setLocked,
    isLocked: () => locked,
  };
}
