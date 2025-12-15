#include "services/NVSManager.h"

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

// Prefer a custom burned MAC if present, else the default eFuse MAC.
static void get_efuse_mac(uint8_t mac[6]) {
    if (esp_efuse_mac_get_custom(mac) == ESP_OK) return;
    // fall back to default base MAC; this lives in eFuse BLK0
    ESP_ERROR_CHECK(esp_efuse_mac_get_default(mac));
}

// Crockford Base32 (no I, L, O, U) â€“ compact & human-friendly.
// 48 bits (6 bytes) -> 10 characters.
static String base32_crockford(const uint8_t* data, size_t len) {
    static const char* ALPH = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
    uint32_t buffer = 0;
    int bits = 0;
    String out;
    out.reserve((len * 8 + 4) / 5);

    for (size_t i = 0; i < len; ++i) {
        buffer = (buffer << 8) | data[i];
        bits += 8;
        while (bits >= 5) {
            int idx = (buffer >> (bits - 5)) & 0x1F;
            out += ALPH[idx];
            bits -= 5;
        }
    }
    if (bits > 0) {
        int idx = (buffer << (5 - bits)) & 0x1F;
        out += ALPH[idx];
    }
    return out;
}

// Format last 3 bytes as HEX for SSID suffix (e.g. "1A2B3C")
static String hex_suffix_last3(const uint8_t mac[6]) {
    char sfx[7];
    snprintf(sfx, sizeof(sfx), "%02X%02X%02X", mac[3], mac[4], mac[5]);
    return String(sfx);
}

