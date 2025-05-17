#ifndef LOGGER_H
#define LOGGER_H

#include "Config.h"
#include <FS.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include "RTCManager.h"

/**
 * @brief Manages logging of system events, errors, and power states to SPIFFS.
 *        Provides timestamped log entries using the RTCManager.
 */
class Logger {
private:
    bool initialized;          // Tracks whether the SPIFFS filesystem is initialized
    RTCManager* Rtc;           // Pointer to the RTCManager instance for timestamps

public:
    Logger(RTCManager* Rtc);   // Constructor

    bool Begin();              // Initialize SPIFFS and prepare the log file
    bool addLogEntry(const JsonObject& newEntry);  // Adds a new entry to the log file
    String readLogFile();     // Reads the entire content of the log file as a string
    bool clearLogFile();      // Clears the contents of the log file
    bool deleteLogFile();     // Deletes the log file from SPIFFS
    bool createLogFile();     // Creates a new, empty log file

    // Custom event logs for the power management unit and system events
    void logInfo(const String& message);                // Logs a generic informational message
    void logError(const String& message);               // Logs an error message with status=false
    void logUserConnected(const String& mac);           // Logs when a user connects via Wi-Fi
    void log12VAvailable();                             // Logs when 12V power becomes available
    void logWifiSwitchActivated();                      // Logs when Wi-Fi is manually activated via switch
    void logWifiTimeout();                              // Logs Wi-Fi auto disconnect after inactivity
};

#endif // LOGGER_H
