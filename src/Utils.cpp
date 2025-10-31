#include "Utils.h"
#include <pgmspace.h>  // for reading F("...") strings from flash
#if defined(ESP32)
  #include "esp_heap_caps.h"   // heap_caps_malloc for PSRAM
#endif

// ===================== Internal config =====================
// Max characters *per* log line (not including trailing newline).
#ifndef DBG_LINE_MAX
#define DBG_LINE_MAX     256
#endif
// Queue depth (items are pointers, so this can be large cheaply).
#ifndef DBG_QUEUE_DEPTH
#define DBG_QUEUE_DEPTH  256
#endif

#ifndef BLINK_QUEUE_DEPTH
#define BLINK_QUEUE_DEPTH  16
#endif

// ---- New: grouping buffer sizing ----
#ifndef DBG_GROUP_INIT_CAP
#define DBG_GROUP_INIT_CAP  512    // initial bytes for a group buffer
#endif
#ifndef DBG_GROUP_MAX
#define DBG_GROUP_MAX       8192   // soft cap before we auto-flush inside a group
#endif

// ===================== Debug implementation =====================
namespace {

  struct DebugMsg {
    uint16_t len;        // bytes in text (not incl. NUL)
    bool     addNewline; // whether to add '\n'
    char     text[];     // flexible tail array (NUL-terminated)
  };

  QueueHandle_t     s_dbgQ        = nullptr;      // queue of DebugMsg*
  TaskHandle_t      s_dbgTask     = nullptr;
  SemaphoreHandle_t s_serialMtx   = nullptr;
  bool              s_started     = false;

  // ---- New: print grouping (atomic bursts) ----
  // recursive gate; owner keeps it for the whole group; others block on prints
  SemaphoreHandle_t s_groupGate   = nullptr;      // recursive mutex
  TaskHandle_t      s_groupOwner  = nullptr;      // task that owns the current group
  bool              s_groupActive = false;
  char*             s_groupBuf    = nullptr;
  size_t            s_groupLen    = 0;
  size_t            s_groupCap    = 0;

