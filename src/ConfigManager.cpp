
#include "ConfigManager.h"




ConfigManager::ConfigManager(Preferences* preferences) : 
preferences(preferences),namespaceName(CONFIG_PARTITION){}

ConfigManager::~ConfigManager() {
    end();  // Ensure preferences are closed properly
}

void ConfigManager::RestartSysDelayDown(unsigned long delayTime) {
    unsigned long startTime = millis();  // Record the start time
    DEBUG_PRINTLN("###########################################################");
    DEBUG_PRINTLN("#           Restarting the Device in: " + String(delayTime / 1000)+ " Sec              #" );
    DEBUG_PRINTLN("###########################################################");
    // Ensure 32 '#' are printed after the countdown
    unsigned long interval = delayTime / 30;  // Divide delayTime by 32 to get interval

    
        for (int i = 0; i < 30; i++) {  // Print 32 '#' characters
            DEBUG_PRINT("🔵");
            delay(interval);  // Delay for visibility of each '#' character
            esp_task_wdt_reset();  // Reset watchdog timer
        }
        DEBUG_PRINTLN();  // Move to the next line after printing
        DEBUG_PRINTLN("Restarting now...");

    simulatePowerDown();  // Simulate power down before restart
}

void ConfigManager::RestartSysDelay(unsigned long delayTime) {
    unsigned long startTime = millis();  // Record the start time
    DEBUG_PRINTLN("###########################################################");
    DEBUG_PRINTLN("#           Restarting the Device in: " + String(delayTime / 1000)+ " Sec              #" );
    DEBUG_PRINTLN("###########################################################");
    // Ensure 32 '#' are printed after the countdown
    unsigned long interval = delayTime / 30;  // Divide delayTime by 32 to get interval

        for (int i = 0; i < 30; i++) {  // Print 32 '#' characters
            DEBUG_PRINT("🔵");
            delay(interval);  // Delay for visibility of each '#' character
            esp_task_wdt_reset();  // Reset watchdog timer
        }
        DEBUG_PRINTLN();  // Move to the next line after printing

        DEBUG_PRINTLN("Restarting now...");
    //simulatePowerDown();  // Simulate power down before restart
     ESP.restart();
}

void ConfigManager::CountdownDelay(unsigned long delayTime) {
    unsigned long startTime = millis();  // Record the start time
    DEBUG_PRINTLN("###########################################################");
    DEBUG_PRINT("Waiting User Action: ");
    DEBUG_PRINT(delayTime / 1000);  // Convert delayTime to seconds
    DEBUG_PRINTLN(" Sec");
    // Ensure 32 '#' are printed after the countdown
    unsigned long interval = delayTime / 32;  // Divide delayTime by 32 to get interval

    for (int i = 0; i < 32; i++) {  // Print 32 '#' characters
        DEBUG_PRINT("#");
        delay(interval);  // Delay dynamically based on the given delayTime
        esp_task_wdt_reset();  // Reset watchdog timer
    }
    DEBUG_PRINTLN();  // Move to the next line after printing
}

void ConfigManager::simulatePowerDown() {
    // Put the ESP32 into deep sleep for 1 second (simulate power-down)
    esp_sleep_enable_timer_wakeup(1000000); // 1 second (in microseconds)
    esp_deep_sleep_start();  // Enter deep sleep
}

void ConfigManager::startPreferencesReadWrite() {
    preferences->begin(CONFIG_PARTITION, false);  // false = read-write mode
    DEBUG_PRINTLN("Preferences opened in write mode.");

}

void ConfigManager::startPreferencesRead() {
    preferences->begin(CONFIG_PARTITION, true);  // true = read-only mode
    DEBUG_PRINTLN("Preferences opened in read mode.");
}

void ConfigManager::begin() {
    DEBUG_PRINTLN("###########################################################");
    DEBUG_PRINTLN("#               Starting CONFIG Manager ⚙️                 #");
    DEBUG_PRINTLN("###########################################################");
    bool resetFlag = GetBool(RESET_FLAG, true);  // Default to true if not set
    if (resetFlag) {
        // Only print once, if necessary, then reset device
            DEBUG_PRINTLN("ConfigManager: Initializing the device... 🔄");
        initializeDefaults();  // Reset preferences if the flag is set
        RestartSysDelay(10000);  // Use a delay for restart after reset
    } else {
        // Use existing configuration, no need for unnecessary delay
            DEBUG_PRINTLN("ConfigManager: Using existing configuration... ✅");
    }
}

bool ConfigManager::getResetFlag() {
    esp_task_wdt_reset();
    bool value = preferences->getBool(RESET_FLAG, true); // Default to true if not set
    return value;
}

void ConfigManager::end() {
    preferences->end();  // Close preferences
}

void ConfigManager::initializeDefaults() {
    initializeVariables();  // Initialize all default variables
}

