/**************************************************************
 *  Author      : Tshibangu Samuel
 *  Role        : Freelance Embedded Systems Engineer
 *  Expertise   : Secure IoT Systems, Embedded C++, RTOS, Control Logic
 *  Contact     : tshibsamuel47@gmail.com
 *  Portfolio   : https://www.freelancer.com/u/tshibsamuel477
 *  Phone       : +216 54 429 793
 **************************************************************/
#ifndef SWITCH_MANAGER_H
#define SWITCH_MANAGER_H

#include <Arduino.h>
#include <DeviceTransport.hpp>

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


