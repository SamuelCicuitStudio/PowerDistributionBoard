  // ========================================================
  // ===============         LT TOGGLES         ============
  // ========================================================

  function toggleLT() {
    const ltToggle = document.getElementById("ltToggle");
    const isOn = !!(ltToggle && ltToggle.checked);
    updateModePills();
    sendControlCommand("set", "ledFeedback", isOn);
  }

