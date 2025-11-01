/**************************************************************
 *  Project     : Power Manager
 *  File        : RGBConfig.h
 *  Purpose     : Centralized configuration for the RGB status LED.
 *                - Pure compile-time configuration (no NVS)
 *                - RTOS/Task and queue sizing
 *
 *  ───────────────────────────────────────────────────────────
 *  Color Reference (VS Code #hex swatches)
 *  Hardware note: Blue LED is not wired (RG-only). All colors use R+G.
 *
 *  Basic RG palette:
 *    RG_RED            #FF0000
 *    RG_GRN            #00FF00
 *    RG_AMB (amber)    #FF7800
 *    RG_YEL (yellow)   #FFC800
 *    RG_WHT_DARK       #1E1E00
 *    RG_WHT_SOFT       #787800
 *    RG_OFF            #000000
 *
 *  Background states:
 *    RGB_BG_START_COLOR    #00FF00  (START = RG_GRN)
 *    RGB_BG_IDLE_COLOR     #00B400  (0,180,0)
 *    RGB_BG_RUN_COLOR      #00DC00  (0,220,0)
 *    RGB_BG_FAULT_COLOR    #FF0000  (FAULT = RG_RED)
 *    RGB_BG_MAINT_COLOR    #FF7800  (MAINT = RG_AMB)
 *    RGB_BG_WAIT_COLOR     #FF7800  (WAIT  = RG_AMB)
 *
 *  Overlays — Fan & Relay:
 *    RGB_OVR_FAN_ON        #00FF00  (RG_GRN)
 *    RGB_OVR_FAN_OFF       #FF7800  (RG_AMB)
 *    RGB_OVR_RELAY_ON      #FFC800  (RG_YEL)
 *    RGB_OVR_RELAY_OFF     #FF7800  (RG_AMB)
 *
 *  Overlays — Wi-Fi:
 *    RGB_OVR_WIFI_STA      #00FF00  (joined STA)
 *    RGB_OVR_WIFI_AP       #FFC800  (AP active)
 *    RGB_OVR_WIFI_LOST     #FF7800  (link lost)
 *    RGB_OVR_NET_RECOVER   #00DC00  (0,220,0)
 *
 *  Overlays — Web roles:
 *    RGB_OVR_WEB_ADMIN     #C83C00  (200,60,0)
 *    RGB_OVR_WEB_USER      #3CC800  (60,200,0)
 *
 *  Overlays — Temperature & Current:
 *    RGB_OVR_TEMP_WARN     #FFC800
 *    RGB_OVR_TEMP_CRIT     #FF0000
 *    RGB_OVR_CURR_WARN     #FFC800
 *    RGB_OVR_CURR_TRIP     #FF0000
 *
 *  Overlays — Channels:
 *    RGB_OVR_OUTPUT_ON     #00FF00
 *    RGB_OVR_OUTPUT_OFF    #FF7800
 *
 *  Overlays — General:
 *    RGB_OVR_WAKE_FLASH    #787800
 *    RGB_OVR_RESET_TRIGGER #787800
 *    RGB_OVR_LOW_BATT      #FFC800
 *    RGB_OVR_CRITICAL_BATT #FF0000
 *
 *  Overlays — Power-up sequence (RG-only friendly):
 *    RGB_OVR_PWR_WAIT_12V    #C87800  (200,120,0)
 *    RGB_OVR_PWR_CHARGING    #FFA000  (255,160,0)
 *    RGB_OVR_PWR_THRESH_OK   #00DC00  (0,220,0)
 *    RGB_OVR_PWR_BYPASS_ON   #00B43C  (0,180,60)
 *    RGB_OVR_PWR_WAIT_BUTTON #787800  (120,120,0)
 *    RGB_OVR_PWR_START       #00C800  (0,200,0)
 *  ───────────────────────────────────────────────────────────
 **************************************************************/

#ifndef RGB_CONFIG_H
#define RGB_CONFIG_H

/**
 * @file RGBConfig.h
 * @brief Compile-time configuration & palette for the RGB LED.
 *
 * Priorities:
 *   PRIO_BACKGROUND=0, PRIO_ACTION=1, PRIO_ALERT=2, PRIO_CRITICAL=3
 *
 * Queue policy:
 *   Overlays posted with preempt=true will interrupt if priority >= current.
 *   When full, alerts (>= PRIO_ALERT) drop the oldest to make room.
 */

// =============================== Core ===============================
#define RGB_TASK_STACK     4096
#define RGB_TASK_PRIORITY  2
#define RGB_CMD_QUEUE_LEN  24

// Wired: only R/G (Blue not connected)
#define RGB_FORCE_RG_ONLY   1

