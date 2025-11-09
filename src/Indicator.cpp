#include "Indicator.h"

// How many pending LED operations we can buffer.
#define INDICATOR_QUEUE_LEN 64   // increased from 32 for more headroom

Indicator::Indicator()
: shiftState(0),
  feedback(false),
  _taskHandle(nullptr),
  _queue(nullptr),
  _mutex(nullptr)
{
}

void Indicator::begin() {
    DEBUGGSTART();
    DEBUG_PRINTLN("###########################################################");
    DEBUG_PRINTLN("#                  Starting Indicator                     #");
    DEBUG_PRINTLN("###########################################################");
    DEBUGGSTOP();

    // 1) Create mutex (to guard hardware writes / shared fields)
    _mutex = xSemaphoreCreateMutex();

    // 2) Init GPIOs (74HC595 pins + the 2 direct LEDs)
    pinMode(SHIFT_SER_PIN, OUTPUT);
    pinMode(SHIFT_SCK_PIN, OUTPUT);
    pinMode(SHIFT_RCK_PIN, OUTPUT);
    pinMode(FL06_LED_PIN, OUTPUT);
    pinMode(FL08_LED_PIN, OUTPUT);

    // 3) Safe boot state
    if (lock()) {
        shiftState = 0;
        feedback   = true;  // ensure startupChaser displays at boot

        // Clear physical outputs
        digitalWrite(FL06_LED_PIN, LOW);
        digitalWrite(FL08_LED_PIN, LOW);
        hwUpdateShiftRegister(); // writes shiftState=0 to latch
        unlock();
    }

    // 4) Create the FIFO queue that producers will write to
    _queue = xQueueCreate(INDICATOR_QUEUE_LEN, sizeof(Cmd));

    // 5) Start worker task that will own the hardware
    xTaskCreate(
        taskTrampoline,
        "IndicatorTask",
        2048,
        this,
        1,
        &_taskHandle
    );

    // 6) Enqueue startup animation (runs atomically inside the worker)
    Cmd animCmd;
    animCmd.type    = CMD_STARTUP_CHASER;
    animCmd.index   = 0;
    animCmd.state   = false;
    animCmd.rawData = 0;
    sendCmd(animCmd);

    // 7) After boot animation, load final feedback setting from config
    bool fbConf = CONF->GetBool(LED_FEEDBACK_KEY, DEFAULT_LED_FEEDBACK);
    if (lock()) {
        feedback = fbConf;
        unlock();
    }

    DEBUG_PRINTLN("[Indicator] LED pins initialized, task running, startup sequence queued 🔧");
}

// -----------------------------------------------------------------------------
// Public API -> queue producers
// -----------------------------------------------------------------------------

void Indicator::setLED(uint8_t flIndex, bool state) {
    Cmd cmd;
    cmd.type    = CMD_SET_LED;
    cmd.index   = flIndex;
    cmd.state   = state;
    cmd.rawData = 0;
    sendCmd(cmd);
}

void Indicator::clearAll() {
    Cmd cmd;
    cmd.type    = CMD_CLEAR_ALL;
    cmd.index   = 0;
    cmd.state   = false;
    cmd.rawData = 0;
    sendCmd(cmd);
}

void Indicator::startupChaser() {
    Cmd cmd;
    cmd.type    = CMD_STARTUP_CHASER;
    cmd.index   = 0;
    cmd.state   = false;
    cmd.rawData = 0;
    sendCmd(cmd);
}

// Backward-compatibility wrappers (now enqueued)
void Indicator::updateShiftRegister() {
    Cmd cmd;
    cmd.type    = CMD_UPDATE_SHIFTREG;
    cmd.index   = 0;
    cmd.state   = false;
    cmd.rawData = 0;
    sendCmd(cmd);
}

void Indicator::setShiftLED(uint8_t qIndex, bool state) {
    Cmd cmd;
    cmd.type    = CMD_SET_SHIFT_LED;
    cmd.index   = qIndex;
    cmd.state   = state;
    cmd.rawData = 0;
    sendCmd(cmd);
}

void Indicator::shiftOutFast(uint8_t data) {
    Cmd cmd;
    cmd.type    = CMD_SHIFT_RAW;
    cmd.index   = 0;
    cmd.state   = false;
    cmd.rawData = data;
    sendCmd(cmd);
}

// -----------------------------------------------------------------------------
// Internal queue helper
// -----------------------------------------------------------------------------
void Indicator::sendCmd(const Cmd &cmd) {
    if (!_queue) return;

    // IMPORTANT: No mutex here — allow enqueues during animations.
    // Non-blocking send. If queue is full, drop oldest then enqueue newest.
    if (xQueueSendToBack(_queue, &cmd, 0) != pdTRUE) {
        Cmd trash;
        xQueueReceive(_queue, &trash, 0);   // drop oldest
        xQueueSendToBack(_queue, &cmd, 0);  // try again
    }
}

// -----------------------------------------------------------------------------
// RTOS task plumbing
// -----------------------------------------------------------------------------
void Indicator::taskTrampoline(void* pv) {
    Indicator* self = static_cast<Indicator*>(pv);
    self->taskLoop();
    vTaskDelete(nullptr); // should never return
}

void Indicator::taskLoop() {
    Cmd cmd;
    for (;;) {
        // Wait for next LED command
        if (xQueueReceive(_queue, &cmd, portMAX_DELAY) == pdTRUE) {
            // Take mutex before touching shared state / hardware
            if (lock()) {
                handleCmd(cmd);
                unlock();
            }
        }
    }
}

