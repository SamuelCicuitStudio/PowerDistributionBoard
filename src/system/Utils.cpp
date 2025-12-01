/**************************************************************
 *  Author      : Tshibangu Samuel
 *  Role        : Freelance Embedded Systems Engineer
 *  Expertise   : Secure IoT Systems, Embedded C++, RTOS, Control Logic
 *  Contact     : tshibsamuel47@gmail.com
 *  Portfolio   : https://www.freelancer.com/u/tshibsamuel477
 *  Phone       : +216 54 429 793
 **************************************************************/

#include "system/Utils.h"
#include <pgmspace.h>

#if defined(ESP32)
  #include "esp_heap_caps.h"
#endif

// ===================== Internal config =====================

// Max characters per message chunk (excluding optional trailing newline).
#ifndef DBG_LINE_MAX
#define DBG_LINE_MAX        1024
#endif

// Queue depth (stores DebugMsg* pointers).
#ifndef DBG_QUEUE_DEPTH
#define DBG_QUEUE_DEPTH     1024
#endif

// Max bytes for a single grouped burst (static buffer, never reallocs).
#ifndef DBG_GROUP_MAX
#define DBG_GROUP_MAX       8192
#endif

static_assert(DBG_LINE_MAX >= 32,        "DBG_LINE_MAX too small");
static_assert(DBG_GROUP_MAX >= DBG_LINE_MAX,
              "DBG_GROUP_MAX must be >= DBG_LINE_MAX");

// ===================== Debug implementation =====================

namespace {

struct DebugMsg {
    uint16_t len;          // Number of bytes in text (excluding NUL)
    bool     addNewline;   // Whether to append '\n' when flushing
    char     text[];       // Flexible array: NUL-terminated payload
};

QueueHandle_t     s_dbgQ        = nullptr; // Queue of DebugMsg*
TaskHandle_t      s_dbgTask     = nullptr; // Background writer task
SemaphoreHandle_t s_serialMtx   = nullptr; // Mutex guarding Serial writes
bool              s_started     = false;   // Debug system started flag

// Grouping (atomic bursts)
SemaphoreHandle_t s_groupGate   = nullptr; // Gate protecting group state
TaskHandle_t      s_groupOwner  = nullptr; // Current owner task handle
bool              s_groupActive = false;   // Grouping active flag

// Static group buffer (no realloc/free)
static char   s_groupBuf[DBG_GROUP_MAX];
static size_t s_groupLen = 0;
static size_t s_groupCap = DBG_GROUP_MAX;

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------

inline TaskHandle_t curTask_() {
#if defined(ESP32)
    return xTaskGetCurrentTaskHandle();
#else
    return (TaskHandle_t)1; // Fallback for non-RTOS / single-thread
#endif
}

void ensureGroupInit_() {
    if (!s_groupGate) {
    #if defined(ESP32)
        s_groupGate = xSemaphoreCreateRecursiveMutex();
    #else
        s_groupGate = xSemaphoreCreateMutex();
    #endif
    }
}

// Reset group buffer indices (buffer storage is static).
inline void groupBufReset_() {
    s_groupLen = 0;
    if (s_groupCap != DBG_GROUP_MAX) {
        s_groupCap = DBG_GROUP_MAX;
    }
    s_groupBuf[0] = '\0';
}

// Allocate DebugMsg in PSRAM if available; fallback to internal heap.
DebugMsg* allocMsg_(size_t payloadLen, bool addNewline) {
    if (payloadLen > (DBG_LINE_MAX - 1)) {
        payloadLen = DBG_LINE_MAX - 1;
    }

    const size_t bytes = sizeof(DebugMsg) + payloadLen + 1;
    void* mem = nullptr;

#if defined(ESP32)
    mem = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!mem) {
        mem = heap_caps_malloc(bytes, MALLOC_CAP_8BIT);
    }
#else
    mem = malloc(bytes);
#endif

    if (!mem) return nullptr;

    auto* m   = reinterpret_cast<DebugMsg*>(mem);
    m->len    = static_cast<uint16_t>(payloadLen);
    m->addNewline = addNewline;
    m->text[0] = '\0';
    return m;
}

inline void freeMsg_(DebugMsg* m) {
    if (!m) return;
#if defined(ESP32)
    heap_caps_free(m);
#else
    free(m);
#endif
}

