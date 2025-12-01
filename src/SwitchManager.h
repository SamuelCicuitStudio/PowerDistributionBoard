#ifndef SWITCH_MANAGER_H
#define SWITCH_MANAGER_H

#include <Arduino.h>
#include "DeviceTransport.h"

class SwitchManager {
public:
    SwitchManager();
    static SwitchManager* instance;

    static void SwitchTask(void* pvParameters);
    void TapDetect();

private:
    void detectTapOrHold();
};

#endif // SWITCH_MANAGER_H
