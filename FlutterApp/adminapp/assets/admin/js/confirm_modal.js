  // ========================================================
  // ===============        CONFIRM MODAL        ============
  // ========================================================

  function openConfirm(kind, action) {
    pendingConfirm = kind || null;
    pendingConfirmAction = typeof action === "function" ? action : null;

    const modal = document.getElementById("confirmModal");
    const title = document.getElementById("confirmTitle");
    const message = document.getElementById("confirmMessage");
    const okBtn = document.getElementById("confirmOkBtn");
    const cancelBtn = document.getElementById("confirmCancelBtn");

    if (!modal || !title || !message || !okBtn) return;

    // Always show cancel for confirmations
    if (cancelBtn) cancelBtn.style.display = "";

    okBtn.classList.remove("danger", "warning", "success");

    if (kind === "reset") {
      title.textContent = "Confirm Reset";
      message.textContent = "This will reset the device (soft reset). Proceed-";
      okBtn.textContent = "Yes, Reset";
      okBtn.classList.add("danger");
    } else if (kind === "reboot") {
      title.textContent = "Confirm Reboot";
      message.textContent = "The device will reboot. Continue-";
      okBtn.textContent = "Yes, Reboot";
      okBtn.classList.add("danger");
    } else if (kind === "wifiRestart") {
      title.textContent = "Restart Required";
      message.textContent =
        "Changing Wi-Fi settings will restart the device and disconnect this session. Continue-";
      okBtn.textContent = "Confirm & Restart";
      okBtn.classList.add("danger");
    } else if (kind === "sessionChange") {
      title.textContent = "Confirm Session Change";
      message.textContent =
        "Updating credentials will disconnect all users and return to login. Continue-";
      okBtn.textContent = "Confirm & Disconnect";
      okBtn.classList.add("danger");
    } else if (kind === "sessionWifiChange") {
      title.textContent = "Confirm Changes";
      message.textContent =
        "Updating credentials and Wi-Fi settings will disconnect all users and restart the device. Continue-";
      okBtn.textContent = "Confirm";
      okBtn.classList.add("danger");
    } else if (kind === "setupReset") {
      title.textContent = "Reset Setup";
      message.textContent =
        "This clears setup progress and calibration flags. Continue-";
      okBtn.textContent = "Reset Setup";
      okBtn.classList.add("danger");
    } else {
      title.textContent = "Confirm Action";
      message.textContent = "Are you sure-";
      okBtn.textContent = "Confirm";
      okBtn.classList.add("warning");
    }

    modal.style.display = "flex";
  }

  function closeConfirm() {
    const modal = document.getElementById("confirmModal");
    if (modal) modal.style.display = "none";
    pendingConfirm = null;
    pendingConfirmAction = null;
  }

  function bindConfirmModal() {
    const modal = document.getElementById("confirmModal");
    const okBtn = document.getElementById("confirmOkBtn");
    const cancelBtn = document.getElementById("confirmCancelBtn");

    if (!modal || !okBtn) return;

    if (cancelBtn) {
      cancelBtn.addEventListener("click", () => {
        pendingConfirm = null;
        pendingConfirmAction = null;
        closeConfirm();
      });
    }

    okBtn.addEventListener("click", () => {
      if (!pendingConfirm) {
        closeConfirm();
        return;
      }

      if (pendingConfirm === "reset") {
        resetSystem();
      } else if (pendingConfirm === "reboot") {
        rebootSystem();
      } else if (pendingConfirmAction) {
        pendingConfirmAction();
      }

      pendingConfirm = null;
      pendingConfirmAction = null;
      closeConfirm();
    });

    // Click outside to close
    modal.addEventListener("click", (e) => {
      if (e.target === modal) {
        pendingConfirm = null;
        pendingConfirmAction = null;
        closeConfirm();
      }
    });

    // ESC key closes
    document.addEventListener("keydown", (e) => {
      if (e.key === "Escape") {
        pendingConfirm = null;
        pendingConfirmAction = null;
        closeConfirm();
      }
    });
  }

  // Generic alert that reuses confirm modal with no pendingConfirm
  function openAlert(title, message, variant = "warning") {
    pendingConfirm = null;
    pendingConfirmAction = null;

    const modal = document.getElementById("confirmModal");
    const titleEl = document.getElementById("confirmTitle");
    const messageEl = document.getElementById("confirmMessage");
    const okBtn = document.getElementById("confirmOkBtn");
    const cancelBtn = document.getElementById("confirmCancelBtn");

    if (!modal || !titleEl || !messageEl || !okBtn) return;

    // Hide cancel for alerts
    if (cancelBtn) cancelBtn.style.display = "none";

    titleEl.textContent = title || "Notice";
    messageEl.textContent = message || "";
    okBtn.textContent = "OK";

    okBtn.classList.remove("danger", "warning", "success");
    if (variant === "danger") okBtn.classList.add("danger");
    else if (variant === "success") okBtn.classList.add("success");
    else okBtn.classList.add("warning");

    modal.style.display = "flex";
  }

