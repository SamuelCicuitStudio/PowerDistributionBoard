/**************************************************************
 *  Author      : Tshibangu Samuel
 *  Role        : Freelance Embedded Systems Engineer
 *  Expertise   : Secure IoT Systems, Embedded C++, RTOS, Control Logic
 *  Contact     : tshibsamuel47@gmail.com
 *  Portfolio   : https://www.freelancer.com/u/tshibsamuel477
 *  Phone       : +216 54 429 793
 **************************************************************/

#ifndef UTILS_H
#define UTILS_H

/**
 * @file Utils.h
 * @brief Thread-safe debug printing and LED blink utilities for ESP32/Arduino.
 *
 * Provides:
 *  - Non-blocking, thread-safe debug output via background task + queue.
 *  - Atomic “grouped” printing (Debug::groupStart/Stop/Cancel).
 *  - Optional status LED blink patterns via a dedicated task.
 */

#include "Config.h"

// ===================== Global debug switch =====================

#ifndef DEBUGMODE
#define DEBUGMODE true          ///< Compile-time enable/disable debug output
#endif

#ifndef SERIAL_BAUD_RATE
#define SERIAL_BAUD_RATE 921600 ///< Default Serial baud rate for Debug::begin()
#endif

// ===================== Thread-safe debug API =====================

namespace Debug {
    // Initialization (usually auto-called on first print)
    void begin(unsigned long baud = SERIAL_BAUD_RATE);

    // String output
    void print(const char* s);
    void print(const String& s);
    void print(const __FlashStringHelper* fs);      // F("...")
    void println(const char* s);
    void println(const String& s);
    void println(const __FlashStringHelper* fs);    // F("...")
    void println();                                 // blank line

    // Numeric output
    void print(int32_t v);
    void print(uint32_t v);
    void print(int64_t v);
    void print(uint64_t v);
    void print(long v);
    void print(unsigned long v);
    void print(float v);
    void print(double v);

    // Numeric with precision (Arduino-style)
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

    // Access underlying Serial write mutex (if direct Serial access is needed)
    SemaphoreHandle_t serialMutex();

    // ===== Grouped printing (atomic burst) =====

    /**
     * @brief Start a grouped print section.
     *
     * The calling task becomes the owner; subsequent Debug::print* from this
     * task will append into an internal static buffer until groupStop/Cancel.
     */
    void groupStart();

    /**
     * @brief Flush grouped content as a contiguous burst and release ownership.
     * @param addTrailingNewline If true, appends a newline after the group.
     */
    void groupStop(bool addTrailingNewline = false);

    /**
     * @brief Cancel the current group, discarding buffered content.
     */
    void groupCancel();
}

// ===================== Debug macros =====================

#if DEBUGMODE

    #define DEBUG_PRINT(...)      Debug::print(__VA_ARGS__)
    #define DEBUG_PRINTLN(...)    Debug::println(__VA_ARGS__)
    #define DEBUG_PRINTF(...)     Debug::printf(__VA_ARGS__)

    #ifndef DEBUGGSTART
    #define DEBUGGSTART()         Debug::groupStart()
    #endif

    #ifndef DEBUGGSTOP
    #define DEBUGGSTOP()          Debug::groupStop(false)
    #endif

#else

    #define DEBUG_PRINT(...)      do {} while (0)
    #define DEBUG_PRINTLN(...)    do {} while (0)
    #define DEBUG_PRINTF(...)     do {} while (0)
    #define DEBUGGSTART()         do {} while (0)
    #define DEBUGGSTOP()          do {} while (0)

#endif // DEBUGMODE

#endif // UTILS_H
