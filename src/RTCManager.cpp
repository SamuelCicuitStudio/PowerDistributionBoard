#include "RTCManager.h"
#include <time.h>
#include <sys/time.h>

// Constructor implementation
RTCManager::RTCManager(struct tm* timeinfo) {
    this->timeinfo = timeinfo;  // Store the pointer to the timeinfo struct
    update();  // Initialize time and date values
    if (DEBUGMODE)Serial.print("Last ON Time");
    if (DEBUGMODE)Serial.print("Time: ");
    if (DEBUGMODE)Serial.println(formattedTime);  // Print formatted time (HH:MM)
    if (DEBUGMODE)Serial.print("Date: ");
    if (DEBUGMODE)Serial.println(formattedDate);  // Print formatted date (YYYY-MM-DD)
}

// Set the system time from a Unix timestamp (seconds since Jan 1, 1970)
void RTCManager::setUnixTime(unsigned long timestamp) {
    struct timeval tv;
    tv.tv_sec = timestamp;  // Set seconds since the Unix epoch
    tv.tv_usec = 0;  // No microseconds
    esp_task_wdt_reset(); // Reset the watchdog timer to prevent a system reset
    settimeofday(&tv, nullptr); // Set system time
}

// Get the current Unix timestamp (seconds since Jan 1, 1970)
unsigned long RTCManager::getUnixTime() {
    time_t now;
    esp_task_wdt_reset(); // Reset the watchdog timer to prevent a system reset
    if (getLocalTime(timeinfo)) {
        // Convert time struct to Unix timestamp
        now = mktime(timeinfo);
        return now;
    }
    return 0;  // Return 0 if time can't be fetched
}

// Get the current time as a formatted string (HH:MM)
String RTCManager::getTime() {
    return formattedTime;
}

// Get the current date as a formatted string (YYYY-MM-DD)
String RTCManager::getDate() {
    return formattedDate;
}

// Update the formatted time and date values
void RTCManager::update() {
    if (getLocalTime(timeinfo)) {
        // Format time (HH:MM)
        char timeString[6];
        sprintf(timeString, "%02d:%02d", timeinfo->tm_hour, timeinfo->tm_min);
        formattedTime = String(timeString);

        // Format date (YYYY-MM-DD)
        char dateString[11];
        sprintf(dateString, "%04d-%02d-%02d", timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday);
        formattedDate = String(dateString);
    } else {
        if (DEBUGMODE)Serial.println("Failed to get local time.");
    }
}

// Function to set the time of the RTC directly
void RTCManager::setRTCTime(int year, int month, int day, int hour, int minute, int second) {
    // Set the time values directly in the timeinfo struct
    timeinfo->tm_year = year - 1900;  // Year since 1900 (e.g., 2025 -> 2025 - 1900)
    timeinfo->tm_mon = month - 1;     // Month (0-based, e.g., January is 0)
    timeinfo->tm_mday = day;          // Day of the month
    timeinfo->tm_hour = hour;         // Hour (24-hour format)
    timeinfo->tm_min = minute;        // Minute
    timeinfo->tm_sec = second;        // Second

    // Use settimeofday to update the system time
    struct timeval tv;
    tv.tv_sec = mktime(timeinfo);  // Convert tm structure to Unix timestamp
    tv.tv_usec = 0;  // No microseconds

    // Set the system time
    settimeofday(&tv, nullptr);

    // Update the formatted time and date in the class
    update();
}