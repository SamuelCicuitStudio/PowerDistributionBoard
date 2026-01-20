  // ========================================================
  // ===============   Session Stats Frontend   =============
  // ========================================================
  //
  // Pure UI helpers; backend will call updateSessionStatsUI()
  // later with:
  // {
  //   valid: bool,
  //   running: bool,            // optional, if current session
  //   energy_Wh: number,
  //   duration_s: number,
  //   peakPower_W: number,
  //   peakCurrent_A: number
  // }

  function updateSessionStatsUI(session) {
    const statusEl = document.getElementById("sessionStatus");
    const eEl = document.getElementById("sessionEnergy");
    const dEl = document.getElementById("sessionDuration");
    const pWEl = document.getElementById("sessionPeakPower");
    const pAEl = document.getElementById("sessionPeakCurrent");

    if (!statusEl || !eEl || !dEl || !pWEl || !pAEl) return;

    // No data / no active session
    if (!session || !session.valid) {
      statusEl.textContent = "No active session";
      statusEl.className = "session-status session-status-idle";

      eEl.textContent = "-- Wh";
      dEl.textContent = "-- s";
      pWEl.textContent = "-- W";
      pAEl.textContent = "-- A";
      return;
    }

    const running = !!session.running;

    if (running) {
      statusEl.textContent = "Session running";
      statusEl.className = "session-status session-status-running";
    } else {
      statusEl.textContent = "Last session";
      statusEl.className = "session-status session-status-finished";
    }

    eEl.textContent = (session.energy_Wh || 0).toFixed(2) + " Wh";
    dEl.textContent = (session.duration_s || 0).toString() + " s";
    pWEl.textContent = (session.peakPower_W || 0).toFixed(1) + " W";
    pAEl.textContent = (session.peakCurrent_A || 0).toFixed(2) + " A";
  }

  // History modal controls (frontend only)
  function openSessionHistory() {
    const m = document.getElementById("sessionHistoryModal");
    if (!m) return;
    m.classList.add("show");
  }

  function closeSessionHistory() {
    const m = document.getElementById("sessionHistoryModal");
    if (!m) return;
    m.classList.remove("show");
  }

  function bindSessionHistoryButton() {
    const btn = document.getElementById("sessionHistoryBtn");
    if (!btn) return;

    btn.addEventListener("click", loadSessionHistoryAndOpen);
  }

  // Error modal controls
  function openErrorModal() {
    const m = document.getElementById("errorModal");
    if (!m) return;
    m.classList.add("show");
  }

  function closeErrorModal() {
    const m = document.getElementById("errorModal");
    if (!m) return;
    m.classList.remove("show");
  }

  function updateEventBadge(warnCount, errCount) {
    const badge = document.getElementById("eventBadge");
    if (!badge) return;
    const w = Math.max(0, Number(warnCount) || 0);
    const e = Math.max(0, Number(errCount) || 0);
    const warnEl = document.getElementById("eventWarnCount");
    const errEl = document.getElementById("eventErrCount");
    if (warnEl) warnEl.textContent = String(w);
    if (errEl) errEl.textContent = String(e);
    if (!warnEl || !errEl) {
      badge.textContent = "warn " + w + " err " + e;
    }
    badge.classList.toggle("active", w > 0 || e > 0);
  }

  function showEventToast(kind, reason) {
    const toast = document.getElementById("eventToast");
    const kindEl = document.getElementById("eventToastKind");
    const reasonEl = document.getElementById("eventToastReason");
    if (!toast) return;
    const isError = kind === "error";
    toast.classList.remove("warn", "err");
    toast.classList.add("show");
    toast.classList.add(isError ? "err" : "warn");
    if (kindEl) kindEl.textContent = isError ? "ERROR" : "WARNING";
    if (reasonEl)
      reasonEl.textContent =
        reason || (isError ? "New error detected." : "New warning detected.");
    lastEventToastKind = isError ? "error" : "warning";
    eventToastVisible = true;
  }

  function hideEventToast() {
    const toast = document.getElementById("eventToast");
    if (!toast) return;
    toast.classList.remove("show", "warn", "err");
    eventToastVisible = false;
    lastEventToastKind = null;
  }

  async function fetchLastEvent(markRead) {
    const url = markRead ? "/last_event?mark_read=1" : "/last_event";
    const res = await fetch(url, { cache: "no-store", headers: cborHeaders() });
    if (res.status === 401) {
      noteAuthFailure();
      throw new Error("HTTP 401");
    }
    if (!res.ok) throw new Error("HTTP " + res.status);
    resetAuthFailures();
    return readCbor(res, {});
  }

  function applyUnreadCounts(warnCount, errCount, forceToast) {
    const w = Math.max(0, Number(warnCount) || 0);
    const e = Math.max(0, Number(errCount) || 0);
    const hasUnread = w > 0 || e > 0;
    const hasNew = w > lastEventUnread.warn || e > lastEventUnread.error;
    lastEventUnread = { warn: w, error: e };
    updateEventBadge(w, e);
    if (!hasUnread) {
      hideEventToast();
      return false;
    }
    return hasNew || forceToast;
  }

  function showEventToastFromPayload(data, warnCount, errCount) {
    const hasError = errCount > 0;
    let reason = "";
    if (hasError) {
      reason =
        (data && data.last_error && data.last_error.reason) ||
        (data && data.errors && data.errors[0] && data.errors[0].reason) ||
        "";
      showEventToast("error", reason);
    } else {
      reason =
        (data && data.last_warning && data.last_warning.reason) ||
        (data && data.warnings && data.warnings[0] && data.warnings[0].reason) ||
        "";
      showEventToast("warning", reason);
    }
  }

  function handleEventNotice(notice) {
    if (!notice) return;
    const unread = notice.unread || {};
    const w = Math.max(0, Number(unread.warn) || 0);
    const e = Math.max(0, Number(unread.error) || 0);
    if (!applyUnreadCounts(w, e, false)) return;
    const kind =
      notice.kind === "warning"
        ? "warning"
        : notice.kind === "error"
        ? "error"
        : e > 0
        ? "error"
        : "warning";
    const reason =
      notice.reason ||
      (kind === "error" &&
        notice.last_error &&
        notice.last_error.reason) ||
      (kind === "warning" &&
        notice.last_warning &&
        notice.last_warning.reason) ||
      "";
    showEventToast(kind, reason);
  }

  async function handleEventUnreadUpdate(unread) {
    if (!unread) return;
    const w = Math.max(0, Number(unread.warn) || 0);
    const e = Math.max(0, Number(unread.error) || 0);
    const shouldToast = applyUnreadCounts(w, e, !eventToastVisible);
    if (!shouldToast) return;
    try {
      const data = await fetchLastEvent(false);
      showEventToastFromPayload(data, w, e);
    } catch (err) {
      const kind = e > 0 ? "error" : "warning";
      showEventToast(kind, "");
    }
  }

  function rssiToBars(rssi) {
    if (!Number.isFinite(rssi)) return 0;
    if (rssi >= -55) return 4;
    if (rssi >= -67) return 3;
    if (rssi >= -75) return 2;
    if (rssi >= -85) return 1;
    return 0;
  }

  function updateWifiSignal(mon) {
    const wrap = document.getElementById("wifiSignal");
    const icon = document.getElementById("wifiSignalIcon");
    if (!wrap || !icon) return;
    if (!mon || !mon.wifiSta) {
      wrap.style.display = "none";
      return;
    }
    const connected = mon.wifiConnected !== false;
    const rssi = Number(mon.wifiRssi);
    const bars = connected ? rssiToBars(rssi) : 0;
    icon.src = "icons/wifi-" + bars + "-bars.png";
    wrap.style.display = "inline-flex";
    if (!connected) {
      wrap.title = "WiFi not connected";
    } else {
      wrap.title = Number.isFinite(rssi)
        ? "WiFi signal (" + rssi + " dBm)"
        : "WiFi signal";
    }
  }

  function formatTopTemp(val) {
    if (typeof val === "number" && val > -100) {
      return Math.round(val) + "\u00B0C";
    }
    return "Off";
  }

  function updateTopTemps(mon) {
    const boardEl = document.getElementById("boardTempText");
    const hsEl = document.getElementById("heatsinkTempText");
    if (!boardEl || !hsEl) return;
    if (!mon) return;
    boardEl.textContent = formatTopTemp(mon.boardTemp);
    hsEl.textContent = formatTopTemp(mon.heatsinkTemp);
  }

  function updateTopPower(mon) {
    const vEl = document.getElementById("dcVoltageText");
    const iEl = document.getElementById("dcCurrentText");
    if (!vEl || !iEl) return;
    if (!mon) return;
    const v = Number(mon.capVoltage);
    const i = readAcsCurrent(mon);
    vEl.textContent = Number.isFinite(v) ? v.toFixed(1) + "V" : "--V";
    iEl.textContent = Number.isFinite(i) ? i.toFixed(2) + "A" : "--A";
  }

  function bindErrorButton() {
    const btn = document.getElementById("errorBtn");
    if (!btn) return;
    btn.addEventListener("click", loadLastEventAndOpen);
  }

  function openLogModal() {
    const m = document.getElementById("logModal");
    if (!m) return;
    m.classList.add("show");
  }

  function closeLogModal() {
    const m = document.getElementById("logModal");
    if (!m) return;
    m.classList.remove("show");
  }

  async function loadDeviceLog() {
    const body = document.getElementById("logContent");
    if (!body) return;
    body.textContent = "Loading...";
    try {
      const res = await fetch("/device_log", { cache: "no-store" });
      if (!res.ok) {
        body.textContent = "Failed to load log (" + res.status + ")";
        return;
      }
      const text = await res.text();
      body.textContent = text && text.trim().length ? text : "No log data.";
    } catch (err) {
      body.textContent = "Failed to load log.";
      console.error("Log load failed:", err);
    }
  }

  async function clearDeviceLog() {
    const body = document.getElementById("logContent");
    if (body) body.textContent = "Clearing...";
    try {
      const res = await fetch("/device_log_clear", {
        method: "POST",
      });
      if (!res.ok) {
        if (body) body.textContent = "Clear failed (" + res.status + ")";
        return;
      }
      await loadDeviceLog();
    } catch (err) {
      if (body) body.textContent = "Clear failed.";
      console.error("Log clear failed:", err);
    }
  }

  function bindLogButton() {
    const btn = document.getElementById("logBtn");
    if (btn) {
      btn.addEventListener("click", async () => {
        openLogModal();
        await loadDeviceLog();
      });
    }
    const refreshBtn = document.getElementById("logRefreshBtn");
    if (refreshBtn) {
      refreshBtn.addEventListener("click", loadDeviceLog);
    }
    const clearBtn = document.getElementById("logClearBtn");
    if (clearBtn) {
      clearBtn.addEventListener("click", clearDeviceLog);
    }

    const modal = document.getElementById("logModal");
    if (modal) {
      const closeBtn = modal.querySelector(".log-close");
      const backdrop = modal.querySelector(".log-backdrop");
      if (closeBtn) closeBtn.addEventListener("click", closeLogModal);
      if (backdrop) backdrop.addEventListener("click", closeLogModal);
    }
  }

  // Expose for inline onclick handlers in HTML.
  window.closeLogModal = closeLogModal;
  window.openLogModal = openLogModal;

  function bindEventBadge() {
    const warnBtn = document.getElementById("eventWarnBtn");
    const errBtn = document.getElementById("eventErrBtn");
    if (warnBtn) {
      warnBtn.addEventListener("click", () => loadLastEventAndOpen("warning"));
      warnBtn.addEventListener("keydown", (e) => {
        if (e.key === "Enter" || e.key === " ") {
          e.preventDefault();
          loadLastEventAndOpen("warning");
        }
      });
    }
    if (errBtn) {
      errBtn.addEventListener("click", () => loadLastEventAndOpen("error"));
      errBtn.addEventListener("keydown", (e) => {
        if (e.key === "Enter" || e.key === " ") {
          e.preventDefault();
          loadLastEventAndOpen("error");
        }
      });
    }
  }

  function bindEventToast() {
    const toast = document.getElementById("eventToast");
    const viewBtn = document.getElementById("eventToastViewBtn");
    const openToast = () => {
      const focus =
        lastEventUnread.error > 0 || lastEventToastKind === "error"
          ? "error"
          : "warning";
      loadLastEventAndOpen(focus, { markRead: true });
    };
    if (viewBtn) viewBtn.addEventListener("click", openToast);
    if (toast) {
      toast.addEventListener("click", (e) => {
        if (e.target === viewBtn) return;
        openToast();
      });
    }
  }

  function formatEventTime(epochSec, ms) {
    if (epochSec) return formatEpochLocal(epochSec);
    if (ms) return fmtUptime(ms);
    return "--";
  }

  function renderEventList(listId, events, emptyText, kindLabel) {
    const list = document.getElementById(listId);
    if (!list) return;
    list.innerHTML = "";
    if (!Array.isArray(events) || events.length === 0) {
      const empty = document.createElement("div");
      empty.className = "error-empty";
      empty.textContent = emptyText || "No events logged yet.";
      list.appendChild(empty);
      return;
    }

    events.forEach((ev) => {
      const item = document.createElement("div");
      item.className =
        "event-item " + (kindLabel === "Warning" ? "warning" : "error");

      const kindEl = document.createElement("span");
      kindEl.className = "event-kind";
      kindEl.textContent = kindLabel;

      const reasonEl = document.createElement("span");
      reasonEl.className = "event-reason";
      reasonEl.textContent = ev.reason || "--";

      const timeEl = document.createElement("span");
      timeEl.className = "event-time";
      timeEl.textContent = formatEventTime(ev.epoch, ev.ms);

      item.appendChild(kindEl);
      item.appendChild(reasonEl);
      item.appendChild(timeEl);
      list.appendChild(item);
    });
  }

  function focusEventSection(sectionId) {
    const section = document.getElementById(sectionId);
    if (!section) return;
    section.scrollIntoView({ behavior: "smooth", block: "start" });
  }

  function setEventView(mode) {
    const modal = document.getElementById("errorModal");
    const title = document.getElementById("eventModalTitle");
    const warnSection = document.getElementById("warningHistorySection");
    const errSection = document.getElementById("errorHistorySection");
    const lastErrorSection = document.getElementById("lastErrorSection");
    const lastStopSection = document.getElementById("lastStopSection");
    const stateRow = document.getElementById("errorStateRow");
    if (!modal) return;
    modal.classList.remove("view-warning", "view-error");
    if (mode === "warning") {
      modal.classList.add("view-warning");
      if (title) title.textContent = "Warnings";
      if (warnSection) warnSection.style.display = "";
      if (errSection) errSection.style.display = "none";
      if (lastErrorSection) lastErrorSection.style.display = "none";
      if (lastStopSection) lastStopSection.style.display = "none";
      if (stateRow) stateRow.style.display = "none";
    } else if (mode === "error") {
      modal.classList.add("view-error");
      if (title) title.textContent = "Errors";
      if (warnSection) warnSection.style.display = "none";
      if (errSection) errSection.style.display = "";
      if (lastErrorSection) lastErrorSection.style.display = "";
      if (lastStopSection) lastStopSection.style.display = "none";
      if (stateRow) stateRow.style.display = "";
    } else {
      if (title) title.textContent = "Last Stop / Error";
      if (warnSection) warnSection.style.display = "";
      if (errSection) errSection.style.display = "";
      if (lastErrorSection) lastErrorSection.style.display = "";
      if (lastStopSection) lastStopSection.style.display = "";
      if (stateRow) stateRow.style.display = "";
    }
  }

  async function loadLastEventAndOpen(focus, opts = {}) {
    const markRead = opts.markRead !== false;
    setEventView(focus || "all");
    try {
      const data = await fetchLastEvent(markRead);
      const stateEl = document.getElementById("errorStateText");
      if (stateEl) stateEl.textContent = data.state || "--";

      const err = data.last_error || {};
      const stop = data.last_stop || {};

      const errReason = document.getElementById("errorReasonText");
      if (errReason) errReason.textContent = err.reason || "--";
      const errTime = document.getElementById("errorTimeText");
      if (errTime) errTime.textContent = formatEventTime(err.epoch, err.ms);

      const stopReason = document.getElementById("stopReasonText");
      if (stopReason) stopReason.textContent = stop.reason || "--";
      const stopTime = document.getElementById("stopTimeText");
      if (stopTime) stopTime.textContent = formatEventTime(stop.epoch, stop.ms);

      renderEventList(
        "warningList",
        data.warnings || [],
        "No warnings logged yet.",
        "Warning"
      );
      renderEventList(
        "errorList",
        data.errors || [],
        "No errors logged yet.",
        "Error"
      );
      if (data.unread) {
        applyUnreadCounts(data.unread.warn, data.unread.error, false);
      } else {
        applyUnreadCounts(0, 0, false);
      }
      if (markRead) hideEventToast();

      openErrorModal();
      if (focus === "warning") focusEventSection("warningHistorySection");
      else if (focus === "error") focusEventSection("errorHistorySection");
    } catch (err) {
      console.error("Last event load failed", err);
      setEventView(focus || "all");
      openErrorModal();
    }
  }

  async function loadSessionHistoryAndOpen() {
    try {
      // Load from SPIFFS/static file
      const res = await fetch("/History.cbor", {
        cache: "no-store",
        headers: cborHeaders(),
      });
      if (!res.ok) {
        console.error("Failed to load History.cbor:", res.status);
        openSessionHistory(); // Show empty modal instead of doing nothing
        return;
      }

      const data = await readCbor(res, {});

      // Support both { history: [...] } and plain [...]
      const history = Array.isArray(data) ? data : data.history || [];

      const tbody = document.getElementById("sessionHistoryTableBody");
      const placeholder = document.querySelector(
        ".session-history-placeholder"
      );
      if (!tbody) {
        openSessionHistory();
        return;
      }

      // Clear previous rows
      tbody.innerHTML = "";

      // Hide "No history loaded yet" placeholder once we try to load
      if (placeholder) {
        placeholder.style.display = "none";
      }

      if (!history.length) {
        const tr = document.createElement("tr");
        const td = document.createElement("td");
        td.colSpan = 5;
        td.textContent = "No sessions recorded yet.";
        tr.appendChild(td);
        tbody.appendChild(tr);
      } else {
        history.forEach((s, idx) => {
          const tr = document.createElement("tr");

          const startSec = (s.start_ms || 0) / 1000;
          const duration = s.duration_s || 0;
          const energyWh = Number(s.energy_Wh || 0);
          const peakP = Number(s.peakPower_W || 0);
          const peakI = Number(s.peakCurrent_A || 0);

          tr.innerHTML = `
          <td>${idx + 1}</td>
          <td>${startSec.toFixed(0)} s</td>
          <td>${duration}s</td>
          <td>${energyWh.toFixed(2)} Wh</td>
          <td>${peakP.toFixed(1)} W</td>
          <td>${peakI.toFixed(2)} A</td>
        `;
          tbody.appendChild(tr);
        });
      }

      // Open the modal (this is the correct function)
      openSessionHistory();
    } catch (e) {
      console.error("Session history load failed", e);
    }
  }