void Indicator::handleCmd(const Cmd &cmd) {
    switch (cmd.type) {
        case CMD_SET_LED:
            // If feedback is disabled, force everything OFF
            if (!feedback) {
                hwClearAll();
            } else {
                hwSetLED(cmd.index, cmd.state);
            }
            break;

        case CMD_CLEAR_ALL:
            hwClearAll();
            break;

        case CMD_STARTUP_CHASER:
            hwStartupChaser();
            break;

        case CMD_SET_SHIFT_LED:
            hwSetShiftLED(cmd.index, cmd.state);
            break;

        case CMD_UPDATE_SHIFTREG:
            hwUpdateShiftRegister();
            break;

        case CMD_SHIFT_RAW:
            hwShiftOutFast(cmd.rawData);
            break;
    }
}

// -----------------------------------------------------------------------------
// Low-level hardware ops (mutex already held)
// -----------------------------------------------------------------------------

// Map FL1..FL10 to either shift-register bits or direct GPIO
void Indicator::hwSetLED(uint8_t flIndex, bool state) {
    switch (flIndex) {
        case 1:  hwSetShiftLED(0, state); break;  // Q0 → FL1
        case 2:  hwSetShiftLED(2, state); break;  // Q2 → FL2
        case 3:  hwSetShiftLED(4, state); break;  // Q4 → FL3
        case 4:  hwSetShiftLED(6, state); break;  // Q6 → FL4
        case 5:  hwSetShiftLED(1, state); break;  // Q1 → FL5
        case 6:  digitalWrite(FL06_LED_PIN, state); break; // GPIO → FL6
        case 7:  hwSetShiftLED(3, state); break;  // Q3 → FL7
        case 8:  digitalWrite(FL08_LED_PIN, state); break; // GPIO → FL8
        case 9:  hwSetShiftLED(7, state); break;  // Q7 → FL9
        case 10: hwSetShiftLED(5, state); break;  // Q5 → FL10
        default:
            // invalid index -> ignore
            break;
    }
}

// Update shiftState bit in memory and push it to the shift register latch
void Indicator::hwSetShiftLED(uint8_t qIndex, bool state) {
    if (qIndex > 7) return;

    if (state)
        shiftState |= (1 << qIndex);
    else
        shiftState &= ~(1 << qIndex);

    hwUpdateShiftRegister();
}

// Actually clock out current shiftState to the 74HC595 and toggle latch
void Indicator::hwUpdateShiftRegister() {
    digitalWrite(SHIFT_RCK_PIN, LOW);
    hwShiftOutFast(shiftState);
    digitalWrite(SHIFT_RCK_PIN, HIGH);
}

// Bit-bang out 8 bits MSB→LSB on SHIFT_SER_PIN / SHIFT_SCK_PIN
void Indicator::hwShiftOutFast(uint8_t data) {
    for (int8_t i = 7; i >= 0; i--) {
        digitalWrite(SHIFT_SCK_PIN, LOW);
        digitalWrite(SHIFT_SER_PIN, (data >> i) & 0x01);
        digitalWrite(SHIFT_SCK_PIN, HIGH);
    }
}

// Clear all LEDs and record that state
void Indicator::hwClearAll() {
    shiftState = 0;
    hwUpdateShiftRegister();
    digitalWrite(FL06_LED_PIN, LOW);
    digitalWrite(FL08_LED_PIN, LOW);
    DEBUG_PRINTLN("[Indicator]Indicator: All LEDs turned OFF 📴");
}

// Animated startup pattern (runs atomically inside worker)
void Indicator::hwStartupChaser() {
    const uint16_t T_WIPE  = 40;  // ms per LED in the wipe
    const uint16_t T_DOT   = 40;  // ms per LED in ping-pong dot
    const uint16_t T_PHASE = 80;  // ms per even/odd flash phase

    // 0) Safety: ensure all off to start
    for (int i = 1; i <= 10; ++i) hwSetLED(i, false);

    // 1) Forward wipe ON
    for (int i = 1; i <= 10; ++i) {
        hwSetLED(i, true);
        vTaskDelay(pdMS_TO_TICKS(T_WIPE));
    }

    // 2) Forward wipe OFF
    for (int i = 1; i <= 10; ++i) {
        hwSetLED(i, false);
        vTaskDelay(pdMS_TO_TICKS(T_WIPE / 2));
    }

    // 3) Ping-pong single dot (L→R→L)
    for (int i = 1; i <= 10; ++i) {
        hwSetLED(i, true);
        vTaskDelay(pdMS_TO_TICKS(T_DOT));
        hwSetLED(i, false);
    }
    for (int i = 10; i >= 1; --i) {
        hwSetLED(i, true);
        vTaskDelay(pdMS_TO_TICKS(T_DOT));
        hwSetLED(i, false);
    }

    // 4) Even/Odd flash, then leave OFF
    for (int phase = 0; phase < 2; ++phase) {
        for (int i = 1; i <= 10; ++i) {
            bool odd = (i & 1);
            hwSetLED(i, phase ? !odd : odd);
        }
        vTaskDelay(pdMS_TO_TICKS(T_PHASE));
    }

    // Final clean/off
    for (int i = 1; i <= 10; ++i) hwSetLED(i, false);
}
