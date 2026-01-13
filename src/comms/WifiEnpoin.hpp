/**************************************************************
 *  Author      : Tshibangu Samuel
 *  Role        : Freelance Embedded Systems Engineer
 *  Expertise   : Secure IoT Systems, Embedded C++, RTOS, Control Logic
 *  Contact     : tshibsamuel47@gmail.com
 *  Portfolio   : https://www.freelancer.com/u/tshibsamuel477
 *  Phone       : +216 54 429 793
 **************************************************************/
#ifndef WIFIENPOIN_H
#define WIFIENPOIN_H

// Centralized list of HTTP endpoints and response strings used by WiFiManager.
// Keep this in sync with WiFiManager::registerRoutes_ so UI/backend share a single source of truth.

// ===== Endpoints (HTTP/SSE) =====
#define EP_LOGIN              "/login"               // Login page
#define EP_DEVICE_INFO        "/device_info"         // Device identity/version (JSON)
#define EP_HEARTBEAT          "/heartbeat"           // Session keep-alive ping
#define EP_CONNECT            "/connect"             // POST credentials to start a session
#define EP_DISCONNECT         "/disconnect"          // POST to end session and redirect to login
#define EP_STATE_STREAM       "/state_stream"        // Server-Sent Events stream of device state
#define EP_EVENT_STREAM       "/event_stream"        // Server-Sent Events stream of device events
#define EP_MONITOR_STREAM     "/monitor_stream"      // Server-Sent Events stream of live monitor
#define EP_MONITOR_SINCE      "/monitor_since"       // Live monitor batch (HTTP)
#define EP_MONITOR            "/monitor"             // Telemetry snapshot (caps, temps, outputs, session stats)
#define EP_CONTROL            "/control"             // Control commands (set/get)
#define EP_LOAD_CONTROLS      "/load_controls"       // Load persisted control/config values
#define EP_SESSION_HISTORY    "/session_history"     // Live history JSON (from tracker)
#define EP_HISTORY_JSON       "/History.json"        // Static history file fallback
#define EP_DEVICE_LOG         "/device_log"          // Device log readout
#define EP_DEVICE_LOG_CLEAR   "/device_log_clear"    // Clear device log
#define EP_CALIB_STATUS       "/calib_status"        // Calibration recorder status
#define EP_CALIB_START        "/calib_start"         // Start calibration recorder
#define EP_CALIB_STOP         "/calib_stop"          // Stop calibration recorder
#define EP_CALIB_CLEAR        "/calib_clear"         // Clear calibration recorder data
#define EP_CALIB_DATA         "/calib_data"          // Calibration recorder data
#define EP_CALIB_FILE         "/calib_file"          // Calibration recorder JSON file
#define EP_CALIB_HISTORY_LIST "/calib_history_list"  // Calibration history list
#define EP_CALIB_HISTORY_FILE "/calib_history_file"  // Calibration history file fetch
#define EP_CALIB_PI_SUGGEST   "/calib_pi_suggest"    // Thermal model suggestion
#define EP_CALIB_PI_SAVE      "/calib_pi_save"       // Thermal model save
#define EP_WIRE_TEST_STATUS   "/wire_test_status"    // Wire test status
#define EP_WIRE_TEST_START    "/wire_test_start"     // Wire test start
#define EP_WIRE_TEST_STOP     "/wire_test_stop"      // Wire test stop
#define EP_NTC_CALIBRATE      "/ntc_calibrate"       // NTC multi-point calibration
#define EP_NTC_BETA_CALIBRATE "/ntc_beta_calibrate"  // NTC single-point beta calibration
#define EP_NTC_CAL_STATUS     "/ntc_cal_status"      // NTC calibration status
#define EP_NTC_CAL_STOP       "/ntc_cal_stop"        // NTC calibration stop
#define EP_AP_CONFIG          "/ap_config"           // Access Point credentials update
#define EP_LAST_EVENT         "/last_event"          // Last stop/error details
#define EP_FAVICON            "/favicon.ico"         // Favicon
#define EP_ADMIN_PAGE         "/admin.html"          // Admin UI shell
#define EP_USER_PAGE          "/user.html"           // User UI shell
#define EP_LOGIN_HTML         "/login.html"          // Login HTML page
#define EP_LOGIN_FAILED_PAGE  "/login_failed.html"   // Login failed page

// ===== Static asset roots =====
#define EP_STATIC_ROOT        "/"        // Root
#define EP_STATIC_ICONS       "/icons/"  // Icons
#define EP_STATIC_CSS         "/css/"    // Styles
#define EP_STATIC_JS          "/js/"     // Scripts
#define EP_STATIC_FONTS       "/fonts/"  // Fonts

// ===== Redirect URLs =====
#define URL_LOGIN_REDIRECT    "http://powerboard.local/login"

// ===== Content types =====
#define CT_APP_JSON           "application/json"
#define CT_TEXT_PLAIN         "text/plain"
#define CT_TEXT_HTML          "text/html"

