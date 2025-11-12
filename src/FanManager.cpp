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
    // members default-initialized in header
}

void FanManager::begin() {
    if (started_) return;
    started_ = true;

    DEBUGGSTART();
    DEBUG_PRINTLN("###########################################################");
    DEBUG_PRINTLN("#             Starting Dual-Fan Manager 🌀🌀              #");
    DEBUG_PRINTLN("###########################################################");
    DEBUGGSTOP();

    // 1) Create mutex so all future hardware writes are protected
    _mutex = xSemaphoreCreateMutex();

    // 2) Configure LEDC PWM hardware channels (both fans)
    ledcSetup(FAN_CAP_PWM_CHANNEL,  FAN_PWM_FREQ, FAN_PWM_RESOLUTION);
    ledcSetup(FAN_HS_PWM_CHANNEL,   FAN_PWM_FREQ, FAN_PWM_RESOLUTION);

    ledcAttachPin(FAN_CAP_PWM_PIN,  FAN_CAP_PWM_CHANNEL);
    ledcAttachPin(FAN_HS_PWM_PIN,   FAN_HS_PWM_CHANNEL);

    // 3) Create command queue
    _queue = xQueueCreate(FAN_CMD_QUEUE_LEN, sizeof(Cmd));

    // 4) Start worker task to consume queue
    xTaskCreate(
        taskTrampoline,
        "FanTask",
        4096,
        this,
        1,
        &_taskHandle
    );

    // 5) Start in a known safe state (both OFF)
    sendCmd({CMD_STOP, 0, FanSel::Cap});
    sendCmd({CMD_STOP, 0, FanSel::Heatsink});

    DEBUG_PRINTLN("[Fan] Dual-fan initialized; both STOP queued 🛑");
}

// ======================================================================
// Back-compat API (maps to CAP fan)
// ======================================================================
void FanManager::setSpeedPercent(uint8_t pct)      { setCapSpeedPercent(pct); }
void FanManager::stop()                            { stopCap(); }
uint8_t FanManager::getSpeedPercent() const        { return getCapSpeedPercent(); }

// ======================================================================
// New dual-fan API
// ======================================================================
void FanManager::setCapSpeedPercent(uint8_t pct)        { sendCmd({CMD_SET_SPEED, pct, FanSel::Cap}); }
void FanManager::stopCap()                               { sendCmd({CMD_STOP, 0,    FanSel::Cap}); }
uint8_t FanManager::getCapSpeedPercent() const {
    uint8_t d=0;
    if (lock()) { d = currentDuty_[0]; unlock(); } else { d = currentDuty_[0]; }
    float pct = (d / 255.0f) * 100.0f + 0.5f;
    return (uint8_t)pct;
}

void FanManager::setHeatsinkSpeedPercent(uint8_t pct)   { sendCmd({CMD_SET_SPEED, pct, FanSel::Heatsink}); }
void FanManager::stopHeatsink()                          { sendCmd({CMD_STOP, 0,    FanSel::Heatsink}); }
uint8_t FanManager::getHeatsinkSpeedPercent() const {
    uint8_t d=0;
    if (lock()) { d = currentDuty_[1]; unlock(); } else { d = currentDuty_[1]; }
    float pct = (d / 255.0f) * 100.0f + 0.5f;
    return (uint8_t)pct;
}

// ======================================================================
// Internal queue helper
// ======================================================================
void FanManager::sendCmd(const Cmd &cmd) {
    if (!_queue) return;
    // Non-blocking; newest-wins if full
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
    auto* self = static_cast<FanManager*>(pv);
    self->taskLoop();
    vTaskDelete(nullptr);
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
            uint8_t pct = constrain(cmd.pct, 0, 100);
            hwApplySpeedPercent(cmd.which, pct);
            break;
        }
        case CMD_STOP:
            hwApplyStop(cmd.which);
            break;
    }
}

// ======================================================================
// Low-level hardware ops (called ONLY from worker task)
// ======================================================================
void FanManager::hwApplySpeedPercent(FanSel which, uint8_t pct) {
    const uint8_t duty = (uint8_t)((pct / 100.0f) * 255.0f);

    if (!lock()) return;

    if (which == FanSel::Cap) {
        currentDuty_[0] = duty;
        ledcWrite(FAN_CAP_PWM_CHANNEL, duty);
    } else {
        currentDuty_[1] = duty;
        ledcWrite(FAN_HS_PWM_CHANNEL, duty);
    }

    unlock();

    // Print after unlock to keep critical section minimal
    if (which == FanSel::Cap) {
        DEBUG_PRINTF("[Fan] CAP speed -> %u%% (duty %u) 🌀\n", pct, duty);
    } else {
        DEBUG_PRINTF("[Fan] HS  speed -> %u%% (duty %u) 🌀\n", pct, duty);
    }
}

void FanManager::hwApplyStop(FanSel which) {
    if (!lock()) return;

    if (which == FanSel::Cap) {
        currentDuty_[0] = 0;
        ledcWrite(FAN_CAP_PWM_CHANNEL, 0);
    } else {
        currentDuty_[1] = 0;
        ledcWrite(FAN_HS_PWM_CHANNEL, 0);
    }

    unlock();

    if (which == FanSel::Cap) {
        DEBUG_PRINTLN("[Fan] CAP stopped ⛔");
    } else {
        DEBUG_PRINTLN("[Fan] HS  stopped ⛔");
    }
}
