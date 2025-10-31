#ifndef UTILS_H
#define UTILS_H

#include "Config.h"

// ===================== Global debug switch =====================
#ifndef DEBUGMODE
#define DEBUGMODE true
#endif
#define SERIAL_BAUD_RATE 921600

// ===================== Thread-safe debug API =====================
namespace Debug {
  // Initialize (auto-called on first print)
  void begin(unsigned long baud = SERIAL_BAUD_RATE);

  // Strings
  void print(const char* s);
  void print(const String& s);
  void print(const __FlashStringHelper* fs);  // F("...")
  void println(const char* s);
  void println(const String& s);
  void println(const __FlashStringHelper* fs); // F("...")
  void println(); // blank line

  // Numbers (exact overloads to avoid ambiguity)
  void print(int32_t v);
  void print(uint32_t v);
  void print(int64_t v);
  void print(uint64_t v);
  void print(long v);
  void print(unsigned long v);
  void print(float v);
  void print(double v);

  // With precision (Arduino-style)
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

  // If you *must* print directly to Serial, you can borrow the mutex.
  SemaphoreHandle_t serialMutex();

  // ===== New: GROUPED PRINTING (atomic bursts) =====
  // Start a print group owned by the current task (blocks other tasks' prints).
  void groupStart();
  // Flush buffered lines as one atomic chunk, then release.
  // addTrailingNewline=false keeps your exact line endings; set true to force one final '\n'.
  void groupStop(bool addTrailingNewline = false);
  // Emergency: drop buffered data and release the group (use only if you catch a logic error).
  void groupCancel();
}

// ===================== Debug macros (ordered, thread-safe) =====================
// Variadic so calls like DEBUG_PRINT(value, 4) work.
#if DEBUGMODE
  #define DEBUG_PRINT(...)     Debug::print(__VA_ARGS__)
  #define DEBUG_PRINTLN(...)   Debug::println(__VA_ARGS__)   // 0 or 1+ args
  #define DEBUG_PRINTF(...)    Debug::printf(__VA_ARGS__)
  // ===== New: simple brackets for atomic prints =====
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

// ===================== LED blinking (ordered, thread-safe) =====================
struct BlinkPattern {
  uint8_t  pin;
  uint16_t onMs;           // time HIGH per pulse
  uint16_t offMs;          // time LOW between pulses
  uint8_t  count;          // pulses
  bool     ensureOutput = true;
};


// Backward compatible: one short pulse
void BlinkStatusLED(uint8_t pin, int durationMs = 100);

// Enqueue a full pattern
void EnqueueBlink(const BlinkPattern& pat);

#endif // UTILS_H
