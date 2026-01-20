  // ========================================================
  // ===============   GLOBAL HELPERS / STATE   =============
  // ========================================================

  const powerEl = () => document.getElementById("powerButton");
  const powerText = () => document.getElementById("powerLabel");

  let lastState = "Shutdown"; // Device state from backend
  let pendingConfirm = null; // For confirm modal
  let pendingConfirmAction = null;
  let lastLoadedControls = null; // Snapshot from /load_controls
  let lastSetupStatus = null; // Snapshot from /setup_status
  let lastMonitor = null; // Snapshot from /monitor
  let isMuted = false; // Buzzer mute cached state
  let stateStream = null; // EventSource for zero-lag state
  let eventStream = null; // EventSource for warnings/errors
  let statePollTimer = null; // Fallback polling timer
  let monitorPollTimer = null; // /monitor polling timer (UI snapshot)
  let heartbeatTimer = null; // /heartbeat keepalive timer
  let dashboardClockTimer = null; // dashboard clock updater timer
  let calibrationBusy = false; // prevent overlapping manual calibrations
  let setupRunAllowed = true;
  let setupConfigOk = true;
  let setupCalibOk = true;
  let wizardActive = false;
  let wizardStepIndex = 0;
  const WIZARD_SKIP_DEFAULTS = {
    wire: false,
    floor: false,
    wifi: false,
    cred: false,
    presence: false,
  };
  let wizardSkipped = { ...WIZARD_SKIP_DEFAULTS };
  const WIZARD_SKIP_STORAGE_KEY = "pbWizardSkipped";
  const wizardNodeCache = [];
  let ntcCalPollTimer = null;
  let ntcCalPollIntervalMs = 1000;
  let liveControlSamples = [];
  let liveControlStartPerf = null;
  let liveControlChartPaused = false;
  let liveControlMaxSamples = 1200;
  let liveControlLastIntervalMs = 1000;
  let liveControlModalOpen = false;
  let testModeState = { active: false, label: "--", targetC: NaN };
  let ambientWaitState = { active: false, label: "--", tolC: NaN, sinceMs: 0 };
  let lastWireTestStatus = null;
  let lastEventUnread = { warn: 0, error: 0 };
  let lastEventToastKind = null;
  let eventToastVisible = false;
  let authFailCount = 0;
  const AUTH_FAIL_THRESHOLD = 3;
  let liveControlDrag = {
    dragging: false,
    startX: 0,
    startScrollLeft: 0,
  };
  let wiresCoolPromptTimer = null;

  const CBOR_MIME = "application/cbor";

  function cborHeaders(extra) {
    if (window.pbCborHeaders) return window.pbCborHeaders(extra);
    const headers = new Headers(extra || {});
    if (!headers.has("Content-Type")) headers.set("Content-Type", CBOR_MIME);
    if (!headers.has("Accept")) headers.set("Accept", CBOR_MIME);
    return headers;
  }

  function encodeCbor(payload) {
    if (window.pbEncodeCbor) return window.pbEncodeCbor(payload);
    return new Uint8Array(0);
  }

  function encodeCborBase64(payload) {
    if (window.pbEncodeCborBase64) return window.pbEncodeCborBase64(payload);
    return "";
  }

  function decodeCborBase64(raw) {
    if (!raw) return null;
    if (window.pbDecodeCborBase64) return window.pbDecodeCborBase64(raw);
    return null;
  }

  async function readCbor(res, fallback) {
    if (!res) return fallback;
    try {
      if (window.pbReadCbor) {
        const data = await window.pbReadCbor(res);
        return data == null ? fallback : data;
      }
      if (window.pbDecodeCbor) {
        const buf = await res.arrayBuffer();
        if (!buf || !buf.byteLength) return fallback;
        const data = window.pbDecodeCbor(buf);
        return data == null ? fallback : data;
      }
    } catch (e) {
      return fallback;
    }
    return fallback;
  }

  function decodeSsePayload(ev) {
    const raw = ev && typeof ev.data === "string" ? ev.data : "";
    if (!raw) return null;
    return decodeCborBase64(raw);
  }

  function loadWizardSkipped() {
    try {
      const raw = localStorage.getItem(WIZARD_SKIP_STORAGE_KEY);
      if (!raw) return;
      const data = decodeCborBase64(raw);
      if (!data || typeof data !== "object") {
        localStorage.removeItem(WIZARD_SKIP_STORAGE_KEY);
        return;
      }
      wizardSkipped = { ...wizardSkipped, ...data };
    } catch (e) {
      // ignore storage errors
    }
  }

  function saveWizardSkipped() {
    try {
      const encoded = encodeCborBase64(wizardSkipped);
      if (!encoded) {
        localStorage.removeItem(WIZARD_SKIP_STORAGE_KEY);
        return;
      }
      localStorage.setItem(WIZARD_SKIP_STORAGE_KEY, encoded);
    } catch (e) {
      // ignore storage errors
    }
  }

  function resetWizardSkipped() {
    wizardSkipped = { ...WIZARD_SKIP_DEFAULTS };
    saveWizardSkipped();
  }

  function setWizardSkipped(key, value) {
    if (!key) return;
    const next = !!value;
    if (wizardSkipped[key] === next) return;
    wizardSkipped[key] = next;
    saveWizardSkipped();
  }

  loadWizardSkipped();

  function redirectToLogin() {
    if (window.pbClearToken) window.pbClearToken();
    if (window.pbLoginUrl) {
      window.location.href = window.pbLoginUrl();
    } else {
      window.location.href = "login.html";
    }
  }

  function noteAuthFailure() {
    authFailCount += 1;
    if (authFailCount >= AUTH_FAIL_THRESHOLD) {
      redirectToLogin();
    }
  }

  function resetAuthFailures() {
    authFailCount = 0;
  }

  const adminHasToken = !(window.pbGetToken && !window.pbGetToken());
  if (!adminHasToken) {
    redirectToLogin();
  }

