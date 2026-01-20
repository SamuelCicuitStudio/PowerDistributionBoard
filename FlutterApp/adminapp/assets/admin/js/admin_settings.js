  function saveAdminSettings(scope = "all") {
    if (
      guardUnsafeAction("saving admin settings", {
        blockAuto: true,
        blockCalib: true,
      })
    ) {
      return;
    }
    const section = scope === "admin" || scope === "wifi" ? scope : "all";

    const current = (document.getElementById("adminCurrentPassword") || {})
      .value;
    const username = (document.getElementById("adminUsername") || {}).value;
    const password = (document.getElementById("adminPassword") || {}).value;
    const wifiSSID = (document.getElementById("wifiSSID") || {}).value;
    const wifiPassword = (document.getElementById("wifiPassword") || {}).value;

    const payload = {};
    const trimmedCurrent = String(current || "").trim();
    if (trimmedCurrent) payload.current = trimmedCurrent;

    let hasChanges = false;
    if (section === "admin" || section === "all") {
      const u = String(username || "").trim();
      const p = String(password || "");
      if (u) {
        payload.username = u;
        hasChanges = true;
      }
      if (p) {
        payload.password = p;
        hasChanges = true;
      }
    }
    if (section === "wifi" || section === "all") {
      const ssid = String(wifiSSID || "").trim();
      const pw = String(wifiPassword || "");
      if (ssid) {
        payload.wifiSSID = ssid;
        hasChanges = true;
      }
      if (pw) {
        payload.wifiPassword = pw;
        hasChanges = true;
      }
    }

    if (!hasChanges) {
      openAlert("Admin Settings", "Nothing to update.", "warning");
      return;
    }

    if ((payload.username || payload.password) && !payload.current) {
      openAlert("Admin Settings", "Current password is required.", "warning");
      return;
    }

    const adminChanged = !!(payload.username || payload.password);
    const wifiChanged = !!(payload.wifiSSID || payload.wifiPassword);

    const doSave = () => {
      sendControlCommand("set", "adminCredentials", payload).then((res) => {
        if (res && !res.error) {
          resetAdminSettings(section);
          if (adminChanged) {
            disconnectDevice();
            return;
          }
          if (wifiChanged) {
            fetchSetupStatus();
            openAlert(
              "Wi-Fi Settings",
              "Wi-Fi settings saved. Restarting device...",
              "success"
            );
            return;
          }
          fetchSetupStatus();
          openAlert("Admin Settings", "Admin settings updated.", "success");
        } else if (res && res.error) {
          openAlert("Admin Settings", res.error, "danger");
        }
      });
    };

    if (adminChanged && wifiChanged) {
      openConfirm("sessionWifiChange", doSave);
      return;
    }
    if (adminChanged) {
      openConfirm("sessionChange", doSave);
      return;
    }
    if (wifiChanged) {
      openConfirm("wifiRestart", doSave);
      return;
    }

    doSave();
  }

  function resetAdminSettings(scope = "all") {
    const section = scope === "admin" || scope === "wifi" ? scope : "all";
    setField("adminCurrentPassword", "");

    if (section === "admin" || section === "all") {
      setField("adminUsername", "");
      setField("adminPassword", "");
    }

    if (section === "wifi" || section === "all") {
      setField("wifiSSID", "");
      setField("wifiPassword", "");
    }
  }

