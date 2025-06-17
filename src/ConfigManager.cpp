
#include "ConfigManager.h"



/************************************************************************************************/
/*                           Config Manager class definition                                    */
/************************************************************************************************/
/**
 * @brief Constructor for the ConfigManager class.
 * 
 * Initializes the ConfigManager with a reference to an external Preferences object,
 * which is used for storing and retrieving configuration settings.
 * 
 * @param prefs Reference to the Preferences object.
 */
ConfigManager::ConfigManager(Preferences* preferences) : preferences(preferences),namespaceName(CONFIG_PARTITION) {}

/**
 * @brief Destructor for the ConfigManager class.
 * 
 * Ensures that the preferences are properly closed when the ConfigManager 
 * object is destroyed, preventing memory leaks and ensuring data integrity.
 */
ConfigManager::~ConfigManager() {
    end();  // Ensure preferences are closed properly
}

/**
 * @brief Restarts the system after a specified delay.
 * 
 * This function initiates a countdown before restarting the device. 
 * During the countdown, it prints the remaining time and a series of '#' 
 * characters for visibility. It also resets the watchdog timer to prevent 
 * the system from resetting prematurely.
 * 
 * @param delayTime Time in milliseconds to wait before restarting the device.
 */
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
/**
 * @brief Restarts the system after a specified delay.
 * 
 * This function initiates a countdown before restarting the device. 
 * During the countdown, it prints the remaining time and a series of '#' 
 * characters for visibility. It also resets the watchdog timer to prevent 
 * the system from resetting prematurely.
 * 
 * @param delayTime Time in milliseconds to wait before restarting the device.
 */
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

/**
 * @brief Restarts the system after a specified delay.
 * 
 * This function initiates a countdown before restarting the device. 
 * During the countdown, it prints the remaining time and a series of '#' 
 * characters for visibility. It also resets the watchdog timer to prevent 
 * the system from resetting prematurely.
 * 
 * @param delayTime Time in milliseconds to wait before restarting the device.
 */
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

/**
 * @brief Simulates a power-down by putting the ESP32 into deep sleep.
 * 
 * This function enables the ESP32 to enter deep sleep mode for 1 second. 
 * It is used to simulate the power-down state of the device.
 */
void ConfigManager::simulatePowerDown() {
    // Put the ESP32 into deep sleep for 1 second (simulate power-down)
    esp_sleep_enable_timer_wakeup(1000000); // 1 second (in microseconds)
    esp_deep_sleep_start();  // Enter deep sleep
}

/**
 * @brief Opens the preferences in read-write mode.
 * 
 * This function initializes the Preferences library to allow 
 * writing configuration settings. It opens the specified 
 * configuration partition in read-write mode.
 */
void ConfigManager::startPreferencesReadWrite() {
    preferences->begin(CONFIG_PARTITION, false);  // false = read-write mode
    DEBUG_PRINTLN("Preferences opened in write mode.");

}

/**
 * @brief Opens the preferences in read-only mode.
 * 
 * This function initializes the Preferences library to allow 
 * reading configuration settings. It opens the specified 
 * configuration partition in read-only mode.
 */
void ConfigManager::startPreferencesRead() {
    preferences->begin(CONFIG_PARTITION, true);  // true = read-only mode
    DEBUG_PRINTLN("Preferences opened in read mode.");
}

/**
 * @brief Initializes the configuration manager and preferences.
 * 
 * This function sets up the ConfigManager by checking the reset flag
 * and initializing various settings and configurations if necessary. 
 * It uses the Preferences library to store configuration data and
 * ensures that the system can either reset to default settings or
 * use existing configurations.
 */
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


/**
 * @brief Retrieves the reset flag from preferences.
 * 
 * This function checks and returns the value of the "Reset" flag from 
 * the preferences. If the flag is not set, it defaults to true. The 
 * function also resets the watchdog timer to prevent the system from 
 * resetting unexpectedly.
 * 
 * @return bool The value of the reset flag.
 */
bool ConfigManager::getResetFlag() {
    esp_task_wdt_reset();
    bool value = preferences->getBool(RESET_FLAG, true); // Default to true if not set
    return value;
}

/**
 * @brief Closes the preferences object.
 * 
 * This function is responsible for closing the preferences object to 
 * ensure that any changes are saved and resources are freed. 
 * It should be called when no further preference operations are needed.
 */
void ConfigManager::end() {
    preferences->end();  // Close preferences
}


/**
 * @brief Initializes all needed default variables.
 * 
 * This function is called to initialize all necessary default variables 
 * for the ConfigManager, ensuring that all values are set to a known 
 * state before use.
 */
void ConfigManager::initializeDefaults() {
    initializeVariables();  // Initialize all default variables
}

/**
 * @brief Initializes all default variables.
 * 
 * This function sets the initial values for various boolean and string 
 * variables used by the ConfigManager. It includes settings for GPIO, 
 * Wi-Fi SSID, and password. Debug messages are printed if DEBUGMODE is enabled.
 */
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



/**
 * @brief Gets a boolean value from preferences.
 * 
 * This function retrieves a boolean value associated with the given key 
 * from the preferences. If the key does not exist, it returns the specified 
 * default value. The function also resets the watchdog timer to prevent 
 * unexpected resets.
 * 
 * @param key The key associated with the boolean value.
 * @param defaultValue The default value to return if the key does not exist.
 * @return bool The retrieved boolean value or the default value.
 */
bool ConfigManager::GetBool(const char* key, bool defaultValue) {
    esp_task_wdt_reset();
    bool value = preferences->getBool(key, defaultValue);
    return value;
}

