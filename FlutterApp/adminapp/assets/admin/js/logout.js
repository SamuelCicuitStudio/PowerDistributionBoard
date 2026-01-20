  // ========================================================
  // ===============      AUTO LOGOUT HOOKS     =============
  // ========================================================

  function sendInstantLogout() {
    try {
      const payload = new Blob([encodeCbor({ action: "disconnect" })], {
        type: "application/cbor",
      });
      navigator.sendBeacon("/disconnect", payload);
    } catch (e) {
      fetch("/disconnect", {
        method: "POST",
        headers: cborHeaders(),
        body: encodeCbor({ action: "disconnect" }),
      });
    }
    try {
      redirectToLogin();
    } catch (e) {
      // ignore navigation errors
    }
  }

  window.addEventListener("pagehide", sendInstantLogout);
  document.addEventListener("visibilitychange", () => {
    if (document.visibilityState === "hidden") {
      sendInstantLogout();
    }
  });