// =============================== Helpers ===============================
#define RGB_HEX(r,g,b)   ( ((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b) )
#define RGB_R(c)         ( (uint8_t)(((c) >> 16) & 0xFF) )
#define RGB_G(c)         ( (uint8_t)(((c) >> 8) & 0xFF) )
#define RGB_B(c)         ( (uint8_t)((c) & 0xFF) )

// Fast rainbow step (kept for compatibility; unused when RG-only)
#define RGB_RAINBOW_STEP_DEG  20.0f

// =============================== Basic RG palette ===============================
// We bias toward Red/Green mixes so visuals look correct with no blue LED.
#define RG_RED        RGB_HEX(255,  0,  0)
#define RG_GRN        RGB_HEX(  0,255,  0)
#define RG_AMB        RGB_HEX(255,120,  0) // amber
#define RG_YEL        RGB_HEX(255,200,  0) // yellow
#define RG_WHT_DARK   RGB_HEX( 30, 30,  0) // “dim white” using RG
#define RG_WHT_SOFT   RGB_HEX(120,120,  0) // soft white via RG
#define RG_OFF        RGB_HEX(  0,  0,  0)

// =============================== Background colors ===============================
// WAIT  : amber breathe (getting ready)
// RUN   : green double-heartbeat (actively working)
// IDLE  : soft-green slow heartbeat (standing by)
// FAULT : very fast red strobe (~8 Hz; 50 on / 75 off) -> see strobe params below
// OFF   : off
#define RGB_BG_START_COLOR      RG_GRN
#define RGB_BG_IDLE_COLOR       RGB_HEX(  0,180,  0)
#define RGB_BG_RUN_COLOR        RGB_HEX(  0,220,  0)
#define RGB_BG_FAULT_COLOR      RG_RED
#define RGB_BG_MAINT_COLOR      RG_AMB
#define RGB_BG_WAIT_COLOR       RG_AMB

// FAULT strobe shape (background)
#define RGB_FAULT_STROBE_ON_MS   50
#define RGB_FAULT_STROBE_OFF_MS  75

// =============================== Overlays for Power Manager ===============================
// Fan + Relay
#define RGB_OVR_FAN_ON          RG_GRN
#define RGB_OVR_FAN_OFF         RG_AMB
#define RGB_OVR_RELAY_ON        RG_YEL
#define RGB_OVR_RELAY_OFF       RG_AMB

// Wi-Fi
#define RGB_OVR_WIFI_STA        RG_GRN
#define RGB_OVR_WIFI_AP         RG_YEL
#define RGB_OVR_WIFI_LOST       RG_AMB
#define RGB_OVR_NET_RECOVER     RGB_HEX(  0,220,  0)

// Web roles
#define RGB_OVR_WEB_ADMIN       RGB_HEX(200, 60,  0)  // orange-red pulse
#define RGB_OVR_WEB_USER        RGB_HEX( 60,200,  0)  // green pulse

// Temperature & Current
#define RGB_OVR_TEMP_WARN       RG_YEL
#define RGB_OVR_TEMP_CRIT       RG_RED
#define RGB_OVR_CURR_WARN       RG_YEL
#define RGB_OVR_CURR_TRIP       RG_RED

// Channels (Output events)
#define RGB_OVR_OUTPUT_ON       RG_GRN
#define RGB_OVR_OUTPUT_OFF      RG_AMB

// General
#define RGB_OVR_WAKE_FLASH      RG_WHT_SOFT
#define RGB_OVR_RESET_TRIGGER   RG_WHT_SOFT
#define RGB_OVR_LOW_BATT        RG_YEL
#define RGB_OVR_CRITICAL_BATT   RG_RED

// Power-up overlays colors (RG-only friendly)
#ifndef RGB_OVR_PWR_WAIT_12V
#define RGB_OVR_PWR_WAIT_12V   RGB_HEX(200, 120, 0)   // amber breathe
#endif
#ifndef RGB_OVR_PWR_CHARGING
#define RGB_OVR_PWR_CHARGING   RGB_HEX(255, 160, 0)   // brighter amber
#endif
#ifndef RGB_OVR_PWR_THRESH_OK
#define RGB_OVR_PWR_THRESH_OK  RGB_HEX(0,   220, 0)   // green flash
#endif
#ifndef RGB_OVR_PWR_BYPASS_ON
#define RGB_OVR_PWR_BYPASS_ON  RGB_HEX(0,   180, 60)  // greenish flash
#endif
#ifndef RGB_OVR_PWR_WAIT_BUTTON
#define RGB_OVR_PWR_WAIT_BUTTON RGB_HEX(120, 120, 0)  // yellow heartbeat
#endif
#ifndef RGB_OVR_PWR_START
#define RGB_OVR_PWR_START      RGB_HEX(0,   200, 0)   // start flash
#endif

#endif // RGB_CONFIG_H
