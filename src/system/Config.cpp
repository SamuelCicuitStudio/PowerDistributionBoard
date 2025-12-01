#include "system/Config.h"
#include <pgmspace.h>  // for reading F("...") strings from flash
#include "esp_heap_caps.h"   // heap_caps_malloc for PSRAM

// ***********************************************
// Globals shared with other modules
// NOTE: WiFiManager now updates wifiStatus under its own mutex,
// and Device updates StartFromremote under its own mutex. We
// keep them volatile here because multiple tasks read them. :contentReference[oaicite:3]{index=3}
volatile WiFiStatus wifiStatus;
volatile bool StartFromremote;
