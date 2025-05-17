#include "Logger.h"

Logger::Logger(RTCManager* rtc) : Rtc(rtc) {
    if (DEBUGMODE) {
        Serial.println("###########################################################");
        Serial.println("#               Starting Power Log Manager               #");
        Serial.println("###########################################################");
    }
}

bool Logger::Begin() {
    if (!SPIFFS.begin(true)) {
        Serial.println("Failed to mount file system");
        return false;
    }

    Serial.println("Mounted file system");

    if (!SPIFFS.exists(LOGFILE_PATH)) {
        Serial.println("Log file not found. Creating a new one.");
        createLogFile();
    } else {
        Serial.println("Log file already exists.");
    }

    initialized = true;
    return true;
}

bool Logger::addLogEntry(const JsonObject& newEntry) {
    if (!initialized) return false;

    File logFile = SPIFFS.open(LOGFILE_PATH, FILE_APPEND);
    if (!logFile) {
        if (DEBUGMODE) Serial.println("Failed to open log file for appending");
        return false;
    }

    logFile.print("{");
    logFile.print("\"timestamp\":\"" + Rtc->getDate() + " " + Rtc->getTime() + "\",");
    logFile.print("\"event_type\":\"" + String(newEntry["event_type"].as<String>()) + "\",");
    logFile.print("\"message\":\"" + String(newEntry["message"].as<String>()) + "\",");
    if (newEntry.containsKey("mac_address")) {
        logFile.print("\"mac_address\":\"" + String(newEntry["mac_address"].as<String>()) + "\",");
    }
    logFile.print("\"status\":" + String(newEntry["status"].as<bool>() ? "true" : "false"));
    logFile.print("},\n");

    logFile.close();
    return true;
}

String Logger::readLogFile() {
    String content = "";
    if (!initialized) return content;

    File logFile = SPIFFS.open(LOGFILE_PATH, FILE_READ);
    if (!logFile) {
        if (DEBUGMODE) Serial.println("Failed to open log file for reading");
        return content;
    }

    while (logFile.available()) {
        content += (char)logFile.read();
    }

    logFile.close();
    return content;
}

bool Logger::clearLogFile() {
    if (!initialized) return false;

    File file = SPIFFS.open(LOGFILE_PATH, FILE_WRITE);
    if (!file) {
        if (DEBUGMODE) Serial.println("Failed to clear log file");
        return false;
    }

    file.print("");  // Just clear content
    file.close();
    if (DEBUGMODE) Serial.println("Log file cleared");
    return true;
}

bool Logger::deleteLogFile() {
    if (!initialized) return false;
    return SPIFFS.remove(LOGFILE_PATH);
}

bool Logger::createLogFile() {
    File file = SPIFFS.open(LOGFILE_PATH, FILE_WRITE);
    if (!file) {
        if (DEBUGMODE) Serial.println("Failed to create log file");
        return false;
    }

    file.print(""); // Empty initially
    file.close();
    if (DEBUGMODE) Serial.println("Log file created");
    return true;
}

// Specific log methods

void Logger::logUserConnected(const String& mac) {
    if (!initialized) return;

    DynamicJsonDocument doc(256);
    doc["event_type"] = "wifi_user_connected";
    doc["message"] = "A user connected via Wi-Fi";
    doc["mac_address"] = mac;
    doc["status"] = true;
    addLogEntry(doc.as<JsonObject>());
}

void Logger::log12VAvailable() {
    if (!initialized) return;

    DynamicJsonDocument doc(256);
    doc["event_type"] = "power";
    doc["message"] = "12V supply is available";
    doc["status"] = true;
    addLogEntry(doc.as<JsonObject>());
}

void Logger::logWifiSwitchActivated() {
    if (!initialized) return;

    DynamicJsonDocument doc(256);
    doc["event_type"] = "wifi_switch";
    doc["message"] = "Wi-Fi manually activated via switch";
    doc["status"] = true;
    addLogEntry(doc.as<JsonObject>());
}

void Logger::logWifiTimeout() {
    if (!initialized) return;

    DynamicJsonDocument doc(256);
    doc["event_type"] = "wifi_timeout";
    doc["message"] = "Wi-Fi disconnected after 4 minutes of inactivity";
    doc["status"] = false;
    addLogEntry(doc.as<JsonObject>());
}

void Logger::logInfo(const String& message) {
    if (!initialized) return;

    DynamicJsonDocument doc(256);
    doc["event_type"] = "info";
    doc["message"] = message;
    doc["status"] = true;
    addLogEntry(doc.as<JsonObject>());
}

void Logger::logError(const String& message) {
    if (!initialized) return;

    DynamicJsonDocument doc(256);
    doc["event_type"] = "error";
    doc["message"] = message;
    doc["status"] = false;
    addLogEntry(doc.as<JsonObject>());
}
