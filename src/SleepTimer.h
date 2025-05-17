#ifndef SLEEPTIMER_H
#define SLEEPTIMER_H

#include "Config.h"
#include <WiFi.h>
#include "RTCManager.h"
#include "ConfigManager.h"
#include "Logger.h" 

class SleepTimer {
private:
    unsigned long lastActivityTime;  // Last user interaction
    bool wifiActive;                 // Tracks Wi-Fi state
    RTCManager* RTC;
    ConfigManager* Conf;
    Logger* Log;

public:
    SleepTimer(RTCManager* RTC, ConfigManager* Conf,Logger* Log);
    void reset();             // Reset inactivity timer
    void checkInactivity();   // Check if Wi-Fi should be disabled
    void timerLoop();         // Run timer loop in background task
    void disableWiFi();       // Actually turn Wi-Fi off
};

#endif // SLEEPTIMER_H
