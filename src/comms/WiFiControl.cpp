#include <WiFiManager.hpp>
#include <Utils.hpp>
#include <DeviceTransport.hpp>
#include <math.h>

void WiFiManager::controlTaskTrampoline(void* pv) {
    static_cast<WiFiManager*>(pv)->controlTaskLoop();
    vTaskDelete(nullptr);
}

void WiFiManager::controlTaskLoop() {
    ControlCmd c{};
    for (;;) {
        if (xQueueReceive(_ctrlQueue, &c, portMAX_DELAY) == pdTRUE) {
            handleControl(c);
        }
    }
}

bool WiFiManager::sendCmd(const ControlCmd& c) {
    if (_ctrlQueue) {
        return xQueueSendToBack(_ctrlQueue, &c, 0) == pdTRUE; // non-blocking; drop if full
    }
    return false;
}

bool WiFiManager::handleControl(const ControlCmd& c) {
    DEBUG_PRINTF("[WiFi] Handling control type: %d\n",
                 static_cast<int>(c.type));

    bool ok = true;
    DeviceTransport* dt = DeviceTransport::Get();

    switch (c.type) {
        case CTRL_REBOOT:
            DEBUG_PRINTLN("[WiFi] CTRL_REBOOT Restarting system...");
            RGB->postOverlay(OverlayEvent::RESET_TRIGGER);
            BUZZ->bip();
            CONF->RestartSysDelayDown(3000);
            break;

        case CTRL_SYS_RESET:
            DEBUG_PRINTLN("[WiFi] CTRL_SYS_RESET Full system reset...");
            RGB->postOverlay(OverlayEvent::RESET_TRIGGER);
            BUZZ->bip();
            ok = dt->requestResetFlagAndRestart();
            break;

        case CTRL_LED_FEEDBACK_BOOL:
            BUZZ->bip();
            ok = dt->setLedFeedback(c.b1);
            break;

        case CTRL_BUZZER_MUTE:
            BUZZ->bip();
            ok = dt->setBuzzerMute(c.b1);
            break;

        case CTRL_RELAY_BOOL:
            BUZZ->bip();
            ok = dt->setRelay(c.b1, false);
            RGB->postOverlay(c.b1 ? OverlayEvent::RELAY_ON : OverlayEvent::RELAY_OFF);
            break;

        case CTRL_OUTPUT_BOOL:
            if (c.i1 >= 1 && c.i1 <= 10) {
                BUZZ->bip();
                if (isAdminConnected()) {
                    ok = dt->setOutput(c.i1, c.b1, true, false);
                    if (ok) RGB->postOutputEvent(c.i1, c.b1);
                } else if (isUserConnected()) {
                    const char* accessKeys[10] = {
                        OUT01_ACCESS_KEY, OUT02_ACCESS_KEY, OUT03_ACCESS_KEY,
                        OUT04_ACCESS_KEY, OUT05_ACCESS_KEY, OUT06_ACCESS_KEY,
                        OUT07_ACCESS_KEY, OUT08_ACCESS_KEY, OUT09_ACCESS_KEY,
                        OUT10_ACCESS_KEY
                    };
                    bool allowed =
                        CONF->GetBool(accessKeys[c.i1 - 1], false);
                    if (allowed) {
                        ok = dt->setOutput(c.i1, c.b1, true, false);
                        if (ok) RGB->postOutputEvent(c.i1, c.b1);
                    } else {
                        ok = false;
                    }
                } else {
                    ok = false;
                }
            } else {
                ok = false;
            }
            break;

        case CTRL_AC_FREQ:
            BUZZ->bip();
            ok = dt->setAcFrequency(c.i1);
            break;

        case CTRL_CHARGE_RES:
            BUZZ->bip();
            ok = dt->setChargeResistor(c.f1);
            break;

        case CTRL_ACCESS_BOOL:
            if (c.i1 >= 1 && c.i1 <= 10) {
                BUZZ->bip();
                ok = dt->setAccessFlag(c.i1, c.b1);
            } else {
                ok = false;
            }
            break;

        case CTRL_SYSTEM_START:
            BUZZ->bip();
            ok = dt->requestRun();
            if (ok) RGB->postOverlay(OverlayEvent::PWR_START);
            break;

        case CTRL_SYSTEM_WAKE:
            BUZZ->bip();
            ok = dt->requestWake();
            if (ok) RGB->postOverlay(OverlayEvent::WAKE_FLASH);
            break;

        case CTRL_SYSTEM_SHUTDOWN:
            BUZZ->bip();
            ok = dt->requestStop();
            if (ok) RGB->postOverlay(OverlayEvent::RELAY_OFF);
            break;

        case CTRL_FAN_SPEED: {
            int pct = constrain(c.i1, 0, 100);
            ok = dt->setFanSpeedPercent(pct, false);
            if (ok) {
                if (pct <= 0) RGB->postOverlay(OverlayEvent::FAN_OFF);
                else          RGB->postOverlay(OverlayEvent::FAN_ON);
            }
            break;
        }

        case CTRL_WIRE_RES: {
            int idx = constrain(c.i1, 1, 10);
            BUZZ->bip();
            ok = dt->setWireRes(idx, c.f1);
            break;
        }

        case CTRL_WIRE_OHM_PER_M: {
            float ohmPerM = c.f1;
            if (ohmPerM <= 0.0f) {
                ohmPerM = DEFAULT_WIRE_OHM_PER_M;
            }
            BUZZ->bip();
            ok = dt->setWireOhmPerM(ohmPerM);
            break;
        }
        case CTRL_WIRE_GAUGE: {
            int awg = constrain(c.i1, 1, 60);
            BUZZ->bip();
            ok = dt->setWireGaugeAwg(awg);
            break;
        }
        case CTRL_CURR_LIMIT: {
            BUZZ->bip();
            float limitA = c.f1;
            if (!isfinite(limitA) || limitA < 0.0f) {
                limitA = 0.0f;
            }
            ok = dt->setCurrentLimitA(limitA);
            break;
        }

        case CTRL_CONFIRM_WIRES_COOL:
            BUZZ->bip();
            ok = dt->confirmWiresCool();
            break;

        case CTRL_CALIBRATE:
            BUZZ->bip();
            ok = dt->startCalibrationTask();
            break;

        default:
            DEBUG_PRINTF("[WiFi] Unknown control type: %d\n",
                         static_cast<int>(c.type));
            ok = false;
            break;
    }

    DEBUG_PRINTF("[WiFi] Control result type=%d ok=%d\n",
                 static_cast<int>(c.type), ok ? 1 : 0);
    return ok;
}

