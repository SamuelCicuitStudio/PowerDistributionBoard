/**************************************************************
 *  Author      : Tshibangu Samuel
 *  Role        : Freelance Embedded Systems Engineer
 *  Expertise   : Secure IoT Systems, Embedded C++, RTOS, Control Logic
 *  Contact     : tshibsamuel47@gmail.com
 *  Portfolio   : https://www.freelancer.com/u/tshibsamuel477
 *  Phone       : +216 54 429 793
 **************************************************************/
#ifndef RGB_CONFIG_H
#define RGB_CONFIG_H

/**************************************************************
 *  Project     : Power Manager
 *  File        : RGBConfig.h
 *  Purpose     : Centralized configuration for the RGB status LED.
 *                - Pure compile-time configuration (no NVS)
 *                - RTOS/Task and queue sizing
 **************************************************************/

/**
 * @file RGBConfig.h
 * @brief Compile-time configuration & palette for the RGB LED.
 *
 * Priorities:
 *   PRIO_BACKGROUND=0, PRIO_ACTION=1, PRIO_ALERT=2, PRIO_CRITICAL=3
 *
 * Queue policy:
 *   Overlays posted with preempt=true interrupt when priority >= current.
 *   When full, alerts (>= PRIO_ALERT) drop the oldest to make room.
 */

// =============================== Core ===============================
#define RGB_TASK_STACK     4096
#define RGB_TASK_PRIORITY  2
#define RGB_CMD_QUEUE_LEN  24

// =============================== Helpers ===============================
#define RGB_HEX(r,g,b)   ( ((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b) )
#define RGB_R(c)         ( (uint8_t)(((c) >> 16) & 0xFF) )
#define RGB_G(c)         ( (uint8_t)(((c) >> 8) & 0xFF) )
#define RGB_B(c)         ( (uint8_t)((c) & 0xFF) )

// =============================== Palette ===============================
#define RGB_RED        RGB_HEX(255,   0,   0)
#define RGB_GREEN      RGB_HEX(  0, 255,   0)
#define RGB_BLUE       RGB_HEX(  0, 140, 255)
#define RGB_AMBER      RGB_HEX(255, 170,   0)
#define RGB_YELLOW     RGB_HEX(255, 230,  40)
#define RGB_TEAL       RGB_HEX(  0, 220, 140)
#define RGB_CYAN       RGB_HEX(  0, 200, 255)
#define RGB_SOFT_WHITE RGB_HEX(220, 220, 220)
#define RGB_OFF        RGB_HEX(  0,   0,   0)

// =============================== Background colors ===============================
// WAIT  : amber heartbeat (waiting for 12V/button/ready)
// RUN   : bright green double-heartbeat (actively delivering power)
// IDLE  : soft-green slow heartbeat (ready/safe)
// FAULT : fast red strobe
// MAINT : cool blue breathe (maintenance/safe mode)
// OFF   : off
#define RGB_BG_BOOT_COLOR      RGB_AMBER
#define RGB_BG_START_COLOR     RGB_HEX(  0, 220,  60)
#define RGB_BG_IDLE_COLOR      RGB_HEX( 60, 200,  60)
#define RGB_BG_RUN_COLOR       RGB_HEX(  0, 255, 120)
#define RGB_BG_FAULT_COLOR     RGB_RED
#define RGB_BG_MAINT_COLOR     RGB_HEX(  0, 140, 255)
#define RGB_BG_WAIT_COLOR      RGB_AMBER
#define RGB_BG_OFF_COLOR       RGB_OFF

// FAULT strobe shape (background)
#define RGB_FAULT_STROBE_ON_MS   60
#define RGB_FAULT_STROBE_OFF_MS  50

// =============================== Overlays ===============================
// Wi-Fi
#define RGB_OVR_WIFI_STA        RGB_GREEN
#define RGB_OVR_WIFI_AP         RGB_YELLOW
#define RGB_OVR_WIFI_LOST       RGB_AMBER
#define RGB_OVR_NET_RECOVER     RGB_HEX(  0, 220, 180)

// Web roles
#define RGB_OVR_WEB_ADMIN       RGB_HEX(255, 120,  40)  // orange-red pulse
#define RGB_OVR_WEB_USER        RGB_HEX(  0, 220, 140)  // teal/green pulse

// Fan / Relay / Bypass / Discharge
#define RGB_OVR_FAN_ON          RGB_CYAN
#define RGB_OVR_FAN_OFF         RGB_AMBER
#define RGB_OVR_RELAY_ON        RGB_YELLOW
#define RGB_OVR_RELAY_OFF       RGB_AMBER
#define RGB_OVR_BYPASS_ON       RGB_TEAL
#define RGB_OVR_DISCHG_ACTIVE   RGB_HEX(255, 210,  80)
#define RGB_OVR_DISCHG_DONE     RGB_HEX(  0, 220, 120)

// Temperature / Current
#define RGB_OVR_TEMP_WARN       RGB_YELLOW
#define RGB_OVR_TEMP_CRIT       RGB_RED
#define RGB_OVR_CURR_WARN       RGB_YELLOW
#define RGB_OVR_CURR_TRIP       RGB_RED

// Power path & faults
#define RGB_OVR_12V_LOST          RGB_HEX(255,  80,  20)
#define RGB_OVR_DC_LOW            RGB_AMBER
#define RGB_OVR_OVERCURRENT       RGB_OVR_CURR_TRIP
#define RGB_OVR_THERMAL_GLOBAL    RGB_HEX(255, 100,  20)
#define RGB_OVR_THERMAL_CH_LOCK   RGB_HEX(255, 180,  40)
#define RGB_OVR_SENSOR_MISSING    RGB_HEX( 80, 160, 255)
#define RGB_OVR_CFG_ERROR         RGB_HEX(255,  60, 180)
#define RGB_OVR_BYPASS_FORCED_OFF RGB_HEX(255, 150,  40)

// Channels (Output events)
#define RGB_OVR_OUTPUT_ON       RGB_HEX(  0, 255, 120)
#define RGB_OVR_OUTPUT_OFF      RGB_AMBER

// Power-up sequence
#define RGB_OVR_PWR_WAIT_12V    RGB_AMBER
#define RGB_OVR_PWR_CHARGING    RGB_HEX(255, 200,  60)
#define RGB_OVR_PWR_THRESH_OK   RGB_HEX(  0, 220,  80)
#define RGB_OVR_PWR_BYPASS_ON   RGB_TEAL
#define RGB_OVR_PWR_WAIT_BUTTON RGB_HEX(220, 180,  80)
#define RGB_OVR_PWR_START       RGB_HEX(  0, 220, 120)

// General
#define RGB_OVR_WAKE_FLASH      RGB_SOFT_WHITE
#define RGB_OVR_RESET_TRIGGER   RGB_SOFT_WHITE
#define RGB_OVR_LOW_BATT        RGB_YELLOW
#define RGB_OVR_CRITICAL_BATT   RGB_RED

#endif // RGB_CONFIG_H


