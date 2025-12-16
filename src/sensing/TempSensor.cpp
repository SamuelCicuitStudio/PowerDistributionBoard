#include "sensing/TempSensor.h"
#include "system/Utils.h"  // DEBUG_* macros

// ============================ Public API ============================

void TempSensor::begin() {
    DEBUGGSTART();
    DEBUG_PRINTLN("###########################################################");
    DEBUG_PRINTLN("#               Starting Temperature Manager ðŸŒ¡ï¸          #");
    DEBUG_PRINTLN("###########################################################");
    DEBUGGSTOP();

    if (!CONF || !ow) {
        DEBUG_PRINTLN("[TempSensor] Missing dependencies âŒ");
        return;
    }

    // Create mutex first
    if (_mutex == nullptr) {
        _mutex = xSemaphoreCreateMutex();
        if (!_mutex) {
            DEBUG_PRINTLN("[TempSensor] Failed to create mutex âŒ");
            return;
        }
    }

    discoverSensors();

    if (sensorCount == 0) {
        DEBUG_PRINTLN("[TempSensor] No sensors found âŒ");
        return;
    }

    CONF->PutInt(TEMP_SENSOR_COUNT_KEY, sensorCount);
    DEBUG_PRINTF("[TempSensor] %u sensor(s) found âœ…\n", sensorCount);

    // Initialize cache as invalid
    if (lock()) {
        for (uint8_t i = 0; i < sensorCount; ++i) {
            lastTempsC[i] = NAN;
            lastValid[i]  = false;
        }
        unlock();
    }

    // Identify roles and persist/update IDs as needed
    identifyAndPersistSensors();

    // Start periodic background sampling (5s default)
    startTemperatureTask(TEMP_SENSOR_UPDATE_INTERVAL_MS);
}

void TempSensor::requestTemperatures() {
    if (!ow || sensorCount == 0) return;

    if (!lock(pdMS_TO_TICKS(50))) {
        DEBUG_PRINTLN("[TempSensor] requestTemperatures(): lock timeout âŒ");
        return;
    }
    ow->reset();
    ow->write(0xCC); // SKIP ROM
    ow->write(0x44); // CONVERT T
    unlock();
}

float TempSensor::getTemperature(uint8_t index) {
    if (index >= sensorCount) return NAN;

    float temp = NAN;
    bool  valid = false;

    if (lock(pdMS_TO_TICKS(10))) {
        temp  = lastTempsC[index];
        valid = lastValid[index];
        unlock();
    } else {
        // Best-effort read if briefly contended
        temp  = lastTempsC[index];
        valid = lastValid[index];
    }
    return valid ? temp : NAN;
}

uint8_t TempSensor::getSensorCount() {
    if (sensorCount > 0) return sensorCount;
    if (CONF) return (uint8_t)CONF->GetInt(TEMP_SENSOR_COUNT_KEY, 0);
    return 0;
}

void TempSensor::printAddress(uint8_t address[8]) {
    for (uint8_t i = 0; i < 8; i++) {
        if (address[i] < 0x10) Serial.print("0");
        Serial.print(address[i], HEX);
        if (i < 7) Serial.print(":");
    }
    Serial.println();
}

void TempSensor::stopTemperatureTask() {
    if (tempTaskHandle != nullptr) {
        vTaskDelete(tempTaskHandle);
        tempTaskHandle = nullptr;
    }
}

void TempSensor::startTemperatureTask(uint32_t intervalMs) {
    stopTemperatureTask();
    if (intervalMs == 0) intervalMs = TEMP_SENSOR_UPDATE_INTERVAL_MS;

    if (lock()) {
        updateIntervalMs = intervalMs;
        unlock();
    }

    BaseType_t ok = xTaskCreate(
        TempSensor::temperatureTask,
        "TempUpdateTask",
        TEMP_SENSOR_TASK_STACK_SIZE,
        this,
        TEMP_SENSOR_TASK_PRIORITY,
        &tempTaskHandle
    );

    if (ok != pdPASS) {
        tempTaskHandle = nullptr;
        DEBUG_PRINTLN("[TempSensor] Failed to start TempUpdateTask âŒ");
    } else {
        DEBUG_PRINTF("[TempSensor] TempUpdateTask started (interval=%lums) âœ…\n",
                     (unsigned long)intervalMs);
    }
}

