#include "Utils.h"
#include <pgmspace.h>
#if defined(ESP32)
  #include "esp_heap_caps.h"
#endif
#include <stdarg.h>

// ===================== Internal: Debug plumbing =====================
namespace {

  struct DebugMsg {
    uint16_t len;        // bytes in text (not incl. NUL)
    bool     addNewline; // whether to add '\n'
    char     text[];     // flexible tail array (NUL-terminated)
  };

  QueueHandle_t     s_dbgQ        = nullptr;
  TaskHandle_t      s_dbgTask     = nullptr;
  SemaphoreHandle_t s_serialMtx   = nullptr;
  bool              s_started     = false;

  // Grouping (atomic bursts)
  SemaphoreHandle_t s_groupGate   = nullptr;      // recursive mutex on ESP32
  TaskHandle_t      s_groupOwner  = nullptr;
  bool              s_groupActive = false;

  // ======= FIXED PSRAM GROUP BUFFER (no resize) =======
  char*             s_groupBuf    = nullptr;      // PSRAM-preferred
  size_t            s_groupLen    = 0;
  size_t            s_groupCap    = 0;           // == DBG_GROUP_FIXED_CAP once allocated

  inline TaskHandle_t curTask_() {
  #if defined(ESP32)
    return xTaskGetCurrentTaskHandle();
  #else
    return (TaskHandle_t)1;
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

  // Fixed allocation at startup; never resized/freed afterwards.
  void ensureFixedGroupBuf_() {
    if (s_groupBuf) return;
    s_groupCap = DBG_GROUP_FIXED_CAP;
  #if defined(ESP32)
    s_groupBuf = (char*)heap_caps_malloc(s_groupCap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_groupBuf) s_groupBuf = (char*)heap_caps_malloc(s_groupCap, MALLOC_CAP_8BIT);
  #else
    s_groupBuf = (char*)malloc(s_groupCap);
  #endif
    if (!s_groupBuf) {
      s_groupCap = 0; // grouping will degrade to per-line enqueue
    } else {
      s_groupLen = 0;
      s_groupBuf[0] = '\0';
    }
  }

  // No resize; just check capacity.
  inline bool groupCapOK_(size_t need) {
    return s_groupBuf && (need <= s_groupCap);
  }

  // Prefer PSRAM for messages; fallback to internal heap.
  DebugMsg* allocMsg_(size_t payloadLen, bool addNewline) {
    if (payloadLen > (DBG_LINE_MAX - 1)) payloadLen = DBG_LINE_MAX - 1;
    const size_t bytes = sizeof(DebugMsg) + payloadLen + 1;
    void* mem = nullptr;
  #if defined(ESP32)
    mem = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!mem) mem = heap_caps_malloc(bytes, MALLOC_CAP_8BIT);
  #else
    mem = malloc(bytes);
  #endif
    if (!mem) return nullptr;
    auto* m = reinterpret_cast<DebugMsg*>(mem);
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

  // Enqueue pointer with drop-oldest policy (non-blocking)
  void enqueuePtr_(DebugMsg* m) {
    if (!s_dbgQ) { freeMsg_(m); return; }
    if (xQueueSend(s_dbgQ, &m, 0) == pdTRUE) return;

    // If full, drop the oldest entry, then push the newest (newest-wins).
    DebugMsg* old = nullptr;
    if (xQueueReceive(s_dbgQ, &old, 0) == pdTRUE) {
      freeMsg_(old);
      if (xQueueSend(s_dbgQ, &m, 0) == pdTRUE) return;
    }
    freeMsg_(m); // drop if still full
  }

  void ensureDebugStart_(unsigned long baud = SERIAL_BAUD_RATE) {
    if (s_started) return;

    if (!s_serialMtx) s_serialMtx = xSemaphoreCreateMutex();
    if (!s_dbgQ)      s_dbgQ      = xQueueCreate(DBG_QUEUE_DEPTH, sizeof(DebugMsg*));
    ensureGroupInit_();
    ensureFixedGroupBuf_();  // << allocate fixed PSRAM group buffer once

    if (!s_dbgTask) {
      xTaskCreatePinnedToCore(
        [](void*) {
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

    if (!Serial) Serial.begin(baud);
    s_started = true;
  }

  inline bool iOwnGroup_() {
    return s_groupActive && (curTask_() == s_groupOwner);
  }

  // Flush grouped buffer as one message (and keep buffer for reuse)
  void flushGroupToQueue_(bool addTrailingNewline) {
    if (!s_groupActive || !s_groupBuf || s_groupLen == 0) {
      if (addTrailingNewline) {
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
    s_groupLen = 0; // keep buffer for reuse (fixed allocation)
    s_groupBuf[0] = '\0';
  }

  // Append into fixed-size group; if would exceed cap, flush first.
  void groupAppend_(const char* data, size_t n, bool addNl) {
    if (!data) { data = ""; n = 0; }
    size_t need = s_groupLen + n + (addNl ? 1 : 0) + 1;

    // If this append would overflow the fixed buffer, flush current group first.
    if (!groupCapOK_(need)) {
      flushGroupToQueue_(false);
      need = n + (addNl ? 1 : 0) + 1;
    }

    // If a *single* payload still won’t fit the fixed buffer
    // (extremely rare; large formatted line), skip grouping and enqueue directly.
    if (!groupCapOK_(need)) {
      DebugMsg* m = allocMsg_(n, /*nl*/addNl);
      if (m) {
        if (n) memcpy(m->text, data, n);
        m->text[n] = '\0';
        enqueuePtr_(m); // queue drop-oldest will apply if full
      }
      return;
    }

    // Normal grouped append
    if (n) {
      memcpy(s_groupBuf + s_groupLen, data, n);
      s_groupLen += n;
    }
    if (addNl) s_groupBuf[s_groupLen++] = '\n';
    s_groupBuf[s_groupLen] = '\0';
  }

  // Enqueue or group a C-string
  inline void enqueueStr_orGroup_(const char* s, bool nl) {
    ensureDebugStart_();
  #if defined(ESP32)
    if (s_groupGate) xSemaphoreTakeRecursive(s_groupGate, portMAX_DELAY);
    else if (s_groupActive) vTaskDelay(1);
  #else
    if (s_groupGate) xSemaphoreTake(s_groupGate, portMAX_DELAY);
  #endif

    const size_t n = s ? strnlen(s, DBG_LINE_MAX - 1) : 0;
    if (iOwnGroup_() && s_groupBuf) {
      groupAppend_(s, n, nl);
    } else {
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

  // Enqueue or group a PROGMEM string with no stack tmp
  inline void enqueueFlash_orGroup_(const __FlashStringHelper* fs, bool nl) {
    ensureDebugStart_();
    if (!fs) return;

    const char* p = reinterpret_cast<const char*>(fs);
    // Measure up to line limit
    size_t n = 0;
    while (n < DBG_LINE_MAX - 1) {
      uint8_t c = pgm_read_byte(p + n);
      if (!c) break;
      ++n;
    }

  #if defined(ESP32)
    if (s_groupGate) xSemaphoreTakeRecursive(s_groupGate, portMAX_DELAY);
    else if (s_groupActive) vTaskDelay(1);
  #else
    if (s_groupGate) xSemaphoreTake(s_groupGate, portMAX_DELAY);
  #endif

    if (iOwnGroup_() && s_groupBuf) {
      // Copy byte-wise from flash into fixed buffer
      size_t need = s_groupLen + n + (nl ? 1 : 0) + 1;
      if (!groupCapOK_(need)) {
        flushGroupToQueue_(false);
        need = n + (nl ? 1 : 0) + 1;
      }

      if (!groupCapOK_(need)) {
        // Cannot fit even after flush: enqueue a direct message instead.
        DebugMsg* m = allocMsg_(n, nl);
        if (m) {
          for (size_t i = 0; i < n; ++i) m->text[i] = (char)pgm_read_byte(p + i);
          m->text[n] = '\0';
          enqueuePtr_(m);
        }
      } else {
        for (size_t i = 0; i < n; ++i) s_groupBuf[s_groupLen++] = (char)pgm_read_byte(p + i);
        if (nl) s_groupBuf[s_groupLen++] = '\n';
        s_groupBuf[s_groupLen] = '\0';
      }

    } else {
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

  // printf into PSRAM (no stack temp). Two-pass: measure → print.
  inline void vprintf_enqueue_orGroup_(const char* fmt, va_list ap) {
    ensureDebugStart_();

  #if defined(ESP32)
    if (s_groupGate) xSemaphoreTakeRecursive(s_groupGate, portMAX_DELAY);
    else if (s_groupActive) vTaskDelay(1);
  #else
    if (s_groupGate) xSemaphoreTake(s_groupGate, portMAX_DELAY);
  #endif

    if (iOwnGroup_() && s_groupBuf) {
      va_list ap2; va_copy(ap2, ap);
      int need = vsnprintf(nullptr, 0, fmt ? fmt : "", ap2);
      va_end(ap2);
      if (need < 0) need = 0;
      size_t total = s_groupLen + (size_t)need + 1;

      if (!groupCapOK_(total)) {
        // flush current group and retry once
        flushGroupToQueue_(false);
        total = (size_t)need + 1;
      }

      if (!groupCapOK_(total)) {
        // still too big for the fixed buffer → bypass grouping
        va_list ap3; va_copy(ap3, ap);
        DebugMsg* m = allocMsg_((size_t)need, /*nl*/false);
        if (m) {
          vsnprintf(m->text, need + 1, fmt ? fmt : "", ap3);
          m->len = (uint16_t)strnlen(m->text, (size_t)need);
          enqueuePtr_(m); // drop-oldest if queue full
        }
        va_end(ap3);
      } else {
        int written = vsnprintf(s_groupBuf + s_groupLen, (int)(s_groupCap - s_groupLen), fmt ? fmt : "", ap);
        if (written > 0) s_groupLen += (size_t)written;
        s_groupBuf[s_groupLen] = '\0';
      }

    } else {
      va_list ap2; va_copy(ap2, ap);
      int need = vsnprintf(nullptr, 0, fmt ? fmt : "", ap2);
      va_end(ap2);
      if (need < 0) need = 0;
      DebugMsg* m = allocMsg_((size_t)need, /*nl*/false);
      if (m) {
        vsnprintf(m->text, need + 1, fmt ? fmt : "", ap);
        m->len = (uint16_t)strnlen(m->text, (size_t)need);
        enqueuePtr_(m);
      }
    }

  #if defined(ESP32)
    if (s_groupGate) xSemaphoreGiveRecursive(s_groupGate);
  #else
    if (s_groupGate) xSemaphoreGive(s_groupGate);
  #endif
  }

  // Numbers → format without big stack frames
  template<typename T>
  inline void print_num_orGroup_(T v, const char* fmt_no_nl, bool nl) {
    ensureDebugStart_();
    // Measure length
    int need = snprintf(nullptr, 0, fmt_no_nl, v);
    if (need < 0) need = 0;
    size_t n = (size_t)need;

  #if defined(ESP32)
    if (s_groupGate) xSemaphoreTakeRecursive(s_groupGate, portMAX_DELAY);
    else if (s_groupActive) vTaskDelay(1);
  #else
    if (s_groupGate) xSemaphoreTake(s_groupGate, portMAX_DELAY);
  #endif

    if (iOwnGroup_() && s_groupBuf) {
      size_t total = s_groupLen + n + (nl ? 1 : 0) + 1;
      if (!groupCapOK_(total)) {
        flushGroupToQueue_(false);
        total = n + (nl ? 1 : 0) + 1;
      }
      if (!groupCapOK_(total)) {
        // too big → bypass group
        DebugMsg* m = allocMsg_(n, nl);
        if (m) {
          snprintf(m->text, (int)(n + 1), fmt_no_nl, v);
          m->len = (uint16_t)strnlen(m->text, n);
          enqueuePtr_(m);
        }
      } else {
        int written = snprintf(s_groupBuf + s_groupLen, (int)(s_groupCap - s_groupLen), fmt_no_nl, v);
        if (written > 0) s_groupLen += (size_t)written;
        if (nl) s_groupBuf[s_groupLen++] = '\n';
        s_groupBuf[s_groupLen] = '\0';
      }

    } else {
      DebugMsg* m = allocMsg_(n, nl);
      if (m) {
        snprintf(m->text, (int)(n + 1), fmt_no_nl, v);
        m->len = (uint16_t)strnlen(m->text, n);
        enqueuePtr_(m);
      }
    }

  #if defined(ESP32)
    if (s_groupGate) xSemaphoreGiveRecursive(s_groupGate);
  #else
    if (s_groupGate) xSemaphoreGive(s_groupGate);
  #endif
  }

  inline void print_float_prec_orGroup_(float v, int digits, bool nl) {
    ensureDebugStart_();
    if (digits < 0) digits = 0;
    if (digits > 8) digits = 8;
    size_t cap = (size_t)(digits + 16);
    if (cap < 24) cap = 24;

    char* tmp = nullptr;
  #if defined(ESP32)
    tmp = (char*)heap_caps_malloc(cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!tmp) tmp = (char*)heap_caps_malloc(cap, MALLOC_CAP_8BIT);
  #else
    tmp = (char*)malloc(cap);
  #endif
    if (!tmp) return;

    dtostrf(v, 0, digits, tmp);
    enqueueStr_orGroup_(tmp, nl);

  #if defined(ESP32)
    heap_caps_free(tmp);
  #else
    free(tmp);
  #endif
  }

} // namespace

// ===================== Public Debug API =====================
namespace Debug {
  void begin(unsigned long baud) { ensureDebugStart_(baud); }

  // strings
  void print(const char* s)                    { enqueueStr_orGroup_(s, false); }
  void print(const String& s)                  { enqueueStr_orGroup_(s.c_str(), false); }
  void print(const __FlashStringHelper* fs)    { enqueueFlash_orGroup_(fs, false); }
  void println(const char* s)                  { enqueueStr_orGroup_(s, true); }
  void println(const String& s)                { enqueueStr_orGroup_(s.c_str(), true); }
  void println(const __FlashStringHelper* fs)  { enqueueFlash_orGroup_(fs, true); }
  void println()                               { enqueueStr_orGroup_("", true); }

  // numbers
  void print(int32_t v)          { print_num_orGroup_<int32_t>(v,          "%ld",  false); }
  void print(uint32_t v)         { print_num_orGroup_<uint32_t>(v,         "%lu",  false); }
  void print(int64_t v)          { print_num_orGroup_<int64_t>(v,          "%lld", false); }
  void print(uint64_t v)         { print_num_orGroup_<uint64_t>(v,         "%llu", false); }
  void print(long v)             { print_num_orGroup_<long>(v,             "%ld",  false); }
  void print(unsigned long v)    { print_num_orGroup_<unsigned long>(v,    "%lu",  false); }
  void print(float v)            { print_float_prec_orGroup_(v, 6, false); }
  void print(double v)           { print_float_prec_orGroup_((float)v, 6, false); }

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
    // Do NOT free or resize; just reset length.
    if (s_groupBuf) {
      s_groupLen = 0;
      s_groupBuf[0] = '\0';
    }
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
    if (s_groupBuf) {
      s_groupLen = 0;
      s_groupBuf[0] = '\0';
    }
  #if defined(ESP32)
    if (s_groupGate) xSemaphoreGiveRecursive(s_groupGate);
  #else
    if (s_groupGate) xSemaphoreGive(s_groupGate);
  #endif
  }
} // namespace

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
        [](void*) {
          BlinkCmd cmd;
          for (;;) {
            if (xQueueReceive(s_blinkQ, &cmd, portMAX_DELAY) == pdTRUE) {
              if (cmd.ensureOutput) pinMode(cmd.pin, OUTPUT);
              for (uint8_t i = 0; i < cmd.count; ++i) {
                digitalWrite(cmd.pin, HIGH);
                vTaskDelay(pdMS_TO_TICKS(cmd.onMs));
                digitalWrite(cmd.pin, LOW);
                if (i + 1 < cmd.count) vTaskDelay(pdMS_TO_TICKS(cmd.offMs));
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
