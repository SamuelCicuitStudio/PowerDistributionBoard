#include "TempSensor.h"

void TempSensor::begin() {
    Serial.println("###########################################################");
    Serial.println("#               Starting Temperature Manager 🌡️          #");
    Serial.println("###########################################################");

    if (!cfg || !ow) {
        Serial.println("[TempSensor] Missing dependencies ❌");
        return;
    }

    // Discover all devices on the bus
    sensorCount = 0;
    ow->reset_search();
    while (ow->search(sensorAddresses[sensorCount]) && sensorCount < MAX_TEMP_SENSORS) {
        Serial.printf("[TempSensor] Found sensor %u: ", sensorCount);
        printAddress(sensorAddresses[sensorCount]);
        sensorCount++;
    }

    if (sensorCount == 0) {
        Serial.println("[TempSensor] No sensors found ❌");
        return;
    }

    cfg->PutInt(TEMP_SENSOR_COUNT_KEY, sensorCount);
    Serial.printf("[TempSensor] %u sensor(s) found ✅\n", sensorCount);

    startTemperatureTask();
}

void TempSensor::requestTemperatures() {
    // Issue conversion command to all devices
    for (uint8_t i = 0; i < sensorCount; i++) {
        ow->reset();
        ow->select(sensorAddresses[i]);
        ow->write(0x44);  // Start conversion
    }
}

float TempSensor::getTemperature(uint8_t index) {
    if (!ow || index >= sensorCount) {
        Serial.printf("[TempSensor] Invalid index %u ❌\n", index);
        return NAN;
    }

    ow->reset();
    ow->select(sensorAddresses[index]);
    ow->write(0xBE);  // Read scratchpad
    ow->read_bytes(scratchpad, 9);

    int16_t raw = (scratchpad[1] << 8) | scratchpad[0];
    return (float)raw / 16.0;  // 12-bit resolution
}

uint8_t TempSensor::getSensorCount() {
    if (!cfg) return 0;
    return cfg->GetInt(TEMP_SENSOR_COUNT_KEY, 0);
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
    updateIntervalMs = intervalMs;
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
        instance->requestTemperatures();
        vTaskDelay(pdMS_TO_TICKS(instance->updateIntervalMs));
    }
}