// ========================= Internal helpers =========================

void TempSensor::discoverSensors() {
    if (!ow) return;

    if (!lock()) {
        DEBUG_PRINTLN("[TempSensor] discoverSensors(): lock failed âŒ");
        return;
    }

    sensorCount = 0;
    ow->reset_search();

    while (sensorCount < MAX_TEMP_SENSORS &&
           ow->search(sensorAddresses[sensorCount]))
    {
        DEBUG_PRINTF("[TempSensor] Found sensor %u: ", sensorCount);
        printAddress(sensorAddresses[sensorCount]);
        sensorCount++;
    }

    unlock();
}

// Read all sensors once AFTER a Convert T has completed (task context).
void TempSensor::updateAllTemperaturesBlocking() {
    if (!ow || sensorCount == 0) return;

    if (!lock()) {
        DEBUG_PRINTLN("[TempSensor] updateAllTemperaturesBlocking(): lock failed âŒ");
        return;
    }

    for (uint8_t i = 0; i < sensorCount; ++i) {
        ow->reset();
        ow->select(sensorAddresses[i]);
        ow->write(0xBE); // READ SCRATCHPAD
        ow->read_bytes(scratchpad, 9);

        // Optional CRC check (disabled to keep it lean)
        // if (OneWire::crc8(scratchpad, 8) != scratchpad[8]) { lastValid[i] = false; continue; }

        int16_t raw = (int16_t)((scratchpad[1] << 8) | scratchpad[0]);
        float tempC = (float)raw / 16.0f;

        lastTempsC[i] = tempC;
        lastValid[i]  = true;
    }

    unlock();
}

// ======================= Background RTOS Task =======================

void TempSensor::temperatureTask(void* param) {
    auto* self = static_cast<TempSensor*>(param);
    const TickType_t convertWaitTicks = pdMS_TO_TICKS(750); // 12-bit

    for (;;) {
        self->requestTemperatures();
        vTaskDelay(convertWaitTicks);
        self->updateAllTemperaturesBlocking();

        uint32_t intervalMs = TEMP_SENSOR_UPDATE_INTERVAL_MS;
        if (self->lock(pdMS_TO_TICKS(10))) {
            intervalMs = self->updateIntervalMs;
            if (intervalMs < 1000) intervalMs = 1000;
            self->unlock();
        }
        int32_t remainMs = (int32_t)intervalMs - 750;
        if (remainMs < 100) remainMs = 100;
        vTaskDelay(pdMS_TO_TICKS((uint32_t)remainMs));
    }
}

// ===================== ROM helpers & role mapping ===================

String TempSensor::addrToHex(const uint8_t a[8]) {
    char buf[17];
    for (int i = 0; i < 8; ++i) sprintf(&buf[i*2], "%02X", a[i]);
    buf[16] = 0;
    return String(buf);
}
bool TempSensor::hexToAddr(const String& hex, uint8_t out[8]) {
    if (hex.length() != 16) return false;
    for (int i = 0; i < 8; ++i) {
        char hi = hex.c_str()[2*i], lo = hex.c_str()[2*i+1];
        auto nyb = [](char c)->int {
            if (c>='0'&&c<='9') return c-'0';
            c &= ~0x20;
            if (c>='A'&&c<='F') return 10 + (c-'A');
            return -1;
        };
        int h = nyb(hi), l = nyb(lo);
        if (h<0 || l<0) return false;
        out[i] = (uint8_t)((h<<4)|l);
    }
    return true;
}
bool TempSensor::equalAddr(const uint8_t a[8], const uint8_t b[8]) {
    for (int i = 0; i < 8; ++i) if (a[i] != b[i]) return false;
    return true;
}
int TempSensor::findIndexByAddr(const uint8_t addr[8]) const {
    for (uint8_t i = 0; i < sensorCount; ++i) {
        if (equalAddr(addr, sensorAddresses[i])) return (int)i;
    }
    return -1;
}