// Enqueue pointer; if full, drop oldest (never block writers).
void enqueuePtr_(DebugMsg* m) {
    if (!s_dbgQ) {
        freeMsg_(m);
        return;
    }

    if (xQueueSend(s_dbgQ, &m, 0) == pdTRUE) {
        return;
    }

    // Queue full: drop oldest then retry.
    DebugMsg* old = nullptr;
    if (xQueueReceive(s_dbgQ, &old, 0) == pdTRUE) {
        freeMsg_(old);
        (void)xQueueSend(s_dbgQ, &m, 0);
        return;
    }

    // Still full: drop the new message.
    freeMsg_(m);
}

// Ensure debug system started (queue, task, mutex, Serial).
void ensureDebugStart_(unsigned long baud = 115200) {
    if (s_started) return;

    if (!s_serialMtx) {
        s_serialMtx = xSemaphoreCreateMutex();
    }

    if (!s_dbgQ) {
        s_dbgQ = xQueueCreate(DBG_QUEUE_DEPTH, sizeof(DebugMsg*));
    }

    ensureGroupInit_();

    if (!s_dbgTask) {
        xTaskCreatePinnedToCore(
            [](void*) {
                DebugMsg* p = nullptr;
                for (;;) {
                    if (xQueueReceive(s_dbgQ, &p, portMAX_DELAY) == pdTRUE) {
                        if (p) {
                            if (s_serialMtx) {
                                xSemaphoreTake(s_serialMtx, portMAX_DELAY);
                            }

                            Serial.write(
                                reinterpret_cast<const uint8_t*>(p->text),
                                p->len
                            );
                            if (p->addNewline) {
                                Serial.write(
                                    reinterpret_cast<const uint8_t*>("\n"), 1);
                            }

                            if (s_serialMtx) {
                                xSemaphoreGive(s_serialMtx);
                            }

                            freeMsg_(p);
                        }
                    }
                }
            },
            "DebugPrintTask",
            4096,
            nullptr,
            1,
            &s_dbgTask,
            tskNO_AFFINITY
        );
    }

    if (!Serial) {
        Serial.begin(baud);
    }

    s_started = true;
}

// Flush group buffer to queue in chunks (â‰¤ DBG_LINE_MAX), optional trailing \n.
void flushGroupToQueue_(bool addTrailingNewline) {
    if (!s_groupActive || s_groupLen == 0) {
        if (addTrailingNewline) {
            DebugMsg* m = allocMsg_(0, true);
            if (m) enqueuePtr_(m);
        }
        return;
    }

    size_t offset = 0;
    while (offset < s_groupLen) {
        size_t slice = s_groupLen - offset;
        if (slice > (DBG_LINE_MAX - 1)) {
            slice = DBG_LINE_MAX - 1;
        }

        DebugMsg* m = allocMsg_(slice, false);
        if (!m) {
            // Allocation failed: drop remaining group data safely.
            offset = s_groupLen;
            break;
        }

        memcpy(m->text, s_groupBuf + offset, slice);
        m->text[slice] = '\0';
        enqueuePtr_(m);
        offset += slice;
    }

    if (addTrailingNewline) {
        DebugMsg* nl = allocMsg_(0, true);
        if (nl) enqueuePtr_(nl);
    }

    groupBufReset_();
}

// Append data into group buffer; flush automatically if it fills.
void groupAppend_(const char* data, size_t n, bool addNl) {
    if (!data) {
        data = "";
        n    = 0;
    }

    while (n > 0) {
        size_t space =
            (s_groupCap > 0) ? (s_groupCap - s_groupLen - 1) : 0; // reserve 1 for NUL

        if (space == 0) {
            flushGroupToQueue_(false);
            space =
                (s_groupCap > 0) ? (s_groupCap - s_groupLen - 1) : 0;

            if (space == 0) {
                // Degenerate case: cap too small; avoid infinite loop.
                return;
            }
        }

        size_t chunk = (n < space) ? n : space;
        if (chunk) {
            memcpy(s_groupBuf + s_groupLen, data, chunk);
            s_groupLen += chunk;
            data       += chunk;
            n          -= chunk;
            s_groupBuf[s_groupLen] = '\0';
        }

        if (n > 0) {
            flushGroupToQueue_(false);
        }
    }

    if (addNl) {
        if (s_groupLen + 1 >= s_groupCap) {
            flushGroupToQueue_(false);
        }

        if (s_groupLen + 1 < s_groupCap) {
            s_groupBuf[s_groupLen++] = '\n';
            s_groupBuf[s_groupLen]   = '\0';
        } else {
            DebugMsg* nl = allocMsg_(0, true);
            if (nl) enqueuePtr_(nl);
        }
    }
}

