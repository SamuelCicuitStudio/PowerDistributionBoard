#ifndef BYPASS_MOSFET_H
#define BYPASS_MOSFET_H

#include "Config.h"


class BypassMosfet {
public:
    BypassMosfet() : state(false), _mutex(nullptr) {}

    void begin();       
    void enable();      
    void disable();     
    bool isEnabled() const;

private:
    bool state;                       // true = bypass active, false = off
    SemaphoreHandle_t _mutex;         // protects state and pin writes

    // internal helpers to lock/unlock safely
    inline bool lock() const {
        if (_mutex == nullptr) return true;
        return (xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE);
    }

    inline void unlock() const {
        if (_mutex) xSemaphoreGive(_mutex);
    }
};

#endif // BYPASS_MOSFET_H
