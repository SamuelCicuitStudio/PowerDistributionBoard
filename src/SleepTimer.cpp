#include "SleepTimer.h"

// ==================================================
// Constructor
// ==================================================
SleepTimer::SleepTimer(RTCManager* RTC, ConfigManager* Conf,Logger* Log) : RTC(RTC), Conf(Conf),Log(Log) {
    this->lastActivityTime = millis();  // Initialize with the current time
    this->wifiActive = true;            // Wi-Fi starts as active
}

// ==================================================
// Reset the Inactivity Countdown
// ==================================================
void SleepTimer::reset() {
    lastActivityTime = millis();  // Reset the countdown to current time
}

// ==================================================
// Check the Inactivity Timer and Disable Wi-Fi
// ==================================================
void SleepTimer::checkInactivity() {
    if ((millis() - lastActivityTime) >= SLEEP_TIMER && wifiActive) {
        disableWiFi();
    }
}

// ==================================================
// Timer Loop Task - Checks inactivity every 120 second
// ==================================================
void SleepTimer::timerLoop() {
    xTaskCreate([](void* parameter) {
        auto* sleepTimer = static_cast<SleepTimer*>(parameter);
        const TickType_t twoMinutes = pdMS_TO_TICKS(120000);  // 120 000 ms = 2 min
        for (;;) {
            sleepTimer->checkInactivity();
            vTaskDelay(twoMinutes);
        }
    }, 
    "TimerLoopTask", 
    2048, 
    this, 
    1, 
    nullptr);
}


// ==================================================
// Disable Wi-Fi Instead of Sleep Mode
// ==================================================
void SleepTimer::disableWiFi() {
    Log->logWifiTimeout();
    wifiActive = false;
    if (DEBUGMODE) Serial.println("Wi-Fi timeout reached. Disabling Wi-Fi...");
    
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
}