// ===== Cache control =====
#define CACHE_CONTROL_NO_STORE "no-store, must-revalidate"

// ===== SSE event names =====
#define SSE_EVENT_STATE       "state"
#define SSE_EVENT_EVENT       "event"

// ===== Status/mode strings used in responses =====
#define STATUS_OK             "ok"
#define STATUS_RUNNING        "running"
#define STATUS_IDLE           "idle"
#define STATUS_STOPPING       "stopping"

#define MODE_ENERGY           "energy"
#define MODE_NTC              "ntc"
#define MODE_MODEL            "model"
#define MODE_NONE             "none"

#define PURPOSE_NONE          "none"
#define PURPOSE_WIRE_TEST     "wire_test"
#define PURPOSE_MODEL_CAL     "model_cal"
#define PURPOSE_NTC_CAL       "ntc_cal"

// ===== Device state strings =====
#define STATE_IDLE            "Idle"
#define STATE_RUNNING         "Running"
#define STATE_ERROR           "Error"
#define STATE_SHUTDOWN        "Shutdown"
#define STATE_UNKNOWN         "Unknown"

// ===== Floor material strings =====
#define FLOOR_MAT_WOOD_STR     "wood"
#define FLOOR_MAT_EPOXY_STR    "epoxy"
#define FLOOR_MAT_CONCRETE_STR "concrete"
#define FLOOR_MAT_SLATE_STR    "slate"
#define FLOOR_MAT_MARBLE_STR   "marble"
#define FLOOR_MAT_GRANITE_STR  "granite"

// ===== Error codes (response values) =====
#define ERR_ALREADY_CONNECTED       "Already connected"
#define ERR_INVALID_JSON            "Invalid JSON"
#define ERR_INVALID_ACTION          "Invalid action"
#define ERR_INVALID_ACTION_TARGET   "Invalid action or target"
#define ERR_MISSING_FIELDS          "Missing fields"
#define ERR_NOT_AUTHENTICATED       "Not authenticated"
#define ERR_UNKNOWN_TARGET          "Unknown target"
#define ERR_ALLOC_FAILED            "alloc_failed"
#define ERR_ALREADY_RUNNING         "already_running"
#define ERR_BAD_PASSWORD            "bad_password"
#define ERR_BUS_SAMPLER_MISSING     "bus_sampler_missing"
#define ERR_CALIBRATION_BUSY        "calibration_busy"
#define ERR_CALIBRATION_FAILED      "calibration_failed"
#define ERR_CTRL_QUEUE_FULL         "ctrl_queue_full"
#define ERR_DEVICE_MISSING          "device_missing"
#define ERR_DEVICE_NOT_IDLE         "device_not_idle"
#define ERR_DEVICE_TRANSPORT_MISSING "device_transport_missing"
#define ERR_ENERGY_START_FAILED     "energy_start_failed"
#define ERR_ENERGY_STOPPED          "energy_stopped"
#define ERR_FIT_FAILED              "fit_failed"
#define ERR_INVALID_COEFFS          "invalid_coeffs"
#define ERR_INVALID_MODE            "invalid_mode"
#define ERR_INVALID_NAME            "invalid_name"
#define ERR_INVALID_REF_TEMP        "invalid_ref_temp"
#define ERR_INVALID_TARGET          "invalid_target"
#define ERR_MISSING_NAME            "missing_name"
#define ERR_NOT_ENOUGH_SAMPLES      "not_enough_samples"
#define ERR_NOT_FOUND               "not_found"
#define ERR_NTC_MISSING             "ntc_missing"
#define ERR_PERSIST_FAILED          "persist_failed"
#define ERR_FAILED                 "failed"
#define ERR_SENSOR_MISSING          "sensor_missing"
#define ERR_SNAPSHOT_BUSY           "snapshot_busy"
#define ERR_START_FAILED            "start_failed"
#define ERR_STATUS_UNAVAILABLE      "status_unavailable"
#define ERR_STOPPED                 "stopped"
#define ERR_TASK_FAILED             "task_failed"
#define ERR_TIMEOUT                 "timeout"
#define ERR_WIRE_ACCESS_BLOCKED     "wire_access_blocked"
#define ERR_WIRE_NOT_CONNECTED      "wire_not_connected"
#define ERR_WIRE_SUBSYSTEM_MISSING  "wire_subsystem_missing"

// ===== Response body helpers =====
#define RESP_ALIVE                  "alive"
#define RESP_OK_TRUE                "{\"ok\":true}"
#define RESP_HISTORY_EMPTY          "{\"history\":[]}"
#define RESP_STATUS_IDLE            "{\"status\":\"" STATUS_IDLE "\"}"
#define RESP_STATUS_STOPPING        "{\"status\":\"" STATUS_STOPPING "\"}"
#define RESP_STATUS_OK_APPLIED      "{\"status\":\"" STATUS_OK "\",\"applied\":true}"
#define RESP_STATUS_OK_QUEUED       "{\"status\":\"" STATUS_OK "\",\"queued\":true}"
#define RESP_STATUS_OK_RUNNING_TRUE "{\"status\":\"" STATUS_OK "\",\"running\":true}"
#define RESP_STATUS_OK_RUNNING_FALSE "{\"status\":\"" STATUS_OK "\",\"running\":false}"

