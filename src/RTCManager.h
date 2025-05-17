#ifndef RTCMANAGER_H
#define RTCMANAGER_H

#include "ConfigManager.h"

class RTCManager {
public:
    RTCManager(struct tm* timeinfo);  // Constructor

    void setUnixTime(unsigned long timestamp);  // Set RTC time using Unix timestamp
    unsigned long getUnixTime();  // Get current Unix timestamp
    String getTime();  // Get current time as a formatted string (HH:MM)
    String getDate();  // Get current date as a formatted string (YYYY-MM-DD)
    void update();  // Update time and date values
    void setRTCTime(int year, int month, int day, int hour, int minute, int second);

private:
    struct tm* timeinfo;  // Struct to hold time information
    String formattedTime;  // Stores the formatted time (HH:MM)
    String formattedDate;  // Stores the formatted date (YYYY-MM-DD)
};

#endif  // RTCMANAGER_H