void TempSensor::identifyAndPersistSensors() {
    // Snapshot discovered ROMs as hex strings
    String roms[MAX_TEMP_SENSORS];
    if (lock()) {
        for (uint8_t i = 0; i < sensorCount; ++i) {
            roms[i] = addrToHex(sensorAddresses[i]);
        }
        unlock();
    }

    // Load persisted IDs (may be empty on first boot)
    String b0 = CONF->GetString(TSB0ID_KEY, "");
    String b1 = CONF->GetString(TSB1ID_KEY, "");
    String hs = CONF->GetString(TSHSID_KEY, "");

    // Presence helper
    auto present = [&](const String& hex)->bool {
        if (hex.length() != 16) return false;
        for (uint8_t i=0;i<sensorCount;++i) if (roms[i] == hex) return true;
        return false;
    };

    // Sort detected ROMs for deterministic selection
    String sorted[MAX_TEMP_SENSORS];
    uint8_t n = sensorCount;
    for (uint8_t i = 0; i < n; ++i) sorted[i] = roms[i];
    for (uint8_t i = 0; i + 1 < n; ++i) {
        for (uint8_t j = i + 1; j < n; ++j) {
            if (sorted[j] < sorted[i]) std::swap(sorted[i], sorted[j]);
        }
    }

    // Re-learn board sensors whenever stored IDs are missing or unset
    bool boardMismatch =
        (b0.length() != 16 || b1.length() != 16) ||
        (b0.length() == 16 && !present(b0)) ||
        (b1.length() == 16 && !present(b1));

    if (boardMismatch && n >= 2) {
        // Pick first two distinct ROMs (fallback to duplicates if only two identical)
        String newB0 = sorted[0];
        String newB1 = sorted[0];
        for (uint8_t i = 1; i < n; ++i) {
            if (sorted[i] != newB0 || i == 1) {
                newB1 = sorted[i];
                break;
            }
        }
        CONF->PutString(TSB0ID_KEY, newB0);
        CONF->PutString(TSB1ID_KEY, newB1);
        b0 = newB0;
        b1 = newB1;
        DEBUG_PRINTLN("[TempSensor] Re-learned Board0/Board1 IDs.");
    }

    // Persist heatsink: first ROM that is neither board sensor. Clear if stale.
    String newHs = "";
    for (uint8_t i = 0; i < n; ++i) {
        if (sorted[i] != b0 && sorted[i] != b1) {
            newHs = sorted[i];
            break;
        }
    }
    if (newHs.length() == 16 && newHs != hs) {
        CONF->PutString(TSHSID_KEY, newHs);
        hs = newHs;
        DEBUG_PRINTLN("[TempSensor] Updated Heatsink ID.");
    } else if (hs.length() == 16 && !present(hs)) {
        CONF->PutString(TSHSID_KEY, "");
        hs = "";
        DEBUG_PRINTLN("[TempSensor] Cleared stale Heatsink ID.");
    }

    // Map roles to current indices
    map_.board0 = -1; map_.board1 = -1; map_.heatsink = -1;
    if (b0.length()==16) { uint8_t a[8]; if (hexToAddr(b0,a)) map_.board0 = findIndexByAddr(a); }
    if (b1.length()==16) { uint8_t a[8]; if (hexToAddr(b1,a)) map_.board1 = findIndexByAddr(a); }
    if (hs.length()==16) { uint8_t a[8]; if (hexToAddr(hs,a)) map_.heatsink = findIndexByAddr(a); }

    DEBUG_PRINTF("[TempSensor] Map -> B0:%d  B1:%d  HS:%d\n", map_.board0, map_.board1, map_.heatsink);
}
int TempSensor::indexForRole(TempRole role) const {
    switch (role) {
        case TempRole::Board0:   return map_.board0;
        case TempRole::Board1:   return map_.board1;
        case TempRole::Heatsink: return map_.heatsink;
        default: return -1;
    }
}
float TempSensor::getHeatsinkTemp() {
    int idx = indexForRole(TempRole::Heatsink);
    return (idx >= 0) ? getTemperature((uint8_t)idx) : NAN;
}
float TempSensor::getBoardTemp(uint8_t which) {
    int idx = (which == 0) ? map_.board0 : map_.board1;
    return (idx >= 0) ? getTemperature((uint8_t)idx) : NAN;
}
String TempSensor::getLabelForIndex(uint8_t index) {
    if (index == map_.board0)   return "Board0";
    if (index == map_.board1)   return "Board1";
    if (index == map_.heatsink) return "Heatsink";
    return "Unknown";
}
