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

// Centralized list of HTTP endpoints served by WiFiManager.
// Keep this in sync with WiFiManager::registerRoutes_ so UI/backend share a single source of truth.

#define EP_LOGIN             "/login"          // Login page
#define EP_DEVICE_INFO       "/device_info"    // Device identity/version (JSON)
#define EP_HEARTBEAT         "/heartbeat"      // Session keep-alive ping
#define EP_CONNECT           "/connect"        // POST credentials to start a session
#define EP_DISCONNECT        "/disconnect"     // POST to end session and redirect to login
#define EP_STATE_STREAM      "/state_stream"   // Server-Sent Events stream of device state
#define EP_MONITOR           "/monitor"        // Telemetry snapshot (caps, temps, outputs, session stats)
#define EP_CONTROL           "/control"        // Control commands (set/get)
#define EP_LOAD_CONTROLS     "/load_controls"  // Load persisted control/config values
#define EP_SESSION_HISTORY   "/session_history"// Live history JSON (from tracker)
#define EP_HISTORY_JSON      "/History.json"   // Static history file fallback
#define EP_CALIB_STATUS      "/calib_status"   // Calibration recorder status
#define EP_CALIB_START       "/calib_start"    // Start calibration recorder
#define EP_CALIB_STOP        "/calib_stop"     // Stop calibration recorder
#define EP_CALIB_CLEAR       "/calib_clear"    // Clear calibration recorder data
#define EP_CALIB_DATA        "/calib_data"     // Calibration recorder data
#define EP_CALIB_FILE        "/calib_file"     // Calibration recorder JSON file
#define EP_LAST_EVENT        "/last_event"     // Last stop/error details
#define EP_FAVICON           "/favicon.ico"    // Favicon
#define EP_ADMIN_PAGE        "/admin.html"     // Admin UI shell
#define EP_USER_PAGE         "/user.html"      // User UI shell

#endif // WIFIENPOIN_H