inline bool iOwnGroup_() {
    return s_groupActive && (curTask_() == s_groupOwner);
}

// Enqueue or group-append a C-string.
inline void enqueueStr_orGroup_(const char* s, bool nl) {
    ensureDebugStart_();

#if defined(ESP32)
    if (s_groupGate) {
        xSemaphoreTakeRecursive(s_groupGate, portMAX_DELAY);
    } else if (s_groupActive) {
        vTaskDelay(1);
    }
#else
    if (s_groupGate) {
        xSemaphoreTake(s_groupGate, portMAX_DELAY);
    }
#endif

    if (iOwnGroup_()) {
        size_t n = s ? strnlen(s, DBG_LINE_MAX - 1) : 0;
        groupAppend_(s, n, nl);
    } else {
        size_t n = s ? strnlen(s, DBG_LINE_MAX - 1) : 0;
        DebugMsg* m = allocMsg_(n, nl);
        if (m) {
            if (n) memcpy(m->text, s, n);
            m->text[n] = '\0';
            enqueuePtr_(m);
        }
    }

#if defined(ESP32)
    if (s_groupGate) xSemaphoreGiveRecursive(s_groupGate);
#else
    if (s_groupGate) xSemaphoreGive(s_groupGate);
#endif
}

// Enqueue or group-append a flash-stored string.
inline void enqueueFlash_orGroup_(const __FlashStringHelper* fs, bool nl) {
    ensureDebugStart_();
    if (!fs) return;

#if defined(ESP32)
    if (s_groupGate) {
        xSemaphoreTakeRecursive(s_groupGate, portMAX_DELAY);
    } else if (s_groupActive) {
        vTaskDelay(1);
    }
#else
    if (s_groupGate) {
        xSemaphoreTake(s_groupGate, portMAX_DELAY);
    }
#endif

    const char* p = reinterpret_cast<const char*>(fs);

    if (iOwnGroup_()) {
        char   tmp[DBG_LINE_MAX];
        size_t n = 0;

        while (n < DBG_LINE_MAX - 1) {
            uint8_t c = pgm_read_byte(p + n);
            if (!c) break;
            tmp[n++] = static_cast<char>(c);
        }

        groupAppend_(tmp, n, nl);
    } else {
        size_t n = 0;
        while (n < DBG_LINE_MAX - 1) {
            uint8_t c = pgm_read_byte(p + n);
            if (!c) break;
            ++n;
        }

        DebugMsg* m = allocMsg_(n, nl);
        if (m) {
            for (size_t i = 0; i < n; ++i) {
                m->text[i] = static_cast<char>(pgm_read_byte(p + i));
            }
            m->text[n] = '\0';
            enqueuePtr_(m);
        }
    }

#if defined(ESP32)
    if (s_groupGate) xSemaphoreGiveRecursive(s_groupGate);
#else
    if (s_groupGate) xSemaphoreGive(s_groupGate);
#endif
}

// vprintf helper: stack buffer -> PSRAM msg or group buffer.
inline void vprintf_enqueue_orGroup_(const char* fmt, va_list ap) {
    ensureDebugStart_();

    char buf[DBG_LINE_MAX];
    vsnprintf(buf, sizeof(buf), fmt ? fmt : "", ap);

#if defined(ESP32)
    if (s_groupGate) {
        xSemaphoreTakeRecursive(s_groupGate, portMAX_DELAY);
    } else if (s_groupActive) {
        vTaskDelay(1);
    }
#else
    if (s_groupGate) {
        xSemaphoreTake(s_groupGate, portMAX_DELAY);
    }
#endif

    if (iOwnGroup_()) {
        groupAppend_(buf, strnlen(buf, DBG_LINE_MAX - 1), false);
    } else {
        size_t n = strnlen(buf, DBG_LINE_MAX - 1);
        DebugMsg* m = allocMsg_(n, false);
        if (m) {
            memcpy(m->text, buf, n);
            m->text[n] = '\0';
            enqueuePtr_(m);
        }
    }

#if defined(ESP32)
    if (s_groupGate) xSemaphoreGiveRecursive(s_groupGate);
#else
    if (s_groupGate) xSemaphoreGive(s_groupGate);
#endif
}

template<typename T>
inline void print_num_orGroup_(T v, const char* fmt_no_nl, bool nl) {
    ensureDebugStart_();
    char buf[DBG_LINE_MAX];
    snprintf(buf, sizeof(buf), fmt_no_nl, v);
    enqueueStr_orGroup_(buf, nl);
}

