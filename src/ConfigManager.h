#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H
/**
 * @file ConfigManager.h
 * @brief Configuration Manager for ESP32
 * 
 * The `ConfigManager` class provides a structured way to manage application 
 * configurations using the ESP32 Preferences library. It allows for saving 
 * and retrieving various data types, including boolean, integer, float, and 
 * string values, to persistent storage. The class is designed to simplify 
 * configuration management, enabling easy access and modification of settings 
 * across system restarts.
 * 
 * Key features include:
 * - Initialization and termination of configuration access.
 * - Methods for storing and retrieving configuration values.
 * - System control methods to restart the system or simulate power down for testing.
 * - Utility functions to manage application flags and reset conditions.
 * 
 * This class is especially useful in applications where persistent configuration 
 * data is necessary, such as in IoT devices that require configuration 
 * management across power cycles.
 */

 #include <Arduino.h>
 #include <Preferences.h>
  // ESP32-specific includes
 #include <esp_task_wdt.h>
 #include <esp_sleep.h>
 #include <driver/rtc_io.h>
 
 // Custom includes
 #include "Config.h"  // Include Config.h for default values
 

class ConfigManager {
public:
    ConfigManager(Preferences* preferences);
    ~ConfigManager();

    void begin();  // Initialize the configuration
    void end();    // End access to preferences

   
    void PutBool(const char* key, bool value);      // Save a boolean value
    void PutInt(const char* key, int value);        // Save an integer value
    void PutFloat(const char* key, float value);    // Save a float value
    void PutString(const char* key, const String& value);  // Save a string value
    void PutUInt(const char* key, int value);       // Save an unsigned integer value
    void PutULong64(const char* key, int value);       // Save an unsigned integer value


    bool GetBool(const char* key, bool defaultValue);    // Retrieve a boolean value
    int GetInt(const char* key, int defaultValue);       // Retrieve an integer value
    uint64_t GetULong64(const char* key, int defaultValue);       // Retrieve an UIntinteger value
    float GetFloat(const char* key, float defaultValue); // Retrieve a float value
    String GetString(const char* key, const String& defaultValue);  // Retrieve a string value

    void RemoveKey(const char* key);  // Remove a specific key
    void ClearKey(); 


    // System control methods
    void RestartSysDelay(unsigned long delayTime);  // Restart system with delay
    void RestartSysDelayDown(unsigned long delayTime);  // Restart system with delay
    void simulatePowerDown();  // Simulate power down for testing
    void CountdownDelay(unsigned long delayTime);

    // Preferences management
    void startPreferencesReadWrite();  // Open preferences in read-write mode
    void startPreferencesRead();       // Open preferences in read-only mode

private:
    // Private utility methods for internal use only
    void initializeDefaults();   // Initialize default values
    void initializeVariables();  // Initialize internal variables
    bool getResetFlag();         // Get system reset flag

    Preferences* preferences;     // Preferences object to store configuration
    const char* namespaceName;   // Namespace for the preferences storage
};

#endif // CONFIG_MANAGER_H