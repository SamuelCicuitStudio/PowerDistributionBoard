#include "FanManager.h"

// How many pending fan commands we buffer
#ifndef FAN_CMD_QUEUE_LEN
#define FAN_CMD_QUEUE_LEN 16
#endif

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
    if (started_) return;
    started_ = true;

    DEBUGGSTART();
    DEBUG_PRINTLN("###########################################################");
    DEBUG_PRINTLN("#                 Starting Fan Manager 🌀                 #");
    DEBUG_PRINTLN("###########################################################");
    DEBUGGSTOP();

    // 1) Create mutex first so all future hardware writes are protected
    _mutex = xSemaphoreCreateMutex();

    // 2) Configure LEDC PWM hardware channel
    ledcSetup(FAN_PWM_CHANNEL, PWM_FREQ, PWM_RESOLUTION);
    ledcAttachPin(FAN_PWM_PIN, FAN_PWM_CHANNEL);

    // 3) Create command queue
    _queue = xQueueCreate(FAN_CMD_QUEUE_LEN, sizeof(Cmd));

    // 4) Start worker task which will consume the queue
    xTaskCreate(
        taskTrampoline,
        "FanTask",
        4096,           // bigger stack: formatted logging + safety
        this,
        1,
        &_taskHandle
    );

    // 5) Start in a known safe state (fan OFF).
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
    // Snapshot duty safely
    uint8_t dutySnapshot;
    if (lock()) {
        dutySnapshot = currentDuty;
        unlock();
    } else {
        dutySnapshot = currentDuty;
    }
    // Convert back to %
    float pct = (dutySnapshot / 255.0f) * 100.0f + 0.5f;
    return static_cast<uint8_t>(pct);
}

// ======================================================================
// Internal queue helper
// ======================================================================

void FanManager::sendCmd(const Cmd &cmd) {
    if (!_queue) return;
    // Non-blocking; if full, drop oldest then enqueue newest (newest-wins)
    if (xQueueSendToBack(_queue, &cmd, 0) != pdTRUE) {
        Cmd drop;
        if (xQueueReceive(_queue, &drop, 0) == pdTRUE) {
            (void)xQueueSendToBack(_queue, &cmd, 0);
        }
    }
}

// ======================================================================
// RTOS task plumbing
// ======================================================================

void FanManager::taskTrampoline(void* pv) {
    FanManager* self = static_cast<FanManager*>(pv);
    self->taskLoop();
    vTaskDelete(nullptr); // never returns
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
            // clamp 0..100
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
    // Convert % -> duty (0..255)
    uint8_t duty = static_cast<uint8_t>((pct / 100.0f) * 255.0f);

    if (!lock()) return;
    currentDuty = duty;
    ledcWrite(FAN_PWM_CHANNEL, duty);
    unlock();

    // Print after unlock to keep critical section minimal
    DEBUG_PRINTF("[FanManager] Fan speed set to %u%% (duty %u) 🌀\n", pct, duty);
}

void FanManager::hwApplyStop() {
    if (!lock()) return;
    currentDuty = 0;
    ledcWrite(FAN_PWM_CHANNEL, 0);
    unlock();
    DEBUG_PRINTLN("[FanManager] Fan stopped ⛔");
}
