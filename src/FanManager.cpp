#include "FanManager.h"

// How many pending fan commands we buffer
#define FAN_CMD_QUEUE_LEN 16

// ===== Singleton storage =====
FanManager* FanManager::s_instance = nullptr;

void FanManager::Init() {
    if (!s_instance) s_instance = new FanManager();
}

FanManager* FanManager::Get() {
    if (!s_instance) s_instance = new FanManager();
    return s_instance;
}

// ===== Ctor kept lightweight; heavy work happens in begin() =====
FanManager::FanManager() {
    // members are already default-initialized in the header
}

void FanManager::begin() {
    if (started_) {
        // already initialized; nothing to do
        return;
    }
    started_ = true;

    DEBUGGSTART();
    DEBUG_PRINTLN("###########################################################");
    DEBUG_PRINTLN("#                 Starting Fan Manager 🌀                 #");
    DEBUG_PRINTLN("###########################################################");
    DEBUGGSTOP();

    // 1. Create mutex first so all future hardware writes are protected
    _mutex = xSemaphoreCreateMutex();

    // 2. Configure LEDC PWM hardware channel
    ledcSetup(FAN_PWM_CHANNEL, PWM_FREQ, PWM_RESOLUTION);
    ledcAttachPin(FAN_PWM_PIN, FAN_PWM_CHANNEL);

    // 3. Create command queue
    _queue = xQueueCreate(FAN_CMD_QUEUE_LEN, sizeof(Cmd));

    // 4. Start worker task which will consume the queue
    xTaskCreate(
        taskTrampoline,
        "FanTask",
        2048,
        this,
        1,
        &_taskHandle
    );

    // 5. Ensure we start in a known safe state (fan OFF).
    Cmd initCmd; initCmd.type = CMD_STOP; initCmd.pct = 0;
    sendCmd(initCmd);

    DEBUG_PRINTLN("[FanManager] Initialized and STOP command queued 🛑");
}

// ======================================================================
// Public API (producers)
// ======================================================================

void FanManager::setSpeedPercent(uint8_t pct) {
    Cmd cmd; cmd.type = CMD_SET_SPEED; cmd.pct = pct; // clamp in handleCmd
    sendCmd(cmd);
}

void FanManager::stop() {
    Cmd cmd; cmd.type = CMD_STOP; cmd.pct = 0;
    sendCmd(cmd);
}

uint8_t FanManager::getSpeedPercent() const {
    uint8_t dutySnapshot = 0;
    if (!lock()) dutySnapshot = currentDuty;
    else { dutySnapshot = currentDuty; unlock(); }
    float pct = (dutySnapshot / 255.0f) * 100.0f + 0.5f;
    return static_cast<uint8_t>(pct);
}

// ======================================================================
// Internal queue helper
// ======================================================================

void FanManager::sendCmd(const Cmd &cmd) {
    if (!_queue) return;
    // FreeRTOS queues are multi-producer safe; no mutex here.
    // Non-blocking; if full, drop the new request (newest-wins policy).
    xQueueSendToBack(_queue, &cmd, 0);
}

// ======================================================================
// RTOS task plumbing
// ======================================================================

void FanManager::taskTrampoline(void* pv) {
    FanManager* self = static_cast<FanManager*>(pv);
    self->taskLoop();
    vTaskDelete(nullptr); // should never actually return
}

void FanManager::taskLoop() {
    Cmd cmd;
    for (;;) {
        if (xQueueReceive(_queue, &cmd, portMAX_DELAY) == pdTRUE) {
            handleCmd(cmd);
        }
    }
}

void FanManager::handleCmd(const Cmd &cmd) {
    switch (cmd.type) {
        case CMD_SET_SPEED: {
            // clamp 0..100 (uses your existing constrain())
            uint8_t pct = constrain(cmd.pct, 0, 100);
            hwApplySpeedPercent(pct);
            break;
        }
        case CMD_STOP:
            hwApplyStop();
            break;
    }
}

// ======================================================================
// Low-level hardware ops (called ONLY from worker task)
// ======================================================================

void FanManager::hwApplySpeedPercent(uint8_t pct) {
    if (!lock()) return;
    // Convert % -> duty (0..255 like original code)
    uint8_t duty = static_cast<uint8_t>((pct / 100.0f) * 255.0f);
    currentDuty = duty;
    ledcWrite(FAN_PWM_CHANNEL, duty);
    unlock();
    DEBUG_PRINTF("[FanManager] Fan speed set to %u%% (duty %u) 🌀\n", pct, duty);
}

void FanManager::hwApplyStop() {
    if (!lock()) return;
    currentDuty = 0;
    ledcWrite(FAN_PWM_CHANNEL, 0);
    unlock();
    DEBUG_PRINTLN("[FanManager] Fan stopped ⛔");
}
