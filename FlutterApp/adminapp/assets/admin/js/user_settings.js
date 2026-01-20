  // ========================================================
  // ===============    USER / ADMIN SETTINGS    ============
  // ========================================================

  function saveUserSettings() {
    if (
      guardUnsafeAction("saving user settings", {
        blockAuto: true,
        blockCalib: true,
      })
    ) {
      return;
    }
    const current = document.getElementById("userCurrentPassword").value;
    const newPass = document.getElementById("userNewPassword").value;
    const newId = document.getElementById("userDeviceId").value;

    const hasChanges =
      String(newPass || "").length > 0 || String(newId || "").trim().length > 0;
    if (!hasChanges) {
      openAlert("User Settings", "Nothing to update.", "warning");
      return;
    }

    const doSave = () => {
      sendControlCommand("set", "userCredentials", {
        current,
        newPass,
        newId,
      }).then((res) => {
        if (res && !res.error) {
          disconnectDevice();
        } else if (res && res.error) {
          openAlert("User Settings", res.error, "danger");
        }
      });
    };

    openConfirm("sessionChange", doSave);
  }

  function resetUserSettings() {
    setField("userCurrentPassword", "");
    setField("userNewPassword", "");
    if (lastLoadedControls && lastLoadedControls.deviceId !== undefined) {
      setField("userDeviceId", lastLoadedControls.deviceId);
    } else {
      setField("userDeviceId", "");
    }
  }