inline void print_float_prec_orGroup_(float v, int digits, bool nl) {
    ensureDebugStart_();
    if (digits < 0)   digits = 0;
    if (digits > 8)   digits = 8;

    char buf[DBG_LINE_MAX];
    dtostrf(v, 0, digits, buf);
    enqueueStr_orGroup_(buf, nl);
}

} // namespace (internal)


// ===================== Public Debug namespace =====================

namespace Debug {

void begin(unsigned long baud) {
    ensureDebugStart_(baud);
}

// Strings
void print(const char* s)                   { enqueueStr_orGroup_(s, false); }
void print(const String& s)                 { enqueueStr_orGroup_(s.c_str(), false); }
void print(const __FlashStringHelper* fs)   { enqueueFlash_orGroup_(fs, false); }

void println(const char* s)                 { enqueueStr_orGroup_(s, true); }
void println(const String& s)               { enqueueStr_orGroup_(s.c_str(), true); }
void println(const __FlashStringHelper* fs) { enqueueFlash_orGroup_(fs, true); }
void println()                               { enqueueStr_orGroup_("", true); }

// Numbers
void print(int32_t v)         { print_num_orGroup_<int32_t>(v,        "%ld",  false); }
void print(uint32_t v)        { print_num_orGroup_<uint32_t>(v,       "%lu",  false); }
void print(int64_t v)         { print_num_orGroup_<int64_t>(v,        "%lld", false); }
void print(uint64_t v)        { print_num_orGroup_<uint64_t>(v,       "%llu", false); }
void print(long v)            { print_num_orGroup_<long>(v,           "%ld",  false); }
void print(unsigned long v)   { print_num_orGroup_<unsigned long>(v,  "%lu",  false); }
void print(float v)           { print_float_prec_orGroup_(v, 6, false); }
void print(double v)          { print_float_prec_orGroup_((float)v, 6, false); }

void println(int32_t v)       { print_num_orGroup_<int32_t>(v,        "%ld",  true); }
void println(uint32_t v)      { print_num_orGroup_<uint32_t>(v,       "%lu",  true); }
void println(int64_t v)       { print_num_orGroup_<int64_t>(v,        "%lld", true); }
void println(uint64_t v)      { print_num_orGroup_<uint64_t>(v,       "%llu", true); }
void println(long v)          { print_num_orGroup_<long>(v,           "%ld",  true); }
void println(unsigned long v) { print_num_orGroup_<unsigned long>(v,  "%lu",  true); }
void println(float v)         { print_float_prec_orGroup_(v, 6, true); }
void println(double v)        { print_float_prec_orGroup_((float)v, 6, true); }

void print(float v, int d)          { print_float_prec_orGroup_(v, d, false); }
void print(double v, int d)         { print_float_prec_orGroup_((float)v, d, false); }
void println(float v, int d)        { print_float_prec_orGroup_(v, d, true); }
void println(double v, int d)       { print_float_prec_orGroup_((float)v, d, true); }

// printf-style
void printf(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vprintf_enqueue_orGroup_(fmt, ap);
    va_end(ap);
}

// Serial mutex accessor
SemaphoreHandle_t serialMutex() {
    ensureDebugStart_();
    return s_serialMtx;
}

// Grouped printing
void groupStart() {
    ensureDebugStart_();
    ensureGroupInit_();

#if defined(ESP32)
    xSemaphoreTakeRecursive(s_groupGate, portMAX_DELAY);
#else
    xSemaphoreTake(s_groupGate, portMAX_DELAY);
#endif

    s_groupOwner  = curTask_();
    s_groupActive = true;
    groupBufReset_();
}

void groupStop(bool addTrailingNewline) {
    ensureDebugStart_();
    flushGroupToQueue_(addTrailingNewline);
    s_groupActive = false;
    s_groupOwner  = nullptr;

#if defined(ESP32)
    xSemaphoreGiveRecursive(s_groupGate);
#else
    xSemaphoreGive(s_groupGate);
#endif
}

void groupCancel() {
    ensureDebugStart_();
    s_groupActive = false;
    s_groupOwner  = nullptr;
    groupBufReset_();

#if defined(ESP32)
    if (s_groupGate) xSemaphoreGiveRecursive(s_groupGate);
#else
    if (s_groupGate) xSemaphoreGive(s_groupGate);
#endif
}

} // namespace Debug
