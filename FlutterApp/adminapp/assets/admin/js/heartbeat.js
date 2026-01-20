  // ========================================================
  // ===============       HEARTBEAT / LOGIN     ============
  // ========================================================

  function startHeartbeat(intervalMs = 1500) {
    if (heartbeatTimer) clearInterval(heartbeatTimer);
    const tick = async () => {
      try {
        const res = await fetch("/heartbeat", { cache: "no-store" });
        if (res.status === 401) {
          noteAuthFailure();
          return;
        }
        if (res.ok) resetAuthFailures();
      } catch (err) {
        console.warn("Heartbeat failed:", err);
      }
    };
    tick();
    heartbeatTimer = setInterval(tick, intervalMs);
  }

  function disconnectDevice() {
    fetch("/disconnect", {
      method: "POST",
      headers: cborHeaders(),
      body: encodeCbor({ action: "disconnect" }),
      redirect: "follow",
    })
      .then((response) => {
        if (response.status === 401) {
          redirectToLogin();
          return null;
        }
        if (response.redirected) {
          redirectToLogin();
          return null;
        }
        if (response.ok) {
          redirectToLogin();
          return null;
        } else {
          return readCbor(response, {}).then((data) => {
            openAlert(
              "Disconnect",
              (data && data.error) || "Unexpected response"
            );
          });
        }
      })
      .catch((err) => {
        console.error("Disconnect failed:", err);
        redirectToLogin();
      });
  }