// Build a deterministic Device ID: e.g. "PDB-9R2KJ-8TF3Z"
static String make_device_id_from_efuse() {
    uint8_t mac[6];
    get_efuse_mac(mac);
    String b32 = base32_crockford(mac, 6); // 10 chars
    // group as 5-5 for readability
    return String("PDB-") + b32.substring(0,5) + "-" + b32.substring(5,10);
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
// - If weâ€™re RO and need RW, we reopen RW
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
    DEBUG_PRINTLN("#                 Starting NVS Manager                  #");
    DEBUG_PRINTLN("###########################################################");
    DEBUGGSTOP();
    lock_();
    ensureOpenRO_();
    bool resetFlag = preferences.getBool(RESET_FLAG, true);
    unlock_();

    if (resetFlag) {
        DEBUG_PRINTLN("[NVS] Initializing the device... ");
        initializeDefaults();
        RestartSysDelay(10000);
    } else {
        DEBUG_PRINTLN("[NVS] Using existing configuration...");
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

  // Read eFuse MAC once (works before WiFi is started)
  uint8_t mac[6];
  get_efuse_mac(mac);

  // Deterministic SSID using last 3 bytes of eFuse MAC (e.g., "PDis_1A2B3C")
  String ssid = String(DEVICE_WIFI_HOTSPOT_NAME) + hex_suffix_last3(mac);
  PutString(DEVICE_WIFI_HOTSPOT_NAME_KEY, ssid);
  PutString(DEVICE_AP_AUTH_PASS_KEY, DEVICE_AP_AUTH_PASS_DEFAULT);

  // Station mode credentials
  PutString(STA_SSID_KEY, DEFAULT_STA_SSID);
  PutString(STA_PASS_KEY, DEFAULT_STA_PASS);

  // Admin/User login
  PutString(ADMIN_ID_KEY, DEFAULT_ADMIN_ID);
  PutString(ADMIN_PASS_KEY, DEFAULT_ADMIN_PASS);
  PutString(USER_ID_KEY, DEFAULT_USER_ID);
  PutString(USER_PASS_KEY, DEFAULT_USER_PASS);

  // Device identity & versions (persist once)
  // Deterministic, human-friendly ID derived from eFuse MAC
  String devId = make_device_id_from_efuse();   // e.g. "PDB-9R2KJ-8TF3Z"
  PutString(DEV_ID_KEY, devId);
  PutString(DEV_SW_KEY, DEVICE_SW_VERSION);
  PutString(DEV_HW_KEY, DEVICE_HW_VERSION);

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
  PutFloat(CP_EMP_GAIN_KEY, DEFAULT_CAP_EMP_GAIN);
  PutFloat(CAP_BANK_CAP_F_KEY, DEFAULT_CAP_BANK_CAP_F);
  PutBool (COOLING_PROFILE_KEY, DEFAULT_COOLING_PROFILE_FAST);
  PutInt  (LOOP_MODE_KEY, DEFAULT_LOOP_MODE);

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

  // Temperature sensor count & idle current
  PutInt(TEMP_SENSOR_COUNT_KEY, DEFAULT_TEMP_SENSOR_COUNT);
  PutFloat(IDLE_CURR_KEY, DEFAULT_IDLE_CURR);

  // --- Buzzer configuration ---
  PutBool(BUZLOW_KEY, BUZLOW_DEFAULT);
  PutBool(BUZMUT_KEY, BUZMUT_DEFAULT);

  // --- Nichrome wire resistances (Ohms, default) ---
  PutFloat(R01OHM_KEY, DEFAULT_WIRE_RES_OHMS);
  PutFloat(R02OHM_KEY, DEFAULT_WIRE_RES_OHMS);
  PutFloat(R03OHM_KEY, DEFAULT_WIRE_RES_OHMS);
  PutFloat(R04OHM_KEY, DEFAULT_WIRE_RES_OHMS);
  PutFloat(R05OHM_KEY, DEFAULT_WIRE_RES_OHMS);
  PutFloat(R06OHM_KEY, DEFAULT_WIRE_RES_OHMS);
  PutFloat(R07OHM_KEY, DEFAULT_WIRE_RES_OHMS);
  PutFloat(R08OHM_KEY, DEFAULT_WIRE_RES_OHMS);
  PutFloat(R09OHM_KEY, DEFAULT_WIRE_RES_OHMS);
  PutFloat(R10OHM_KEY, DEFAULT_WIRE_RES_OHMS);

  // --- Target resistance + wire ohm/m ---
  PutFloat(R0XTGT_KEY,         DEFAULT_TARG_RES_OHMS);
  PutFloat(WIRE_OHM_PER_M_KEY, DEFAULT_WIRE_OHM_PER_M);
  PutInt  (WIRE_GAUGE_KEY,     DEFAULT_WIRE_GAUGE);

  // --- Power tracker persistent statistics ---
  PutFloat(PT_KEY_TOTAL_ENERGY_WH,     PT_DEF_TOTAL_ENERGY_WH);
  PutInt  (PT_KEY_TOTAL_SESSIONS,      PT_DEF_TOTAL_SESSIONS);
  PutInt  (PT_KEY_TOTAL_SESSIONS_OK,   PT_DEF_TOTAL_SESSIONS_OK);

  PutFloat(PT_KEY_LAST_SESS_ENERGY_WH, PT_DEF_LAST_SESS_ENERGY_WH);
  PutInt  (PT_KEY_LAST_SESS_DURATION_S,PT_DEF_LAST_SESS_DURATION_S);
  PutFloat(PT_KEY_LAST_SESS_PEAK_W,    PT_DEF_LAST_SESS_PEAK_W);
  PutFloat(PT_KEY_LAST_SESS_PEAK_A,    PT_DEF_LAST_SESS_PEAK_A);

  PutString(TSB0ID_KEY, "");
  PutString(TSB1ID_KEY, "");
  PutString(TSHSID_KEY, "");
  PutBool  (TSMAP_KEY, false);

  // Cooling profile tuning
  PutFloat(COOL_BURIED_SCALE_KEY, DEFAULT_COOLING_SCALE_BURIED);
  PutFloat(COOL_KCOLD_KEY,        DEFAULT_COOL_K_COLD);
  PutFloat(COOL_DROP_MAX_KEY,     DEFAULT_MAX_COOL_DROP_C);
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
        DEBUG_PRINT("#");
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
        DEBUG_PRINT("#");
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