#define RESP_STATUS_OK_RUNNING_FALSE_SAVED_PREFIX \
  "{\"status\":\"" STATUS_OK "\",\"running\":false,\"saved\":"
#define RESP_STATUS_OK_CLEARED_FILE_PREFIX \
  "{\"status\":\"" STATUS_OK "\",\"cleared\":true,\"file_removed\":"
#define RESP_HISTORY_REMOVED_PREFIX ",\"history_removed\":"

#define RESP_STATE_JSON_PREFIX      "{\"state\":\""
#define RESP_STATE_JSON_MID         "\",\"seq\":"
#define RESP_STATE_JSON_TAIL        ",\"sinceMs\":"
#define RESP_STATE_JSON_SUFFIX      "}"

#define JSON_TRUE                   "true"
#define JSON_FALSE                  "false"

#define RESP_ERR_ALREADY_CONNECTED  "{\"error\":\"" ERR_ALREADY_CONNECTED "\"}"
#define RESP_ERR_INVALID_JSON       "{\"error\":\"" ERR_INVALID_JSON "\"}"
#define RESP_ERR_INVALID_ACTION     "{\"error\":\"" ERR_INVALID_ACTION "\"}"
#define RESP_ERR_INVALID_ACTION_TARGET "{\"error\":\"" ERR_INVALID_ACTION_TARGET "\"}"
#define RESP_ERR_MISSING_FIELDS     "{\"error\":\"" ERR_MISSING_FIELDS "\"}"
#define RESP_ERR_NOT_AUTHENTICATED  "{\"error\":\"" ERR_NOT_AUTHENTICATED "\"}"
#define RESP_ERR_UNKNOWN_TARGET     "{\"error\":\"" ERR_UNKNOWN_TARGET "\"}"
#define RESP_ERR_ALLOC_FAILED       "{\"error\":\"" ERR_ALLOC_FAILED "\"}"
#define RESP_ERR_BAD_PASSWORD       "{\"error\":\"" ERR_BAD_PASSWORD "\"}"
#define RESP_ERR_CALIBRATION_BUSY   "{\"error\":\"" ERR_CALIBRATION_BUSY "\"}"
#define RESP_ERR_CALIBRATION_FAILED "{\"error\":\"" ERR_CALIBRATION_FAILED "\"}"
#define RESP_ERR_CTRL_QUEUE_FULL    "{\"error\":\"" ERR_CTRL_QUEUE_FULL "\"}"
#define RESP_ERR_DEVICE_MISSING     "{\"error\":\"" ERR_DEVICE_MISSING "\"}"
#define RESP_ERR_DEVICE_NOT_IDLE    "{\"error\":\"" ERR_DEVICE_NOT_IDLE "\"}"
#define RESP_ERR_INVALID_COEFFS     "{\"error\":\"" ERR_INVALID_COEFFS "\"}"
#define RESP_ERR_INVALID_NAME       "{\"error\":\"" ERR_INVALID_NAME "\"}"
#define RESP_ERR_INVALID_REF_TEMP   "{\"error\":\"" ERR_INVALID_REF_TEMP "\"}"
#define RESP_ERR_INVALID_TARGET     "{\"error\":\"" ERR_INVALID_TARGET "\"}"
#define RESP_ERR_MISSING_NAME       "{\"error\":\"" ERR_MISSING_NAME "\"}"
#define RESP_ERR_NOT_FOUND          "{\"error\":\"" ERR_NOT_FOUND "\"}"
#define RESP_ERR_NTC_MISSING        "{\"error\":\"" ERR_NTC_MISSING "\"}"
#define RESP_ERR_SNAPSHOT_BUSY      "{\"error\":\"" ERR_SNAPSHOT_BUSY "\"}"
#define RESP_ERR_START_FAILED       "{\"error\":\"" ERR_START_FAILED "\"}"
#define RESP_ERR_STATUS_UNAVAILABLE "{\"error\":\"" ERR_STATUS_UNAVAILABLE "\"}"
#define RESP_ERR_TASK_FAILED        "{\"error\":\"" ERR_TASK_FAILED "\"}"
#define RESP_ERR_WIRE_ACCESS_BLOCKED "{\"error\":\"" ERR_WIRE_ACCESS_BLOCKED "\"}"
#define RESP_ERR_WIRE_NOT_CONNECTED "{\"error\":\"" ERR_WIRE_NOT_CONNECTED "\"}"
#define RESP_ERR_WIRE_SUBSYSTEM_MISSING "{\"error\":\"" ERR_WIRE_SUBSYSTEM_MISSING "\"}"


#endif // WIFIENPOIN_H


