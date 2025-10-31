#ifndef UTILS_H
#define UTILS_H

#include "Config.h"
#include <Arduino.h>

// ===================== Global debug switch =====================
#ifndef DEBUGMODE
#define DEBUGMODE true
#endif
#ifndef SERIAL_BAUD_RATE
#define SERIAL_BAUD_RATE 921600
#endif

// ===================== Debug sizing (tunable) =====================
// Max chars we allow per *single* message allocation (excl. NUL).
#ifndef DBG_LINE_MAX
#define DBG_LINE_MAX       256
#endif

// How many pending log items (pointers) to buffer.
#ifndef DBG_QUEUE_DEPTH
#define DBG_QUEUE_DEPTH    256
#endif

// ===== Fixed-size group buffer (PSRAM) =====
// We allocate this ONCE at startup and never resize.
#ifndef DBG_GROUP_FIXED_CAP
#define DBG_GROUP_FIXED_CAP 8192   // 8 KB by default; adjust as needed
#endif

// Back-compat aliases so older code/macros still compile:
#ifndef DBG_GROUP_INIT_CAP
#define DBG_GROUP_INIT_CAP DBG_GROUP_FIXED_CAP
#endif
#ifndef DBG_GROUP_MAX
#define DBG_GROUP_MAX      DBG_GROUP_FIXED_CAP
#endif

// Blink queue depth
#ifndef BLINK_QUEUE_DEPTH
#define BLINK_QUEUE_DEPTH  16
#endif

// ===================== Thread-safe debug API =====================
namespace Debug {
  void begin(unsigned long baud = SERIAL_BAUD_RATE);

  // Strings
  void print(const char* s);
  void print(const String& s);
  void print(const __FlashStringHelper* fs);
  void println(const char* s);
  void println(const String& s);
  void println(const __FlashStringHelper* fs);
  void println();

  // Numbers (exact overloads)
  void print(int32_t v);
  void print(uint32_t v);
  void print(int64_t v);
  void print(uint64_t v);
  void print(long v);
  void print(unsigned long v);
  void print(float v);
  void print(double v);

  void print(float v, int digits);
  void print(double v, int digits);
  void println(float v, int digits);
  void println(double v, int digits);

  void println(int32_t v);
  void println(uint32_t v);
  void println(int64_t v);
  void println(uint64_t v);
  void println(long v);
  void println(unsigned long v);
  void println(float v);
  void println(double v);

  // printf-style
  void printf(const char* fmt, ...);

  // Borrow Serial mutex if absolutely needed
  SemaphoreHandle_t serialMutex();

  // ===== GROUPED PRINTING =====
  void groupStart();
  void groupStop(bool addTrailingNewline = false);
  void groupCancel();
}

// ===================== Debug macros =====================
#if DEBUGMODE
  #define DEBUG_PRINT(...)     Debug::print(__VA_ARGS__)
  #define DEBUG_PRINTLN(...)   Debug::println(__VA_ARGS__)
  #define DEBUG_PRINTF(...)    Debug::printf(__VA_ARGS__)
  #ifndef DEBUGGSTART
    #define DEBUGGSTART()      Debug::groupStart()
  #endif
  #ifndef DEBUGGSTOP
    #define DEBUGGSTOP()       Debug::groupStop(false)
  #endif
#else
  #define DEBUG_PRINT(...)     do{}while(0)
  #define DEBUG_PRINTLN(...)   do{}while(0)
  #define DEBUG_PRINTF(...)    do{}while(0)
  #define DEBUGGSTART()        do{}while(0)
  #define DEBUGGSTOP()         do{}while(0)
#endif

// ===================== LED blinking =====================
struct BlinkPattern {
  uint8_t  pin;
  uint16_t onMs;
  uint16_t offMs;
  uint8_t  count;
  bool     ensureOutput = true;
};

void BlinkStatusLED(uint8_t pin, int durationMs = 100);
void EnqueueBlink(const BlinkPattern& pat);

#endif // UTILS_H
