  function resetApSettings() {
    setField("apSSID", "");
    setField("apPassword", "");
  }

  function saveApSettings() {
    if (
      guardUnsafeAction("saving AP settings", {
        blockAuto: true,
        blockCalib: true,
      })
    ) {
      return;
    }

    const ssid = String(
      (document.getElementById("apSSID") || {}).value || ""
    ).trim();
    const pw = String(
      (document.getElementById("apPassword") || {}).value || ""
    );

    const payload = {};
    if (ssid) payload.apSSID = ssid;
    if (pw) payload.apPassword = pw;

    if (!payload.apSSID && !payload.apPassword) {
      openAlert("AP Settings", "Nothing to update.", "warning");
      return;
    }

    const doSave = async () => {
      try {
        const res = await fetch("/ap_config", {
          method: "POST",
          headers: cborHeaders(),
          body: encodeCbor(payload),
        });
        const data = await readCbor(res, {});
        if (!res.ok || (data && data.error)) {
          openAlert("AP Settings", data.error || "Update failed.", "danger");
          return;
        }
        resetApSettings();
        fetchSetupStatus();
        openAlert(
          "AP Settings",
          "AP settings saved. Restarting device...",
          "success"
        );
      } catch (err) {
        console.error("AP settings update failed:", err);
        openAlert("AP Settings", "Update failed.", "danger");
      }
    };

    openConfirm("wifiRestart", doSave);
  }

