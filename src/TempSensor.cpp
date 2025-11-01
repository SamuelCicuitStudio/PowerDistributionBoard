#include "TempSensor.h"

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

    // Create mutex first so everything after is protected
    _mutex = xSemaphoreCreateMutex();

    // Discover all devices on the bus
    if (lock()) {
        sensorCount = 0;
        ow->reset_search();
        while (sensorCount < MAX_TEMP_SENSORS &&
               ow->search(sensorAddresses[sensorCount])) {

            DEBUG_PRINTF("[TempSensor] Found sensor %u: ", sensorCount);
            printAddress(sensorAddresses[sensorCount]);
            sensorCount++;
        }
        unlock();
    }

    if (sensorCount == 0) {
        DEBUG_PRINTLN("[TempSensor] No sensors found ❌");
        return;
    }

    CONF->PutInt(TEMP_SENSOR_COUNT_KEY, sensorCount);
    DEBUG_PRINTF("[TempSensor] %u sensor(s) found ✅\n", sensorCount);

    startTemperatureTask();
}

void TempSensor::requestTemperatures() {
    if (!ow) return;

    // Protect OneWire bus while we broadcast "convert T" to each sensor
    if (!lock()) return;

    for (uint8_t i = 0; i < sensorCount; i++) {
        ow->reset();
        ow->select(sensorAddresses[i]);
        ow->write(0x44);  // Start temperature conversion
    }

    unlock();
}

float TempSensor::getTemperature(uint8_t index) {
    if (!ow || index >= sensorCount) {
        //DEBUG_PRINTF("[TempSensor] Invalid index %u ❌\n", index);
        return NAN;
    }

    // Protect OneWire bus + shared scratchpad buffer
    if (!lock()) {
        DEBUG_PRINTLN("[TempSensor] Mutex lock failed ❌");
        return NAN;
    }

    // Select specific sensor and read scratchpad
    ow->reset();
    ow->select(sensorAddresses[index]);
    ow->write(0xBE);  // Read scratchpad
    ow->read_bytes(scratchpad, 9);

    int16_t raw = (scratchpad[1] << 8) | scratchpad[0];
    float tempC = (float)raw / 16.0f;  // DS18B20: 12-bit -> 0.0625°C LSB

    unlock();

    return tempC;
}

uint8_t TempSensor::getSensorCount() {
    if (!CONF) return 0;
    return CONF->GetInt(TEMP_SENSOR_COUNT_KEY, 0);
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
    // Stop old task first
    stopTemperatureTask();

    // Store the new interval safely
    if (lock()) {
        updateIntervalMs = intervalMs;
        unlock();
    }

    // Spawn periodic updater task pinned where you want it
    xTaskCreatePinnedToCore(
        TempSensor::temperatureTask,
        "TempUpdateTask",
        TEMP_SENSOR_TASK_STACK_SIZE,
        this,
        TEMP_SENSOR_TASK_PRIORITY,
        &tempTaskHandle,
        TEMP_SENSOR_TASK_CORE
    );
}

void TempSensor::temperatureTask(void* param) {
    auto* instance = static_cast<TempSensor*>(param);

    for (;;) {
        // Kick all sensors to start a new conversion round
        instance->requestTemperatures();

        // Take a snapshot of delay without holding the mutex during vTaskDelay
        uint32_t intervalSnapshot = 2000;
        if (instance->lock()) {
            intervalSnapshot = instance->updateIntervalMs;
            instance->unlock();
        }

        vTaskDelay(pdMS_TO_TICKS(intervalSnapshot));
    }
}