void ConfigManager::initializeVariables() { 
  // Reset flag
  PutBool(RESET_FLAG, false);

  // Generate unique SSID using last 3 bytes of MAC address
  String mac = WiFi.macAddress();        // Example: "24:6F:28:1A:2B:3C"
  mac.replace(":", "");                  // Remove colons → "246F281A2B3C"
  String suffix = mac.substring(6);      // Take last 6 hex characters → "1A2B3C"
  String ssid = String(DEVICE_WIFI_HOTSPOT_NAME) + suffix;  // Final SSID → "PDis_1A2B3C"

  // Wi-Fi credentials
  PutString(DEVICE_WIFI_HOTSPOT_NAME_KEY, ssid);
  PutString(DEVICE_AP_AUTH_PASS_KEY, DEVICE_AP_AUTH_PASS_DEFAULT);

  // Admin/User login
  PutString(ADMIN_ID_KEY, DEFAULT_ADMIN_ID);
  PutString(ADMIN_PASS_KEY, DEFAULT_ADMIN_PASS);
  PutString(USER_ID_KEY, DEFAULT_USER_ID);
  PutString(USER_PASS_KEY, DEFAULT_USER_PASS);

  // Timing and behavior
  PutInt(ON_TIME_KEY, DEFAULT_ON_TIME);
  PutInt(OFF_TIME_KEY, DEFAULT_OFF_TIME);
  PutInt(INRUSH_DELAY_KEY, DEFAULT_INRUSH_DELAY);
  PutBool(LED_FEEDBACK_KEY, DEFAULT_LED_FEEDBACK);
  PutFloat(TEMP_THRESHOLD_KEY, DEFAULT_TEMP_THRESHOLD);
  PutFloat(CHARGE_RESISTOR_KEY, DEFAULT_CHARGE_RESISTOR_OHMS);
  PutInt(AC_FREQUENCY_KEY, DEFAULT_AC_FREQUENCY);
  PutFloat(AC_VOLTAGE_KEY, DEFAULT_AC_VOLTAGE);
  PutFloat(DC_VOLTAGE_KEY, DEFAULT_DC_VOLTAGE);

  // Output access (admin-controlled)
  PutBool(OUT01_ACCESS_KEY, DEFAULT_OUT01_ACCESS);
  PutBool(OUT02_ACCESS_KEY, DEFAULT_OUT02_ACCESS);
  PutBool(OUT03_ACCESS_KEY, DEFAULT_OUT03_ACCESS);
  PutBool(OUT04_ACCESS_KEY, DEFAULT_OUT04_ACCESS);
  PutBool(OUT05_ACCESS_KEY, DEFAULT_OUT05_ACCESS);
  PutBool(OUT06_ACCESS_KEY, DEFAULT_OUT06_ACCESS);
  PutBool(OUT07_ACCESS_KEY, DEFAULT_OUT07_ACCESS);
  PutBool(OUT08_ACCESS_KEY, DEFAULT_OUT08_ACCESS);
  PutBool(OUT09_ACCESS_KEY, DEFAULT_OUT09_ACCESS);
  PutBool(OUT10_ACCESS_KEY, DEFAULT_OUT10_ACCESS);

  // Desired voltage setting
  PutFloat(DESIRED_OUTPUT_VOLTAGE_KEY, DEFAULT_DESIRED_OUTPUT_VOLTAGE);

  // Temperature sensor count
  PutInt(TEMP_SENSOR_COUNT_KEY, DEFAULT_TEMP_SENSOR_COUNT);
}

bool ConfigManager::GetBool(const char* key, bool defaultValue) {
    esp_task_wdt_reset();
    bool value = preferences->getBool(key, defaultValue);
    return value;
}

int ConfigManager::GetInt(const char* key, int defaultValue) {
    esp_task_wdt_reset();
    int value = preferences->getInt(key, defaultValue);
    return value;
}

uint64_t ConfigManager::GetULong64(const char* key, int defaultValue) {
    esp_task_wdt_reset();
    uint64_t value = preferences->getULong64(key, defaultValue);
    return value;
}

float ConfigManager::GetFloat(const char* key, float defaultValue) {
    esp_task_wdt_reset();
    float value = preferences->getFloat(key, defaultValue);
    return value;
}

String ConfigManager::GetString(const char* key, const String& defaultValue) {
    esp_task_wdt_reset();
    String value = preferences->getString(key, defaultValue);
    return value;
}

void ConfigManager::PutBool(const char* key, bool value) {
    esp_task_wdt_reset();
    RemoveKey(key);
    preferences->putBool(key, value);  // Store the new value
}

void ConfigManager::PutUInt(const char* key, int value) {
    esp_task_wdt_reset();
    RemoveKey(key);
    preferences->putUInt(key, value);  // Store the new value
}

void ConfigManager::PutULong64(const char* key, int value) {
    esp_task_wdt_reset();
    RemoveKey(key);
    preferences->putULong64(key, value);  // Store the new value
}

void ConfigManager::PutInt(const char* key, int value) {
    esp_task_wdt_reset();
    RemoveKey(key);
    preferences->putInt(key, value);  // Store the new value
}

void ConfigManager::PutFloat(const char* key, float value) {
    esp_task_wdt_reset();
    RemoveKey(key);
    preferences->putFloat(key, value);  // Store the new value
}

void ConfigManager::PutString(const char* key, const String& value) {
    esp_task_wdt_reset();
    RemoveKey(key);
    preferences->putString(key, value);  // Store the new value
}

void ConfigManager::ClearKey() {
    preferences->clear();
}

void ConfigManager::RemoveKey(const char * key) {
    esp_task_wdt_reset();  // Reset the watchdog timer
    // Check if the key exists before removing it
    if (preferences->isKey(key)) {
        preferences->remove(key);  // Remove the key if it exists
        #ifdef ENABLE_SERIAL_DEBUG
            //DEBUG_PRINT("Removed key: ");
            //DEBUG_PRINTLN(key);
        #endif
    } else {

            DEBUG_PRINT("Key not found, skipping: ");
            DEBUG_PRINTLN(key);
    }
}


