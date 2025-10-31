#include "NVSManager.h"

// ======================================================
// Static singleton pointer
// ======================================================
NVS* NVS::s_instance = nullptr;


// ======================================================
// Singleton Init() and Get()
// ======================================================
void NVS::Init() {
    // Just force construction so caller doesn't have to think about it.
    (void)NVS::Get();
}

NVS* NVS::Get() {
    if (!s_instance) {
        s_instance = new NVS();
    }
    return s_instance;
}


// ======================================================
// ctor / dtor
// ======================================================
NVS::NVS()
: namespaceName(CONFIG_PARTITION) {
    mutex_ = xSemaphoreCreateRecursiveMutex();
}

NVS::~NVS() {
    end();
    if (mutex_) {
        vSemaphoreDelete(mutex_);
        mutex_ = nullptr;
    }
}


// ======================================================
// small RTOS-friendly sleep helper
// ======================================================
inline void NVS::sleepMs_(uint32_t ms) {
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
        vTaskDelay(pdMS_TO_TICKS(ms));
    } else {
        delay(ms);
    }
}


// ======================================================
// locking helpers
// ======================================================
inline void NVS::lock_()   { if (mutex_) xSemaphoreTakeRecursive(mutex_, portMAX_DELAY); }
inline void NVS::unlock_() { if (mutex_) xSemaphoreGiveRecursive(mutex_); }


// ======================================================
// Preferences open state helpers
// - Lazy open RO or RW
// - If we’re RO and need RW, we reopen RW
// ======================================================
void NVS::ensureOpenRO_() {
    if (!is_open_) {
        preferences.begin(namespaceName, /*readOnly=*/true);
        is_open_ = true;
        open_rw_ = false;
    } else if (open_rw_) {
        // already RW -> fine
    }
}

void NVS::ensureOpenRW_() {
    if (!is_open_) {
        preferences.begin(namespaceName, /*readOnly=*/false);
        is_open_ = true;
        open_rw_ = true;
    } else if (!open_rw_) {
        // currently RO, need to reopen RW
        preferences.end();
        preferences.begin(namespaceName, /*readOnly=*/false);
        is_open_ = true;
        open_rw_ = true;
    }
}

void NVS::startPreferencesReadWrite() {
    lock_();
    ensureOpenRW_();
    DEBUG_PRINTLN("Preferences opened RW");
    unlock_();
}

void NVS::startPreferencesRead() {
    lock_();
    ensureOpenRO_();
    DEBUG_PRINTLN("Preferences opened RO");
    unlock_();
}


// ======================================================
// end() - close preferences
// ======================================================
void NVS::end() {
    lock_();
    if (is_open_) {
        preferences.end();
        is_open_ = false;
        open_rw_ = false;
    }
    unlock_();
}


// ======================================================
// begin()
// - decides first boot vs existing config
// - on first boot we write defaults and reboot
// (same logic you already had in ConfigManager, now under NVS) :contentReference[oaicite:2]{index=2} :contentReference[oaicite:3]{index=3}
/*
   Usage at startup:
       NVS::Init();
       NVS::Get()->begin();
*/
void NVS::begin() {
    DEBUGGSTART() ;
    DEBUG_PRINTLN("###########################################################");
    DEBUG_PRINTLN("#                 Starting NVS Manager ⚙️                 #");
    DEBUG_PRINTLN("###########################################################");
    DEBUGGSTOP();
    lock_();
    ensureOpenRO_();
    bool resetFlag = preferences.getBool(RESET_FLAG, true);
    unlock_();

    if (resetFlag) {
        DEBUG_PRINTLN("[NVS] Initializing the device... 🔄");
        initializeDefaults();
        RestartSysDelay(10000);
    } else {
        DEBUG_PRINTLN("[NVS] Using existing configuration... ✅");
    }
}


// ======================================================
// Core utils
// ======================================================
bool NVS::getResetFlag() {
    esp_task_wdt_reset();
    lock_();
    ensureOpenRO_();
    bool v = preferences.getBool(RESET_FLAG, true);
    unlock_();
    return v;
}

void NVS::initializeDefaults() {
    initializeVariables();
}

// All default keys at first boot.
// - identity / pairing
// - runtime state
// - lock config (electromagnet vs screw, timeout)
// - hardware presence map
void NVS::initializeVariables() { 
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

  // New: Station mode credentials
  PutString(STA_SSID_KEY, DEFAULT_STA_SSID);
  PutString(STA_PASS_KEY, DEFAULT_STA_PASS);

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

  // --- Buzzer configuration ---
  PutBool(BUZLOW_KEY, BUZLOW_DEFAULT);   // Active-low logic (default false)
  PutBool(BUZMUT_KEY, BUZMUT_DEFAULT);   // Muted state (default false)
}


// ======================================================
// Reads (auto-open RO)
// ======================================================
bool NVS::GetBool(const char* key, bool defaultValue) {
    esp_task_wdt_reset();
    lock_();
    ensureOpenRO_();
    bool v = preferences.getBool(key, defaultValue);
    unlock_();
    return v;
}

int NVS::GetInt(const char* key, int defaultValue) {
    esp_task_wdt_reset();
    lock_();
    ensureOpenRO_();
    int v = preferences.getInt(key, defaultValue);
    unlock_();
    return v;
}

