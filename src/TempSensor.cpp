#include "TempSensor.h"

TempSensor::TempSensor()
    : oneWire(ONE_WIRE_BUS), sensors(&oneWire) {}

void TempSensor::begin(ConfigManager* config) {
    Serial.println("###########################################################");
    Serial.println("#               Starting Temperature Manager 🌡️          #");
    Serial.println("###########################################################");

    cfg = config;
    sensors.begin();
    sensorCount = sensors.getDeviceCount();

    if (sensorCount < DEFAULT_TEMP_SENSOR_COUNT)
        sensorCount = DEFAULT_TEMP_SENSOR_COUNT;

    cfg->PutInt(TEMP_SENSOR_COUNT_KEY, sensorCount);
    DEBUG_PRINTF("[TempSensor] Initialized with %u sensors 📡\n", sensorCount);

    for (uint8_t i = 0; i < sensorCount && i < MAX_TEMP_SENSORS; ++i) {
        if (sensors.getAddress(sensorAddresses[i], i)) {
            DEBUG_PRINTF("[TempSensor] Sensor %u address: ", i);
            printAddress(sensorAddresses[i]);
        } else {
            DEBUG_PRINTF("[TempSensor] Sensor %u address not found ❌\n", i);
        }
    }

    startTemperatureTask();  // Start with default interval
}

void TempSensor::startTemperatureTask(uint32_t intervalMs) {
    stopTemperatureTask();  // Stop if already running

    updateIntervalMs = intervalMs;

    DEBUG_PRINTF("[TempSensor] Starting temperature task with %ums interval 🔁\n", updateIntervalMs);

    xTaskCreatePinnedToCore(
        TempSensor::temperatureTask,
        "TempUpdateTask",
        2048,
        this,
        1,
        &tempTaskHandle,
        APP_CPU_NUM
    );
}

void TempSensor::stopTemperatureTask() {
    if (tempTaskHandle != nullptr) {
        DEBUG_PRINTLN("[TempSensor] Stopping temperature task ⛔");
        vTaskDelete(tempTaskHandle);
        tempTaskHandle = nullptr;
    }
}

void TempSensor::requestTemperatures() {
    DEBUG_PRINTLN("[TempSensor] Requesting temperatures... 🔄");
    sensors.requestTemperatures();
}

float TempSensor::getTemperature(uint8_t index) {
    if (index >= sensorCount) {
        DEBUG_PRINTF("[TempSensor] Invalid index %u ❌\n", index);
        return NAN;
    }

    float temp = sensors.getTempCByIndex(index);
    DEBUG_PRINTF("[TempSensor] Sensor %u (", index);
    printAddress(sensorAddresses[index]);
    Serial.printf(") = %.2f°C 🌡️\n", temp);
    return temp;
}

uint8_t TempSensor::getSensorCount() {
    return cfg->GetInt(TEMP_SENSOR_COUNT_KEY, DEFAULT_TEMP_SENSOR_COUNT);
}

void TempSensor::printAddress(DeviceAddress address) {
    for (uint8_t i = 0; i < 8; i++) {
        Serial.print("0x");
        if (address[i] < 0x10) Serial.print("0");
        Serial.print(address[i], HEX);
        if (i < 7) Serial.print(", ");
    }
    Serial.println();
}

void TempSensor::temperatureTask(void* param) {
    TempSensor* instance = static_cast<TempSensor*>(param);
    for (;;) {
        instance->requestTemperatures();
        vTaskDelay(pdMS_TO_TICKS(instance->updateIntervalMs));
    }
}