/**
 * @brief Gets an integer value from preferences.
 * 
 * This function retrieves an integer value associated with the given key 
 * from the preferences. If the key does not exist, it returns the specified 
 * default value. The function also resets the watchdog timer to prevent 
 * unexpected resets.
 * 
 * @param key The key associated with the integer value.
 * @param defaultValue The default value to return if the key does not exist.
 * @return int The retrieved integer value or the default value.
 */
int ConfigManager::GetInt(const char* key, int defaultValue) {
    esp_task_wdt_reset();
    int value = preferences->getInt(key, defaultValue);
    return value;
}
/**
 * @brief Gets an integer value from preferences.
 * 
 * This function retrieves an integer value associated with the given key 
 * from the preferences. If the key does not exist, it returns the specified 
 * default value. The function also resets the watchdog timer to prevent 
 * unexpected resets.
 * 
 * @param key The key associated with the integer value.
 * @param defaultValue The default value to return if the key does not exist.
 * @return int The retrieved integer value or the default value.
 */
uint64_t ConfigManager::GetULong64(const char* key, int defaultValue) {
    esp_task_wdt_reset();
    uint64_t value = preferences->getULong64(key, defaultValue);
    return value;
}

/**
 * @brief Gets a float value from preferences.
 * 
 * This function retrieves a float value associated with the given key 
 * from the preferences. If the key does not exist, it returns the specified 
 * default value. The function also resets the watchdog timer to prevent 
 * unexpected resets.
 * 
 * @param key The key associated with the float value.
 * @param defaultValue The default value to return if the key does not exist.
 * @return float The retrieved float value or the default value.
 */
float ConfigManager::GetFloat(const char* key, float defaultValue) {
    esp_task_wdt_reset();
    float value = preferences->getFloat(key, defaultValue);
    return value;
}

/**
 * @brief Gets a string value from preferences.
 * 
 * This function retrieves a string value associated with the given key 
 * from the preferences. If the key does not exist, it returns the specified 
 * default value. The function also resets the watchdog timer to prevent 
 * unexpected resets.
 * 
 * @param key The key associated with the string value.
 * @param defaultValue The default value to return if the key does not exist.
 * @return String The retrieved string value or the default value.
 */
String ConfigManager::GetString(const char* key, const String& defaultValue) {
    esp_task_wdt_reset();
    String value = preferences->getString(key, defaultValue);
    return value;
}

/**
 * @brief Puts a boolean value into preferences.
 * 
 * This function stores a boolean value associated with the given key 
 * in the preferences. It also resets the watchdog timer to prevent 
 * unexpected resets.
 * 
 * @param key The key to associate with the boolean value.
 * @param value The boolean value to store.
 */
void ConfigManager::PutBool(const char* key, bool value) {
    esp_task_wdt_reset();
    RemoveKey(key);
    preferences->putBool(key, value);  // Store the new value
}

/**
 * @brief Puts an unsigned integer value into preferences.
 * 
 * This function stores an unsigned integer value associated with the given 
 * key in the preferences. It also resets the watchdog timer to prevent 
 * unexpected resets.
 * 
 * @param key The key to associate with the unsigned integer value.
 * @param value The unsigned integer value to store.
 */
void ConfigManager::PutUInt(const char* key, int value) {
    esp_task_wdt_reset();
    RemoveKey(key);
    preferences->putUInt(key, value);  // Store the new value
}

/**
 * @brief Puts an unsigned integer value into preferences.
 * 
 * This function stores an unsigned integer value associated with the given 
 * key in the preferences. It also resets the watchdog timer to prevent 
 * unexpected resets.
 * 
 * @param key The key to associate with the unsigned integer value.
 * @param value The unsigned integer value to store.
 */
void ConfigManager::PutULong64(const char* key, int value) {
    esp_task_wdt_reset();
    RemoveKey(key);
    preferences->putULong64(key, value);  // Store the new value
}

/**
 * @brief Puts an integer value into preferences.
 * 
 * This function stores an integer value associated with the given key 
 * in the preferences. It also resets the watchdog timer to prevent 
 * unexpected resets.
 * 
 * @param key The key to associate with the integer value.
 * @param value The integer value to store.
 */
void ConfigManager::PutInt(const char* key, int value) {
    esp_task_wdt_reset();
    RemoveKey(key);
    preferences->putInt(key, value);  // Store the new value
}

/**
 * @brief Puts a float value into preferences.
 * 
 * This function stores a float value associated with the given key 
 * in the preferences. It also resets the watchdog timer to prevent 
 * unexpected resets.
 * 
 * @param key The key to associate with the float value.
 * @param value The float value to store.
 */
void ConfigManager::PutFloat(const char* key, float value) {
    esp_task_wdt_reset();
    RemoveKey(key);
    preferences->putFloat(key, value);  // Store the new value
}

/**
 * @brief Puts a string value into preferences.
 * 
 * This function stores a string value associated with the given key 
 * in the preferences. It also resets the watchdog timer to prevent 
 * unexpected resets.
 * 
 * @param key The key to associate with the string value.
 * @param value The string value to store.
 */
void ConfigManager::PutString(const char* key, const String& value) {
    esp_task_wdt_reset();
    RemoveKey(key);
    preferences->putString(key, value);  // Store the new value
}

/**
 * @brief Clears all stored preferences.
 * 
 * This function removes all key-value pairs from the preferences 
 * storage.
 */
void ConfigManager::ClearKey() {
    preferences->clear();
}

/**
 * @brief Removes a specific key from the preferences.
 * 
 * This function checks if the specified key exists in the 
 * preferences and removes it if it does. If the key is not found, 
 * it logs a message if debugging is enabled.
 * 
 * @param key The key to remove from the preferences.
 */
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


