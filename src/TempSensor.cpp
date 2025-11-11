#include "TempSensor.h"
#include "Utils.h"  // for DEBUG_* macros if you use them

// ============================ Public API ============================

void TempSensor::begin() {
    DEBUGGSTART();
    DEBUG_PRINTLN("###########################################################");
    DEBUG_PRINTLN("#               Starting Temperature Manager 🌡️          #");
    DEBUG_PRINTLN("###########################################################");
    DEBUGGSTOP();

    if (!CONF || !ow) {
        DEBUG_PRINTLN("[TempSensor] Missing dependencies ❌");
        return;
    }

    // Create mutex first
    if (_mutex == nullptr) {
        _mutex = xSemaphoreCreateMutex();
        if (!_mutex) {
            DEBUG_PRINTLN("[TempSensor] Failed to create mutex ❌");
            return;
        }
    }

    discoverSensors();

    if (sensorCount == 0) {
        DEBUG_PRINTLN("[TempSensor] No sensors found ❌");
        return;
    }

    CONF->PutInt(TEMP_SENSOR_COUNT_KEY, sensorCount);
    DEBUG_PRINTF("[TempSensor] %u sensor(s) found ✅\n", sensorCount);

    // Initialize cache as invalid
    if (lock()) {
        for (uint8_t i = 0; i < sensorCount; ++i) {
            lastTempsC[i] = NAN;
            lastValid[i]  = false;
        }
        unlock();
    }

    // Start periodic background sampling (5s default)
    startTemperatureTask(TEMP_SENSOR_UPDATE_INTERVAL_MS);
}

void TempSensor::requestTemperatures() {
    if (!ow || sensorCount == 0) return;

    // Broadcast "Convert T" to all devices (optimized vs per-sensor loop)
    if (!lock(pdMS_TO_TICKS(50))) {
        DEBUG_PRINTLN("[TempSensor] requestTemperatures(): lock timeout ❌");
        return;
    }

    ow->reset();
    ow->write(0xCC); // SKIP ROM - address all sensors
    ow->write(0x44); // CONVERT T

    unlock();
}

// NON-BLOCKING: returns last cached temperature.
// No OneWire, no I/O, just memory.
float TempSensor::getTemperature(uint8_t index) {
    if (index >= sensorCount) {
        return NAN;
    }

    float temp = NAN;
    bool  valid = false;

    if (lock(pdMS_TO_TICKS(10))) {
        temp  = lastTempsC[index];
        valid = lastValid[index];
        unlock();
    } else {
        // If mutex temporarily busy, read without lock as a best-effort.
        // (float+bool writes are small; risk is minimal and bounded)
        temp  = lastTempsC[index];
        valid = lastValid[index];
    }

    return valid ? temp : NAN;
}

uint8_t TempSensor::getSensorCount() {
    if (sensorCount > 0) {
        return sensorCount;
    }
    if (CONF) {
        return (uint8_t)CONF->GetInt(TEMP_SENSOR_COUNT_KEY, 0);
    }
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

// Start / restart periodic sampler task
void TempSensor::startTemperatureTask(uint32_t intervalMs) {
    // Kill old one if running
    stopTemperatureTask();

    if (intervalMs == 0) {
        intervalMs = TEMP_SENSOR_UPDATE_INTERVAL_MS;
    }

    if (lock()) {
        updateIntervalMs = intervalMs;
        unlock();
    }

    BaseType_t ok = xTaskCreatePinnedToCore(
        TempSensor::temperatureTask,
        "TempUpdateTask",
        TEMP_SENSOR_TASK_STACK_SIZE,
        this,
        TEMP_SENSOR_TASK_PRIORITY,
        &tempTaskHandle,
        TEMP_SENSOR_TASK_CORE
    );

    if (ok != pdPASS) {
        tempTaskHandle = nullptr;
        DEBUG_PRINTLN("[TempSensor] Failed to start TempUpdateTask ❌");
    } else {
        DEBUG_PRINTF("[TempSensor] TempUpdateTask started (interval=%lums) ✅\n",
                     (unsigned long)intervalMs);
    }
}

// ========================= Internal helpers =========================

void TempSensor::discoverSensors() {
    if (!ow) return;

    if (!lock()) {
        DEBUG_PRINTLN("[TempSensor] discoverSensors(): lock failed ❌");
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

// Read all sensors once AFTER a Convert T has completed.
// Called ONLY from background task.
void TempSensor::updateAllTemperaturesBlocking() {
    if (!ow || sensorCount == 0) return;

    if (!lock()) {
        DEBUG_PRINTLN("[TempSensor] updateAllTemperaturesBlocking(): lock failed ❌");
        return;
    }

    for (uint8_t i = 0; i < sensorCount; ++i) {
        // Read scratchpad for this sensor
        ow->reset();
        ow->select(sensorAddresses[i]);
        ow->write(0xBE); // READ SCRATCHPAD
        ow->read_bytes(scratchpad, 9);

        // Optional: minimal sanity (CRC) – robust but cheap
        // If OneWire::crc8 is available, you can enable:
        // if (OneWire::crc8(scratchpad, 8) != scratchpad[8]) {
        //     lastValid[i] = false;
        //     continue;
        // }

        int16_t raw = (int16_t)((scratchpad[1] << 8) | scratchpad[0]);
        float tempC = (float)raw / 16.0f; // DS18B20: 12-bit -> 0.0625°C

        lastTempsC[i] = tempC;
        lastValid[i]  = true;
    }

    unlock();
}

// ======================= Background RTOS Task =======================
//
// Behavior:
//   - Every cycle:
//       1) requestTemperatures()  → start conversion on all sensors
//       2) wait for conversion time (750ms for 12-bit)
//       3) updateAllTemperaturesBlocking() → cache into lastTempsC[]
//       4) sleep remaining time to maintain ~updateIntervalMs period
//
// No public API call ever touches the OneWire bus.
// getTemperature() is O(1) on cached data.
// ===================================================================

void TempSensor::temperatureTask(void* param) {
    auto* self = static_cast<TempSensor*>(param);
    const TickType_t convertWaitTicks = pdMS_TO_TICKS(750); // max DS18B20 12-bit

    for (;;) {
        // 1) Kick all sensors to start conversion
        self->requestTemperatures();

        // 2) Wait for conversion to complete
        vTaskDelay(convertWaitTicks);

        // 3) Read & cache all sensor temperatures
        self->updateAllTemperaturesBlocking();

        // 4) Determine interval (thread-safe snapshot)
        uint32_t intervalMs = TEMP_SENSOR_UPDATE_INTERVAL_MS;
        if (self->lock(pdMS_TO_TICKS(10))) {
            intervalMs = self->updateIntervalMs;
            if (intervalMs < 1000) {
                intervalMs = 1000; // clamp: avoid hammering the bus
            }
            self->unlock();
        }

        // Maintain approx. updateIntervalMs total period:
        // we've already spent ~convertWaitTicks + read time.
        // For simplicity & safety, just sleep (intervalMs - 750ms) if larger.
        int32_t remainMs = (int32_t)intervalMs - 750;
        if (remainMs < 100) {
            remainMs = 100; // ensure some yield
        }

        vTaskDelay(pdMS_TO_TICKS((uint32_t)remainMs));
    }
}
