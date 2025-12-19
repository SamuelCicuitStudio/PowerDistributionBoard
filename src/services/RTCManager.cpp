/**************************************************************
 *  Author      : Tshibangu Samuel
 *  Role        : Freelance Embedded Systems Engineer
 *  Expertise   : Secure IoT Systems, Embedded C++, RTOS, Control Logic
 *  Contact     : tshibsamuel47@gmail.com
 *  Portfolio   : https://www.freelancer.com/u/tshibsamuel477
 *  Phone       : +216 54 429 793
 **************************************************************/

#include "services/RTCManager.h"

RTCManager* RTCManager::s_instance = nullptr;

namespace {

template<typename T>
inline T clampVal(T v, T lo, T hi) {
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static tm g_fallbackTm{};
static constexpr uint64_t kMinValidEpoch = 1609459200ULL; // 2021-01-01

inline bool isValidEpoch(uint64_t epoch) {
    return epoch >= kMinValidEpoch;
}

inline void persistEpoch(uint64_t epoch, const char* key) {
    if (!CONF || !key) {
        return;
    }
    const uint64_t cur = CONF->GetULong64(
        key,
        static_cast<uint64_t>(RTC_DEFAULT_EPOCH)
    );
    if (cur != epoch) {
        CONF->PutULong64(key, epoch);
    }
}

inline bool safeGetLocalTime(tm* out) {
    if (!out) {
        return false;
    }

    if (getLocalTime(out)) {
        return true;
    }

    const time_t now = time(nullptr);
    if (now <= 0) {
        return false;
    }

    localtime_r(&now, out);
    return true;
}

} // namespace

void RTCManager::Init(struct tm* timeinfo) {
    if (!s_instance) {
        s_instance = new RTCManager(timeinfo);
        return;
    }

    if (timeinfo) {
        // Update working buffer pointer and refresh
        s_instance->timeinfo = timeinfo;
        s_instance->update();
    }
}

RTCManager* RTCManager::Get() {
    if (!s_instance) {
        s_instance = new RTCManager(nullptr);
    }
    return s_instance;
}

RTCManager* RTCManager::TryGet() {
    return s_instance;
}

RTCManager::RTCManager(struct tm* tminfo)
    : timeinfo(tminfo ? tminfo : &g_fallbackTm),
      formattedTime(),
      formattedDate(),
      _mutex(nullptr)
{
    DEBUGGSTART();
    DEBUG_PRINTLN("###########################################################");
    DEBUG_PRINTLN("#                   Starting RTC Manager                  #");
    DEBUG_PRINTLN("###########################################################");
    DEBUGGSTOP();

    _mutex = xSemaphoreCreateMutex();

    const uint64_t saved = (CONF)
        ? CONF->GetULong64(
              RTC_CURRENT_EPOCH_KEY,
              static_cast<uint64_t>(RTC_DEFAULT_EPOCH))
        : static_cast<uint64_t>(RTC_DEFAULT_EPOCH);

    setUnixTime(static_cast<unsigned long>(saved));
    update();
}

void RTCManager::setUnixTime(unsigned long timestamp) {
    MutexGuard g(_mutex, pdMS_TO_TICKS(1000));
    if (!g.ok()) {
        DEBUG_PRINTLN("[RTC] setUnixTime lock timeout");
        return;
    }

    if (!isValidEpoch(static_cast<uint64_t>(timestamp))) {
        DEBUG_PRINTF("[RTC] Ignoring invalid epoch: %lu\n",
                     static_cast<unsigned long>(timestamp));
        return;
    }

    DEBUGGSTART();
    DEBUG_PRINT("[RTC] Setting system time from Unix timestamp: ");
    DEBUG_PRINT(timestamp);
    DEBUG_PRINTLN("");
    DEBUGGSTOP();

    struct timeval tv;
    tv.tv_sec  = static_cast<time_t>(timestamp);
    tv.tv_usec = 0;
    settimeofday(&tv, nullptr);

    persistEpoch(static_cast<uint64_t>(timestamp), RTC_CURRENT_EPOCH_KEY);

    DEBUGGSTART();
    DEBUG_PRINT("[RTC] System time set to: ");
    DEBUG_PRINT(timestamp);
    DEBUG_PRINTLN("");
    DEBUGGSTOP();
}

unsigned long RTCManager::getUnixTime() {
    MutexGuard g(_mutex, pdMS_TO_TICKS(200));
    if (!g.ok()) {
        // Fallback without touching shared state
        const time_t now = time(nullptr);
        return (now > 0 && isValidEpoch(static_cast<uint64_t>(now)))
                   ? static_cast<unsigned long>(now)
                   : 0;
    }

    tm snapshot{};
    if (safeGetLocalTime(&snapshot)) {
        const time_t now = mktime(&snapshot);

        if (!isValidEpoch(static_cast<uint64_t>(now))) {
            DEBUG_PRINTLN("[RTC] Time not set; returning 0");
            *timeinfo = snapshot;
            return 0;
        }

        DEBUGGSTART();
        DEBUG_PRINT("[RTC] Current Unix time: ");
        DEBUG_PRINT(static_cast<unsigned long>(now));
        DEBUG_PRINTLN("");
        DEBUGGSTOP();

        // Keep working buffer in sync
        *timeinfo = snapshot;

        return static_cast<unsigned long>(now);
    }

    const time_t now = time(nullptr);
    if (now > 0 && isValidEpoch(static_cast<uint64_t>(now))) {
        DEBUGGSTART();
        DEBUG_PRINT("[RTC] Current Unix time (fallback): ");
        DEBUG_PRINT(static_cast<unsigned long>(now));
        DEBUG_PRINTLN("");
        DEBUGGSTOP();
        return static_cast<unsigned long>(now);
    }

    DEBUG_PRINTLN("[RTC] Failed to get current Unix time.");
    return 0;
}

String RTCManager::getTime() {
    MutexGuard g(_mutex, pdMS_TO_TICKS(100));
    if (!g.ok()) {
        return String();
    }
    return formattedTime;
}

String RTCManager::getDate() {
    MutexGuard g(_mutex, pdMS_TO_TICKS(100));
    if (!g.ok()) {
        return String();
    }
    return formattedDate;
}

void RTCManager::update() {
    MutexGuard g(_mutex, pdMS_TO_TICKS(200));
    if (!g.ok()) {
        DEBUG_PRINTLN("[RTC] update lock timeout");
        return;
    }

    tm tmp{};
    if (!safeGetLocalTime(&tmp)) {
        DEBUG_PRINTLN("[RTC] Failed to get local time.");
        return;
    }

    // Keep working tm in sync
    *timeinfo = tmp;

    const time_t epoch = mktime(&tmp);
    if (!isValidEpoch(static_cast<uint64_t>(epoch))) {
        const char* invalidTime = "--:--";
        const char* invalidDate = "---- -- --";
        if (formattedTime != invalidTime) {
            formattedTime = String(invalidTime);
            DEBUG_PRINTLN("[RTC] Time not set");
        }
        if (formattedDate != invalidDate) {
            formattedDate = String(invalidDate);
            DEBUG_PRINTLN("[RTC] Date not set");
        }
        return;
    }

    char timeString[6];   // "HH:MM"
    char dateString[11];  // "YYYY-MM-DD"

    snprintf(timeString,
             sizeof(timeString),
             "%02d:%02d",
             tmp.tm_hour,
             tmp.tm_min);

    snprintf(dateString,
             sizeof(dateString),
             "%04d-%02d-%02d",
             tmp.tm_year + 1900,
             tmp.tm_mon + 1,
             tmp.tm_mday);

    if (formattedTime != timeString) {
        formattedTime = String(timeString);
        DEBUGGSTART();
        DEBUG_PRINT("[RTC] Updated time: ");
        DEBUG_PRINT(formattedTime);
        DEBUG_PRINTLN("");
        DEBUGGSTOP();
    }

    if (formattedDate != dateString) {
        formattedDate = String(dateString);
        DEBUGGSTART();
        DEBUG_PRINT("[RTC] Updated date: ");
        DEBUG_PRINT(formattedDate);
        DEBUG_PRINTLN("");
        DEBUGGSTOP();
    }
}

void RTCManager::setRTCTime(int year,
                            int month,
                            int day,
                            int hour,
                            int minute,
                            int second) {
    MutexGuard g(_mutex, pdMS_TO_TICKS(1000));
    if (!g.ok()) {
        DEBUG_PRINTLN("[RTC] setRTCTime lock timeout");
        return;
    }

    DEBUGGSTART();
    DEBUG_PRINT("[RTC] Setting RTC time to: ");
    DEBUG_PRINT("Year: ");    DEBUG_PRINT(year);
    DEBUG_PRINT(", Month: "); DEBUG_PRINT(month);
    DEBUG_PRINT(", Day: ");   DEBUG_PRINT(day);
    DEBUG_PRINT(", Hour: ");  DEBUG_PRINT(hour);
    DEBUG_PRINT(", Minute: ");DEBUG_PRINT(minute);
    DEBUG_PRINT(", Second: ");DEBUG_PRINT(second);
    DEBUG_PRINTLN("");
    DEBUGGSTOP();

    year   = clampVal(year,   1970, 2099);
    month  = clampVal(month,     1,   12);
    day    = clampVal(day,       1,   31);
    hour   = clampVal(hour,      0,   23);
    minute = clampVal(minute,    0,   59);
    second = clampVal(second,    0,   59);

    timeinfo->tm_year = year - 1900;
    timeinfo->tm_mon  = month - 1;
    timeinfo->tm_mday = day;
    timeinfo->tm_hour = hour;
    timeinfo->tm_min  = minute;
    timeinfo->tm_sec  = second;

    const time_t epoch = mktime(timeinfo);

    struct timeval tv;
    tv.tv_sec  = epoch;
    tv.tv_usec = 0;
    settimeofday(&tv, nullptr);

    persistEpoch(static_cast<uint64_t>(epoch), RTC_CURRENT_EPOCH_KEY);

    // Intentionally do NOT call update() here to avoid nested locking
    // or unexpected formatted string changes mid-call. Call update()
    // explicitly after setRTCTime() if needed.
}