uint64_t NVS::GetULong64(const char* key, int defaultValue) {
    esp_task_wdt_reset();
    lock_();
    ensureOpenRO_();
    uint64_t v = preferences.getULong64(key, defaultValue);
    unlock_();
    return v;
}

float NVS::GetFloat(const char* key, float defaultValue) {
    esp_task_wdt_reset();
    lock_();
    ensureOpenRO_();
    float v = preferences.getFloat(key, defaultValue);
    unlock_();
    return v;
}

String NVS::GetString(const char* key, const String& defaultValue) {
    esp_task_wdt_reset();
    lock_();
    ensureOpenRO_();
    String v = preferences.getString(key, defaultValue);
    unlock_();
    return v;
}


// ======================================================
// Writes (auto-open RW)
// (We remove existing key first to guarantee type)
// ======================================================
void NVS::PutBool(const char* key, bool value) {
    esp_task_wdt_reset();
    lock_();
    ensureOpenRW_();
    if (preferences.isKey(key)) preferences.remove(key);
    preferences.putBool(key, value);
    unlock_();
}

void NVS::PutUInt(const char* key, int value) {
    esp_task_wdt_reset();
    lock_();
    ensureOpenRW_();
    if (preferences.isKey(key)) preferences.remove(key);
    preferences.putUInt(key, value);
    unlock_();
}

void NVS::PutULong64(const char* key, int value) {
    esp_task_wdt_reset();
    lock_();
    ensureOpenRW_();
    if (preferences.isKey(key)) preferences.remove(key);
    preferences.putULong64(key, value);
    unlock_();
}

void NVS::PutInt(const char* key, int value) {
    esp_task_wdt_reset();
    lock_();
    ensureOpenRW_();
    if (preferences.isKey(key)) preferences.remove(key);
    preferences.putInt(key, value);
    unlock_();
}

void NVS::PutFloat(const char* key, float value) {
    esp_task_wdt_reset();
    lock_();
    ensureOpenRW_();
    if (preferences.isKey(key)) preferences.remove(key);
    preferences.putFloat(key, value);
    unlock_();
}

void NVS::PutString(const char* key, const String& value) {
    esp_task_wdt_reset();
    lock_();
    ensureOpenRW_();
    if (preferences.isKey(key)) preferences.remove(key);
    preferences.putString(key, value);
    unlock_();
}


// ======================================================
// Key management
// ======================================================
void NVS::ClearKey() {
    lock_();
    ensureOpenRW_();
    preferences.clear();
    unlock_();
}

void NVS::RemoveKey(const char* key) {
    esp_task_wdt_reset();
    lock_();
    ensureOpenRW_();
    if (preferences.isKey(key)) {
        preferences.remove(key);
    } else {
        DEBUG_PRINT("[NVS] Key not found, skipping: ");
        DEBUG_PRINTLN(key);
    }
    unlock_();
}


// ======================================================
// System helpers / reboot paths
// ======================================================
void NVS::RestartSysDelayDown(unsigned long delayTime) {
    unsigned long interval = delayTime / 30;
    DEBUGGSTART();
    DEBUG_PRINTLN("###########################################################");
    DEBUG_PRINTLN("#           Restarting the Device in: " + String(delayTime / 1000)+ " Sec              #");
    DEBUG_PRINTLN("###########################################################");
    DEBUGGSTOP() ;
    for (int i = 0; i < 30; i++) {
        DEBUG_PRINT("🔵");
        sleepMs_(interval);
        esp_task_wdt_reset();
    }
    DEBUG_PRINTLN();
    DEBUG_PRINTLN("[NVS] Restarting now...");
    DEBUGGSTOP() ;
    simulatePowerDown();
}

void NVS::RestartSysDelay(unsigned long delayTime) {
    unsigned long interval = delayTime / 30;
    DEBUGGSTART() ;
    DEBUG_PRINTLN("###########################################################");
    DEBUG_PRINTLN("#           Restarting the Device in: " + String(delayTime / 1000)+ " Sec              #");
    DEBUG_PRINTLN("###########################################################");
    DEBUGGSTOP() ;
    for (int i = 0; i < 30; i++) {
        DEBUG_PRINT("🔵");
        sleepMs_(interval);
        esp_task_wdt_reset();
    }
    DEBUG_PRINTLN();
    DEBUG_PRINTLN("[NVS] Restarting now...");
    
    ESP.restart();
}

void NVS::CountdownDelay(unsigned long delayTime) {
    unsigned long interval = delayTime / 32;
    DEBUGGSTART() ;
    DEBUG_PRINTLN("###########################################################");
    DEBUG_PRINT("[NVS] Waiting User Action: ");
    DEBUG_PRINT(delayTime / 1000);
    DEBUG_PRINTLN(" Sec");
    DEBUGGSTOP() ;
    for (int i = 0; i < 32; i++) {
        DEBUG_PRINT("#");
        sleepMs_(interval);
        esp_task_wdt_reset();
    }
    DEBUG_PRINTLN();
    
}

void NVS::simulatePowerDown() {
    esp_sleep_enable_timer_wakeup(1000000); // 1s
    esp_deep_sleep_start();
}