  inline TaskHandle_t curTask_() {
#if defined(ESP32)
    return xTaskGetCurrentTaskHandle();
#else
    return (TaskHandle_t)1; // single-thread fallback
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

  void groupBufFree_() {
#if defined(ESP32)
    if (s_groupBuf) heap_caps_free(s_groupBuf);
#else
    if (s_groupBuf) free(s_groupBuf);
#endif
    s_groupBuf = nullptr;
    s_groupLen = 0;
    s_groupCap = 0;
  }

  bool groupBufEnsure_(size_t need) {
    if (need <= s_groupCap) return true;
    size_t newCap = s_groupCap ? s_groupCap : DBG_GROUP_INIT_CAP;
    while (newCap < need) newCap <<= 1;
    if (newCap > DBG_GROUP_MAX) newCap = DBG_GROUP_MAX; // clamp; we will auto-flush if still not enough

    void* mem = nullptr;
#if defined(ESP32)
    mem = heap_caps_realloc(s_groupBuf, newCap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!mem) mem = heap_caps_realloc(s_groupBuf, newCap, MALLOC_CAP_8BIT);
#else
    mem = realloc(s_groupBuf, newCap);
#endif
    if (!mem) return false;
    s_groupBuf = (char*)mem;
    s_groupCap = newCap;
    return true;
  }

  // Allocate a DebugMsg in PSRAM if available; fallback to internal heap.
  DebugMsg* allocMsg_(size_t payloadLen, bool addNewline) {
    if (payloadLen > (DBG_LINE_MAX - 1)) payloadLen = DBG_LINE_MAX - 1;
    const size_t bytes = sizeof(DebugMsg) + payloadLen + 1;
    void* mem = nullptr;

#if defined(ESP32)
    // Prefer SPIRAM 8-bit capable; fallback to default heap if PSRAM unavailable.
    mem = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!mem) mem = heap_caps_malloc(bytes, MALLOC_CAP_8BIT);
#else
    mem = malloc(bytes);
#endif

    if (!mem) return nullptr;
    DebugMsg* m = reinterpret_cast<DebugMsg*>(mem);
    m->len = (uint16_t)payloadLen;
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

  // Enqueue with drop-oldest policy (never blocks).
  void enqueuePtr_(DebugMsg* m) {
    if (!s_dbgQ) { freeMsg_(m); return; }
    if (xQueueSend(s_dbgQ, &m, 0) == pdTRUE) return;

    // Queue full: drop oldest then try again.
    DebugMsg* old = nullptr;
    if (xQueueReceive(s_dbgQ, &old, 0) == pdTRUE) {
      freeMsg_(old);
      if (xQueueSend(s_dbgQ, &m, 0) == pdTRUE) return;
    }

    // Still full (extreme case): drop the new one.
    freeMsg_(m);
  }

  // Ensure task/queue/mutex; start Serial lazily.
  void ensureDebugStart_(unsigned long baud = 115200) {
    if (s_started) return;

    if (!s_serialMtx) s_serialMtx = xSemaphoreCreateMutex();
    if (!s_dbgQ)      s_dbgQ      = xQueueCreate(DBG_QUEUE_DEPTH, sizeof(DebugMsg*));
    ensureGroupInit_();

    if (!s_dbgTask) {
      xTaskCreatePinnedToCore(
        [](void*){
          DebugMsg* p = nullptr;
          for (;;) {
            if (xQueueReceive(s_dbgQ, &p, portMAX_DELAY) == pdTRUE) {
              if (p) {
                if (s_serialMtx) xSemaphoreTake(s_serialMtx, portMAX_DELAY);
                Serial.write(reinterpret_cast<const uint8_t*>(p->text), p->len);
                if (p->addNewline) Serial.write((const uint8_t*)"\n", 1);
                if (s_serialMtx) xSemaphoreGive(s_serialMtx);
                freeMsg_(p);
              }
            }
          }
        },
        "DebugPrintTask", 4096, nullptr, 1, &s_dbgTask, tskNO_AFFINITY
      );
    }

    // Start Serial if user didn't already.
    if (!Serial) Serial.begin(baud);

    s_started = true;
  }

  // ---- New: grouped flush (send the current buffer as one chunk) ----
  void flushGroupToQueue_(bool addTrailingNewline) {
    if (!s_groupActive || !s_groupBuf || s_groupLen == 0) {
      if (addTrailingNewline) {
        // even if empty, allow caller to force a newline (rare)
        DebugMsg* m = allocMsg_(0, true);
        if (m) enqueuePtr_(m);
      }
      return;
    }
    DebugMsg* m = allocMsg_(s_groupLen, /*addNewline*/false);
    if (!m) { s_groupLen = 0; return; }
    memcpy(m->text, s_groupBuf, s_groupLen);
    m->text[s_groupLen] = '\0';
    enqueuePtr_(m);
    if (addTrailingNewline) {
      DebugMsg* nl = allocMsg_(0, true);
      if (nl) enqueuePtr_(nl);
    }
    s_groupLen = 0; // keep buffer for reuse within same group
  }

  // ---- New: append into the current group (auto-flush if too big) ----
  void groupAppend_(const char* data, size_t n, bool addNl) {
    if (!data) { data = ""; n = 0; }
    size_t need = s_groupLen + n + (addNl ? 1 : 0) + 1; // +1 for NUL (not sent)
    if (need > DBG_GROUP_MAX) {
      // Flush what we have so far, then start fresh.
      flushGroupToQueue_(false);
      s_groupLen = 0;
    }
    // Ensure capacity for the remaining piece
    need = (s_groupLen + n + (addNl ? 1 : 0) + 1);
    if (!groupBufEnsure_(need)) return;

    if (n) {
      memcpy(s_groupBuf + s_groupLen, data, n);
      s_groupLen += n;
    }
    if (addNl) s_groupBuf[s_groupLen++] = '\n';
    s_groupBuf[s_groupLen] = '\0';
  }

  inline bool iOwnGroup_() {
    return s_groupActive && (curTask_() == s_groupOwner);
  }

  // Copy C-string into PSRAM msg and enqueue OR group-buffer.
  inline void enqueueStr_orGroup_(const char* s, bool nl) {
    ensureDebugStart_();

#if defined(ESP32)
    if (s_groupGate) xSemaphoreTakeRecursive(s_groupGate, portMAX_DELAY);
    else if (s_groupActive) vTaskDelay(1);
#else
    if (s_groupGate) xSemaphoreTake(s_groupGate, portMAX_DELAY);
#endif

    if (iOwnGroup_()) {
      const size_t n = s ? strnlen(s, DBG_LINE_MAX - 1) : 0;
      groupAppend_(s, n, nl);
    } else {
      const size_t n = s ? strnlen(s, DBG_LINE_MAX - 1) : 0;
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

  // Copy flash-stored string (F("...")) into PSRAM msg and enqueue OR group-buffer.
  inline void enqueueFlash_orGroup_(const __FlashStringHelper* fs, bool nl) {
    ensureDebugStart_();
    if (!fs) return;

#if defined(ESP32)
    if (s_groupGate) xSemaphoreTakeRecursive(s_groupGate, portMAX_DELAY);
    else if (s_groupActive) vTaskDelay(1);
#else
    if (s_groupGate) xSemaphoreTake(s_groupGate, portMAX_DELAY);
#endif

    if (iOwnGroup_()) {
      // Measure up to limit
      const char* p = reinterpret_cast<const char*>(fs);
      size_t n = 0;
      while (n < DBG_LINE_MAX - 1) {
        uint8_t c = pgm_read_byte(p + n);
        if (!c) break;
        ++n;
      }
      // Copy from flash into the group buffer
      if (n) {
        // small temp stack buffer to pull from flash
        char tmp[DBG_LINE_MAX];
        for (size_t i = 0; i < n; ++i) tmp[i] = (char)pgm_read_byte(p + i);
        groupAppend_(tmp, n, nl);
      } else if (nl) {
        groupAppend_("", 0, true);
      }
    } else {
      // Not grouped by me → normal enqueue
      const char* p = reinterpret_cast<const char*>(fs);
      size_t n = 0;
      while (n < DBG_LINE_MAX - 1) {
        uint8_t c = pgm_read_byte(p + n);
        if (!c) break;
        ++n;
      }
      DebugMsg* m = allocMsg_(n, nl);
      if (m) {
        for (size_t i = 0; i < n; ++i) m->text[i] = (char)pgm_read_byte(p + i);
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

  // vprintf into a temporary stack buffer, then PSRAM-copy or group-buffer.
  inline void vprintf_enqueue_orGroup_(const char* fmt, va_list ap) {
    ensureDebugStart_();
    char buf[DBG_LINE_MAX];
    vsnprintf(buf, sizeof(buf), fmt ? fmt : "", ap);

#if defined(ESP32)
    if (s_groupGate) xSemaphoreTakeRecursive(s_groupGate, portMAX_DELAY);
    else if (s_groupActive) vTaskDelay(1);
#else
    if (s_groupGate) xSemaphoreTake(s_groupGate, portMAX_DELAY);
#endif

    if (iOwnGroup_()) {
      groupAppend_(buf, strnlen(buf, DBG_LINE_MAX - 1), /*nl*/false);
    } else {
      const size_t n = strnlen(buf, DBG_LINE_MAX - 1);
      DebugMsg* m = allocMsg_(n, /*nl*/false);
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
    if (digits < 0) digits = 0;
    if (digits > 8) digits = 8;   // clamp
    char buf[DBG_LINE_MAX];
    dtostrf(v, 0, digits, buf);
    enqueueStr_orGroup_(buf, nl);
  }
} // namespace

namespace Debug {
  void begin(unsigned long baud) { ensureDebugStart_(baud); }

  // strings
  void print(const char* s)                    { enqueueStr_orGroup_(s, false); }
  void print(const String& s)                  { enqueueStr_orGroup_(s.c_str(), false); }
  void print(const __FlashStringHelper* fs)    { enqueueFlash_orGroup_(fs, false); }
  void println(const char* s)                  { enqueueStr_orGroup_(s, true); }
  void println(const String& s)                { enqueueStr_orGroup_(s.c_str(), true); }
  void println(const __FlashStringHelper* fs)  { enqueueFlash_orGroup_(fs, true); }
  void println()                               { enqueueStr_orGroup_("", true); }  // blank line

  // numbers
  void print(int32_t v)          { print_num_orGroup_<int32_t>(v,          "%ld",  false); }
  void print(uint32_t v)         { print_num_orGroup_<uint32_t>(v,         "%lu",  false); }
  void print(int64_t v)          { print_num_orGroup_<int64_t>(v,          "%lld", false); }
  void print(uint64_t v)         { print_num_orGroup_<uint64_t>(v,         "%llu", false); }
  void print(long v)             { print_num_orGroup_<long>(v,             "%ld",  false); }
  void print(unsigned long v)    { print_num_orGroup_<unsigned long>(v,    "%lu",  false); }
  void print(float v)            { print_float_prec_orGroup_(v, 6, false); }
  void print(double v)           { print_float_prec_orGroup_((float)v, 6, false); }

  // with precision (Arduino-style)
  void print(float v, int digits)    { print_float_prec_orGroup_(v, digits, false); }
  void print(double v, int digits)   { print_float_prec_orGroup_((float)v, digits, false); }
  void println(float v, int digits)  { print_float_prec_orGroup_(v, digits, true); }
  void println(double v, int digits) { print_float_prec_orGroup_((float)v, digits, true); }

  void println(int32_t v)        { print_num_orGroup_<int32_t>(v,          "%ld",  true); }
  void println(uint32_t v)       { print_num_orGroup_<uint32_t>(v,         "%lu",  true); }
  void println(int64_t v)        { print_num_orGroup_<int64_t>(v,          "%lld", true); }
  void println(uint64_t v)       { print_num_orGroup_<uint64_t>(v,         "%llu", true); }
  void println(long v)           { print_num_orGroup_<long>(v,             "%ld",  true); }
  void println(unsigned long v)  { print_num_orGroup_<unsigned long>(v,    "%lu",  true); }
  void println(float v)          { print_float_prec_orGroup_(v, 6, true); }
  void println(double v)         { print_float_prec_orGroup_((float)v, 6, true); }

  void printf(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    vprintf_enqueue_orGroup_(fmt, ap);
    va_end(ap);
  }

  SemaphoreHandle_t serialMutex() { ensureDebugStart_(); return s_serialMtx; }

  // ===== Grouping API =====
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
    groupBufFree_();
    groupBufEnsure_(DBG_GROUP_INIT_CAP);
  }

  void groupStop(bool addTrailingNewline) {
    ensureDebugStart_();
    // owner (or single-thread) flush
    flushGroupToQueue_(addTrailingNewline);
    s_groupActive = false;
    s_groupOwner  = nullptr;
    groupBufFree_();
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
    groupBufFree_();
#if defined(ESP32)
    if (s_groupGate) xSemaphoreGiveRecursive(s_groupGate);
#else
    if (s_groupGate) xSemaphoreGive(s_groupGate);
#endif
  }
} // namespace Debug

// ===================== LED blink implementation =====================
namespace {
  struct BlinkCmd {
    uint8_t  pin;
    uint16_t onMs;
    uint16_t offMs;
    uint8_t  count;
    bool     ensureOutput;
  };

  QueueHandle_t s_blinkQ    = nullptr;
  TaskHandle_t  s_blinkTask = nullptr;

  void ensureBlinkStart_() {
    if (!s_blinkQ)   s_blinkQ = xQueueCreate(BLINK_QUEUE_DEPTH, sizeof(BlinkCmd));
    if (!s_blinkTask) {
      xTaskCreatePinnedToCore(
        [](void*){
          BlinkCmd cmd;
          for (;;) {
            if (xQueueReceive(s_blinkQ, &cmd, portMAX_DELAY) == pdTRUE) {
              if (cmd.ensureOutput) pinMode(cmd.pin, OUTPUT);
              for (uint8_t i = 0; i < cmd.count; ++i) {
                digitalWrite(cmd.pin, HIGH);
                vTaskDelay(pdMS_TO_TICKS(cmd.onMs));
                digitalWrite(cmd.pin, LOW);
                if (i + 1 < cmd.count) {
                  vTaskDelay(pdMS_TO_TICKS(cmd.offMs));
                }
              }
            }
          }
        },
        "BlinkTask", 2048, nullptr, 1, &s_blinkTask, tskNO_AFFINITY
      );
    }
  }
}

void BlinkStatusLED(uint8_t pin, int durationMs) {
  ensureBlinkStart_();
  BlinkCmd c{ pin, (uint16_t)durationMs, 0 /*offMs*/, 1 /*count*/, true };
  (void)xQueueSend(s_blinkQ, &c, 0);
}

void EnqueueBlink(const BlinkPattern& pat) {
  ensureBlinkStart_();
  BlinkCmd c{ pat.pin, pat.onMs, pat.offMs, pat.count, pat.ensureOutput };
  (void)xQueueSend(s_blinkQ, &c, 0);
}
