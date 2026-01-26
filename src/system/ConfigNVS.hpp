/**************************************************************
 *  Author      : Tshibangu Samuel
 *  Role        : Freelance Embedded Systems Engineer
 *  Expertise   : Secure IoT Systems, Embedded C++, RTOS, Control Logic
 *  Contact     : tshibsamuel47@gmail.com
 *  Portfolio   : https://www.freelancer.com/u/tshibsamuel477
 *  Phone       : +216 54 429 793
 **************************************************************/
#ifndef CONFIG_NVS_HPP
#define CONFIG_NVS_HPP

#include <Arduino.h>
#include <math.h>

#define CONFIG_PARTITION               "config"    // NVS partition name

// ==================================================
// Device Configuration Keys & Defaults for Preferences
// ==================================================

// ---------- Wi-Fi & Storage ----------
#define DEVICE_WIFI_HOTSPOT_NAME_KEY   "APNAM"     // Hotspot SSID key
#define DEVICE_AP_AUTH_PASS_KEY        "APPSS"     // Hotspot password key
#define RESET_FLAG                     "RTFLG"     // Preferences reset flag

#define STA_SSID_KEY                   "WIFSSD"    // Station Mode SSID key
#define STA_PASS_KEY                   "WIFPASS"   // Station Mode password key
// ---------- Authentication (Max 1 Admin, 1 User) ----------
#define ADMIN_ID_KEY                   "ADMID"     // Admin login username
#define ADMIN_PASS_KEY                 "ADMPW"     // Admin login password
#define USER_ID_KEY                    "USRID"     // Single customer login username
#define USER_PASS_KEY                  "USRPW"     // Single customer login password

// ---------- UI / Language ----------
#define UI_LANGUAGE_KEY                "UILANG"    // UI language code ("en", "fr", "it")

// ---------- Preference Keys ----------
#define INRUSH_DELAY_KEY               "INSHDY"    // Inrush delay duration key
#define LED_FEEDBACK_KEY               "LEDFB"     // LED feedback toggle key
#define TEMP_THRESHOLD_KEY             "TMPTH"     // Over-temperature shutdown key
#define TEMP_WARN_KEY                  "TPWRN"     // Over-temperature warning (pre-trip) [degC]
#define CHARGE_RESISTOR_KEY            "CHRES"     // Charge resistor value key
#define AC_FREQUENCY_KEY               "ACFRQ"     // Sampling rate key (Hz)
#define AC_VOLTAGE_KEY                 "ACVLT"     // AC line voltage key
#define CURRENT_SOURCE_KEY             "CSRC"     // int: 0=estimate from Vdrop, 1=ACS sensor
#define CP_EMP_GAIN_KEY                "CPEMGN"    // Empirical capacitor ADC gain key
#define CAP_BANK_CAP_F_KEY             "CPCAPF"    // Capacitor bank capacitance [F] key
#define CURR_LIMIT_KEY                 "CURRLT"   // float: over-current trip threshold [A]
#define TEMP_SENSOR_COUNT_KEY          "TMNT"    // Number of temperature sensors detected
#define RTC_CURRENT_EPOCH_KEY          "RCUR"    // Last known epoch persisted
#define RTC_PRESLEEP_EPOCH_KEY         "RSLP"   // Epoch saved before deep sleep
#define RTC_DEFAULT_EPOCH              0ULL
#define FLOOR_THICKNESS_MM_KEY         "FLTHK"   // float: floor thickness [mm]
#define FLOOR_MATERIAL_KEY             "FLMAT"   // int: floor material code
#define FLOOR_MAX_C_KEY                "FLMAX"   // float: max floor temp [C]
#define FLOOR_SWITCH_MARGIN_C_KEY      "FLMRG"   // float: floor margin for boost->equilibrium switch [C]
#define NICHROME_FINAL_TEMP_C_KEY      "NCFIN"   // float: target final nichrome temp [C]
#define NTC_GATE_INDEX_KEY             "NTCGT"   // int: wire index tied to NTC
#define NTC_T0_C_KEY                   "NTCT0"   // float: NTC T0 reference temp [C]
#define NTC_R0_KEY                     "NTCR0"   // float: NTC R0 at T0 [ohms]
#define NTC_BETA_KEY                   "NTCBT"   // float: NTC beta constant
#define NTC_FIXED_RES_KEY              "NTCFR"   // float: NTC divider fixed resistor [ohms]
#define NTC_MODEL_KEY                  "NTCMD"   // int: NTC model (0=beta, 1=steinhart)
#define NTC_SH_A_KEY                   "NTCSA"   // float: Steinhart-Hart A
#define NTC_SH_B_KEY                   "NTCSB"   // float: Steinhart-Hart B
#define NTC_SH_C_KEY                   "NTCSC"   // float: Steinhart-Hart C
#define NTC_MIN_C_KEY                  "NTCMIN"  // float: NTC min temp [C]
#define NTC_MAX_C_KEY                  "NTCMAX"  // float: NTC max temp [C]
#define NTC_SAMPLES_KEY                "NTCSMP"  // int: NTC samples per update
#define NTC_PRESS_MV_KEY               "NTCPR"   // float: NTC press threshold [mV]
#define NTC_RELEASE_MV_KEY             "NTCRL"   // float: NTC release threshold [mV]
#define NTC_DEBOUNCE_MS_KEY            "NTCDB"   // int: NTC debounce [ms]
#define NTC_CAL_TARGET_C_KEY           "NTCTG"   // float: NTC calibration target temp [C]
#define NTC_CAL_SAMPLE_MS_KEY          "NTCMS"   // int: NTC calibration sample interval [ms]
#define NTC_CAL_TIMEOUT_MS_KEY         "NTCTO"   // int: NTC calibration timeout [ms]

// ---------- Setup Wizard / Calibration Progress ----------
#define SETUP_DONE_KEY                 "SETUP"   // bool: setup completed
#define SETUP_STAGE_KEY                "STAGE"   // int: last completed wizard step
#define SETUP_SUBSTAGE_KEY             "SUBSTG"  // int: last completed wizard sub-step
#define SETUP_WIRE_INDEX_KEY           "STWIRE"  // int: wire index currently calibrating
#define CALIB_CAP_DONE_KEY             "CALCAP"  // bool: cap bank calibration done
#define CALIB_NTC_DONE_KEY             "CALNTC"  // bool: NTC calibration done
#define CALIB_W1_DONE_KEY              "CALW1"   // bool: wire 1 calibration done
#define CALIB_W2_DONE_KEY              "CALW2"   // bool: wire 2 calibration done
#define CALIB_W3_DONE_KEY              "CALW3"   // bool: wire 3 calibration done
#define CALIB_W4_DONE_KEY              "CALW4"   // bool: wire 4 calibration done
#define CALIB_W5_DONE_KEY              "CALW5"   // bool: wire 5 calibration done
#define CALIB_W6_DONE_KEY              "CALW6"   // bool: wire 6 calibration done
#define CALIB_W7_DONE_KEY              "CALW7"   // bool: wire 7 calibration done
#define CALIB_W8_DONE_KEY              "CALW8"   // bool: wire 8 calibration done
#define CALIB_W9_DONE_KEY              "CALW9"   // bool: wire 9 calibration done
#define CALIB_W10_DONE_KEY             "CALW10"  // bool: wire 10 calibration done
#define CALIB_FLOOR_DONE_KEY           "CALFLR"  // bool: floor calibration done
#define CALIB_PRESENCE_DONE_KEY        "CALPRS"  // bool: presence calibration done
#define PRESENCE_MIN_RATIO_KEY         "PMINR"   // float: min current ratio (Imeas/Iexp) for presence
#define PRESENCE_WINDOW_MS_KEY         "PWIN"    // int: averaging window [ms]
#define PRESENCE_FAIL_COUNT_KEY        "PFAIL"   // int: consecutive failures before missing
#define CALIB_W1_STAGE_KEY             "W1STG"   // int: wire 1 calibration stage
#define CALIB_W2_STAGE_KEY             "W2STG"   // int: wire 2 calibration stage
#define CALIB_W3_STAGE_KEY             "W3STG"   // int: wire 3 calibration stage
#define CALIB_W4_STAGE_KEY             "W4STG"   // int: wire 4 calibration stage
#define CALIB_W5_STAGE_KEY             "W5STG"   // int: wire 5 calibration stage
#define CALIB_W6_STAGE_KEY             "W6STG"   // int: wire 6 calibration stage
#define CALIB_W7_STAGE_KEY             "W7STG"   // int: wire 7 calibration stage
#define CALIB_W8_STAGE_KEY             "W8STG"   // int: wire 8 calibration stage
#define CALIB_W9_STAGE_KEY             "W9STG"   // int: wire 9 calibration stage
#define CALIB_W10_STAGE_KEY            "W10STG"  // int: wire 10 calibration stage
#define CALIB_W1_RUNNING_KEY           "W1RUN"   // bool: wire 1 calibration running
#define CALIB_W2_RUNNING_KEY           "W2RUN"   // bool: wire 2 calibration running
#define CALIB_W3_RUNNING_KEY           "W3RUN"   // bool: wire 3 calibration running
#define CALIB_W4_RUNNING_KEY           "W4RUN"   // bool: wire 4 calibration running
#define CALIB_W5_RUNNING_KEY           "W5RUN"   // bool: wire 5 calibration running
#define CALIB_W6_RUNNING_KEY           "W6RUN"   // bool: wire 6 calibration running
#define CALIB_W7_RUNNING_KEY           "W7RUN"   // bool: wire 7 calibration running
#define CALIB_W8_RUNNING_KEY           "W8RUN"   // bool: wire 8 calibration running
#define CALIB_W9_RUNNING_KEY           "W9RUN"   // bool: wire 9 calibration running
#define CALIB_W10_RUNNING_KEY          "W10RUN"  // bool: wire 10 calibration running
#define CALIB_W1_TS_KEY                "W1TS"    // int: wire 1 calibration timestamp
#define CALIB_W2_TS_KEY                "W2TS"    // int: wire 2 calibration timestamp
#define CALIB_W3_TS_KEY                "W3TS"    // int: wire 3 calibration timestamp
#define CALIB_W4_TS_KEY                "W4TS"    // int: wire 4 calibration timestamp
#define CALIB_W5_TS_KEY                "W5TS"    // int: wire 5 calibration timestamp
#define CALIB_W6_TS_KEY                "W6TS"    // int: wire 6 calibration timestamp
#define CALIB_W7_TS_KEY                "W7TS"    // int: wire 7 calibration timestamp
#define CALIB_W8_TS_KEY                "W8TS"    // int: wire 8 calibration timestamp
#define CALIB_W9_TS_KEY                "W9TS"    // int: wire 9 calibration timestamp
#define CALIB_W10_TS_KEY               "W10TS"   // int: wire 10 calibration timestamp
#define CALIB_FLOOR_STAGE_KEY          "FLSTG"   // int: floor calibration stage
#define CALIB_FLOOR_RUNNING_KEY        "FLRUN"   // bool: floor calibration running
#define CALIB_FLOOR_TS_KEY             "FLTS"    // int: floor calibration timestamp
#define CALIB_SCHEMA_VERSION_KEY       "CALVER"  // int: calibration schema version
#define FLOOR_MODEL_TAU_KEY            "FLTAU"   // double: floor tau [s]
#define FLOOR_MODEL_K_KEY              "FLKLS"   // double: floor k [W/K]
#define FLOOR_MODEL_C_KEY              "FLCAP"   // double: floor C [J/K]

// ---------- Per-wire model parameters (Wire1..Wire10) ----------
#define W1TAU_KEY                      "W1TAU"   // double: wire 1 tau [s]
#define W2TAU_KEY                      "W2TAU"   // double: wire 2 tau [s]
#define W3TAU_KEY                      "W3TAU"   // double: wire 3 tau [s]
#define W4TAU_KEY                      "W4TAU"   // double: wire 4 tau [s]
#define W5TAU_KEY                      "W5TAU"   // double: wire 5 tau [s]
#define W6TAU_KEY                      "W6TAU"   // double: wire 6 tau [s]
#define W7TAU_KEY                      "W7TAU"   // double: wire 7 tau [s]
#define W8TAU_KEY                      "W8TAU"   // double: wire 8 tau [s]
#define W9TAU_KEY                      "W9TAU"   // double: wire 9 tau [s]
#define W10TAU_KEY                     "W10TAU"  // double: wire 10 tau [s]
#define W1KLS_KEY                      "W1KLS"   // double: wire 1 k [W/K]
#define W2KLS_KEY                      "W2KLS"   // double: wire 2 k [W/K]
#define W3KLS_KEY                      "W3KLS"   // double: wire 3 k [W/K]
#define W4KLS_KEY                      "W4KLS"   // double: wire 4 k [W/K]
#define W5KLS_KEY                      "W5KLS"   // double: wire 5 k [W/K]
#define W6KLS_KEY                      "W6KLS"   // double: wire 6 k [W/K]
#define W7KLS_KEY                      "W7KLS"   // double: wire 7 k [W/K]
#define W8KLS_KEY                      "W8KLS"   // double: wire 8 k [W/K]
#define W9KLS_KEY                      "W9KLS"   // double: wire 9 k [W/K]
#define W10KLS_KEY                     "W10KLS"  // double: wire 10 k [W/K]
#define W1CAP_KEY                      "W1CAP"   // double: wire 1 C [J/K]
#define W2CAP_KEY                      "W2CAP"   // double: wire 2 C [J/K]
#define W3CAP_KEY                      "W3CAP"   // double: wire 3 C [J/K]
#define W4CAP_KEY                      "W4CAP"   // double: wire 4 C [J/K]
#define W5CAP_KEY                      "W5CAP"   // double: wire 5 C [J/K]
#define W6CAP_KEY                      "W6CAP"   // double: wire 6 C [J/K]
#define W7CAP_KEY                      "W7CAP"   // double: wire 7 C [J/K]
#define W8CAP_KEY                      "W8CAP"   // double: wire 8 C [J/K]
#define W9CAP_KEY                      "W9CAP"   // double: wire 9 C [J/K]
#define W10CAP_KEY                     "W10CAP"  // double: wire 10 C [J/K]

// ---------- Device Identity & Versions (NVS keys, <= 6 chars) ----------
#define DEV_ID_KEY                     "DEVID"     // Human-readable device id
#define DEV_SW_KEY                     "DVSWV"     // Software version
#define DEV_HW_KEY                     "DVHWV"     // Hardware version

// Version values (update as you rev firmware / hardware)
#define DEVICE_SW_VERSION              "1.7.0"
#define DEVICE_HW_VERSION              "2.2.5"

// Compile-time guard: enforce max length for new NVS keys (<= 6 chars)
#define ASSERT_NVS_KEY_LEN(k) static_assert(sizeof(k) - 1 <= 6, "NVS key must be <= 6 chars")
ASSERT_NVS_KEY_LEN(DEV_ID_KEY);
ASSERT_NVS_KEY_LEN(DEV_SW_KEY);
ASSERT_NVS_KEY_LEN(DEV_HW_KEY);
ASSERT_NVS_KEY_LEN(UI_LANGUAGE_KEY);
ASSERT_NVS_KEY_LEN(CURRENT_SOURCE_KEY);
ASSERT_NVS_KEY_LEN(CURR_LIMIT_KEY);
ASSERT_NVS_KEY_LEN(TEMP_WARN_KEY);
ASSERT_NVS_KEY_LEN(FLOOR_THICKNESS_MM_KEY);
ASSERT_NVS_KEY_LEN(FLOOR_MATERIAL_KEY);
ASSERT_NVS_KEY_LEN(FLOOR_MAX_C_KEY);
ASSERT_NVS_KEY_LEN(FLOOR_SWITCH_MARGIN_C_KEY);
ASSERT_NVS_KEY_LEN(NICHROME_FINAL_TEMP_C_KEY);
ASSERT_NVS_KEY_LEN(NTC_GATE_INDEX_KEY);
ASSERT_NVS_KEY_LEN(NTC_T0_C_KEY);
ASSERT_NVS_KEY_LEN(NTC_R0_KEY);
ASSERT_NVS_KEY_LEN(NTC_BETA_KEY);
ASSERT_NVS_KEY_LEN(NTC_FIXED_RES_KEY);
ASSERT_NVS_KEY_LEN(NTC_MODEL_KEY);
ASSERT_NVS_KEY_LEN(NTC_SH_A_KEY);
ASSERT_NVS_KEY_LEN(NTC_SH_B_KEY);
ASSERT_NVS_KEY_LEN(NTC_SH_C_KEY);
ASSERT_NVS_KEY_LEN(NTC_MIN_C_KEY);
ASSERT_NVS_KEY_LEN(NTC_MAX_C_KEY);
ASSERT_NVS_KEY_LEN(NTC_SAMPLES_KEY);
ASSERT_NVS_KEY_LEN(NTC_PRESS_MV_KEY);
ASSERT_NVS_KEY_LEN(NTC_RELEASE_MV_KEY);
ASSERT_NVS_KEY_LEN(NTC_DEBOUNCE_MS_KEY);
ASSERT_NVS_KEY_LEN(NTC_CAL_TARGET_C_KEY);
ASSERT_NVS_KEY_LEN(NTC_CAL_SAMPLE_MS_KEY);
ASSERT_NVS_KEY_LEN(NTC_CAL_TIMEOUT_MS_KEY);
ASSERT_NVS_KEY_LEN(SETUP_DONE_KEY);
ASSERT_NVS_KEY_LEN(SETUP_STAGE_KEY);
ASSERT_NVS_KEY_LEN(SETUP_SUBSTAGE_KEY);
ASSERT_NVS_KEY_LEN(SETUP_WIRE_INDEX_KEY);
ASSERT_NVS_KEY_LEN(CALIB_CAP_DONE_KEY);
ASSERT_NVS_KEY_LEN(CALIB_NTC_DONE_KEY);
ASSERT_NVS_KEY_LEN(CALIB_W1_DONE_KEY);
ASSERT_NVS_KEY_LEN(CALIB_W2_DONE_KEY);
ASSERT_NVS_KEY_LEN(CALIB_W3_DONE_KEY);
ASSERT_NVS_KEY_LEN(CALIB_W4_DONE_KEY);
ASSERT_NVS_KEY_LEN(CALIB_W5_DONE_KEY);
ASSERT_NVS_KEY_LEN(CALIB_W6_DONE_KEY);
ASSERT_NVS_KEY_LEN(CALIB_W7_DONE_KEY);
ASSERT_NVS_KEY_LEN(CALIB_W8_DONE_KEY);
ASSERT_NVS_KEY_LEN(CALIB_W9_DONE_KEY);
ASSERT_NVS_KEY_LEN(CALIB_W10_DONE_KEY);
ASSERT_NVS_KEY_LEN(CALIB_FLOOR_DONE_KEY);
ASSERT_NVS_KEY_LEN(CALIB_PRESENCE_DONE_KEY);
ASSERT_NVS_KEY_LEN(PRESENCE_MIN_RATIO_KEY);
ASSERT_NVS_KEY_LEN(PRESENCE_WINDOW_MS_KEY);
ASSERT_NVS_KEY_LEN(PRESENCE_FAIL_COUNT_KEY);
ASSERT_NVS_KEY_LEN(CALIB_W1_STAGE_KEY);
ASSERT_NVS_KEY_LEN(CALIB_W2_STAGE_KEY);
ASSERT_NVS_KEY_LEN(CALIB_W3_STAGE_KEY);
ASSERT_NVS_KEY_LEN(CALIB_W4_STAGE_KEY);
ASSERT_NVS_KEY_LEN(CALIB_W5_STAGE_KEY);
ASSERT_NVS_KEY_LEN(CALIB_W6_STAGE_KEY);
ASSERT_NVS_KEY_LEN(CALIB_W7_STAGE_KEY);
ASSERT_NVS_KEY_LEN(CALIB_W8_STAGE_KEY);
ASSERT_NVS_KEY_LEN(CALIB_W9_STAGE_KEY);
ASSERT_NVS_KEY_LEN(CALIB_W10_STAGE_KEY);
ASSERT_NVS_KEY_LEN(CALIB_W1_RUNNING_KEY);
ASSERT_NVS_KEY_LEN(CALIB_W2_RUNNING_KEY);
ASSERT_NVS_KEY_LEN(CALIB_W3_RUNNING_KEY);
ASSERT_NVS_KEY_LEN(CALIB_W4_RUNNING_KEY);
ASSERT_NVS_KEY_LEN(CALIB_W5_RUNNING_KEY);
ASSERT_NVS_KEY_LEN(CALIB_W6_RUNNING_KEY);
ASSERT_NVS_KEY_LEN(CALIB_W7_RUNNING_KEY);
ASSERT_NVS_KEY_LEN(CALIB_W8_RUNNING_KEY);
ASSERT_NVS_KEY_LEN(CALIB_W9_RUNNING_KEY);
ASSERT_NVS_KEY_LEN(CALIB_W10_RUNNING_KEY);
ASSERT_NVS_KEY_LEN(CALIB_W1_TS_KEY);
ASSERT_NVS_KEY_LEN(CALIB_W2_TS_KEY);
ASSERT_NVS_KEY_LEN(CALIB_W3_TS_KEY);
ASSERT_NVS_KEY_LEN(CALIB_W4_TS_KEY);
ASSERT_NVS_KEY_LEN(CALIB_W5_TS_KEY);
ASSERT_NVS_KEY_LEN(CALIB_W6_TS_KEY);
ASSERT_NVS_KEY_LEN(CALIB_W7_TS_KEY);
ASSERT_NVS_KEY_LEN(CALIB_W8_TS_KEY);
ASSERT_NVS_KEY_LEN(CALIB_W9_TS_KEY);
ASSERT_NVS_KEY_LEN(CALIB_W10_TS_KEY);
ASSERT_NVS_KEY_LEN(CALIB_FLOOR_STAGE_KEY);
ASSERT_NVS_KEY_LEN(CALIB_FLOOR_RUNNING_KEY);
ASSERT_NVS_KEY_LEN(CALIB_FLOOR_TS_KEY);
ASSERT_NVS_KEY_LEN(CALIB_SCHEMA_VERSION_KEY);
ASSERT_NVS_KEY_LEN(FLOOR_MODEL_TAU_KEY);
ASSERT_NVS_KEY_LEN(FLOOR_MODEL_K_KEY);
ASSERT_NVS_KEY_LEN(FLOOR_MODEL_C_KEY);
ASSERT_NVS_KEY_LEN(W1TAU_KEY);
ASSERT_NVS_KEY_LEN(W2TAU_KEY);
ASSERT_NVS_KEY_LEN(W3TAU_KEY);
ASSERT_NVS_KEY_LEN(W4TAU_KEY);
ASSERT_NVS_KEY_LEN(W5TAU_KEY);
ASSERT_NVS_KEY_LEN(W6TAU_KEY);
ASSERT_NVS_KEY_LEN(W7TAU_KEY);
ASSERT_NVS_KEY_LEN(W8TAU_KEY);
ASSERT_NVS_KEY_LEN(W9TAU_KEY);
ASSERT_NVS_KEY_LEN(W10TAU_KEY);
ASSERT_NVS_KEY_LEN(W1KLS_KEY);
ASSERT_NVS_KEY_LEN(W2KLS_KEY);
ASSERT_NVS_KEY_LEN(W3KLS_KEY);
ASSERT_NVS_KEY_LEN(W4KLS_KEY);
ASSERT_NVS_KEY_LEN(W5KLS_KEY);
ASSERT_NVS_KEY_LEN(W6KLS_KEY);
ASSERT_NVS_KEY_LEN(W7KLS_KEY);
ASSERT_NVS_KEY_LEN(W8KLS_KEY);
ASSERT_NVS_KEY_LEN(W9KLS_KEY);
ASSERT_NVS_KEY_LEN(W10KLS_KEY);
ASSERT_NVS_KEY_LEN(W1CAP_KEY);
ASSERT_NVS_KEY_LEN(W2CAP_KEY);
ASSERT_NVS_KEY_LEN(W3CAP_KEY);
ASSERT_NVS_KEY_LEN(W4CAP_KEY);
ASSERT_NVS_KEY_LEN(W5CAP_KEY);
ASSERT_NVS_KEY_LEN(W6CAP_KEY);
ASSERT_NVS_KEY_LEN(W7CAP_KEY);
ASSERT_NVS_KEY_LEN(W8CAP_KEY);
ASSERT_NVS_KEY_LEN(W9CAP_KEY);
ASSERT_NVS_KEY_LEN(W10CAP_KEY);

// ---------- Output Access Flags ----------
#define OUT01_ACCESS_KEY               "OUT1F"
#define OUT02_ACCESS_KEY               "OUT2F"
#define OUT03_ACCESS_KEY               "OUT3F"
#define OUT04_ACCESS_KEY               "OUT4F"
#define OUT05_ACCESS_KEY               "OUT5F"
#define OUT06_ACCESS_KEY               "OUT6F"
#define OUT07_ACCESS_KEY               "OUT7F"
#define OUT08_ACCESS_KEY               "OUT8F"
#define OUT09_ACCESS_KEY               "OUT9F"
#define OUT10_ACCESS_KEY               "OUT10F"
// ---------- Output Access Defaults ----------
#define DEFAULT_OUT01_ACCESS           true
#define DEFAULT_OUT02_ACCESS           true
#define DEFAULT_OUT03_ACCESS           false
#define DEFAULT_OUT04_ACCESS           true
#define DEFAULT_OUT05_ACCESS           false
#define DEFAULT_OUT06_ACCESS           true
#define DEFAULT_OUT07_ACCESS           false
#define DEFAULT_OUT08_ACCESS           true
#define DEFAULT_OUT09_ACCESS           false
#define DEFAULT_OUT10_ACCESS           false

// ---------- Authentication Defaults ----------
#define DEFAULT_ADMIN_ID               "admin"          // Default admin username
#define DEFAULT_ADMIN_PASS             "admin123"       // Default admin password
#define DEFAULT_USER_ID                "user"           // Default customer username
#define DEFAULT_USER_PASS              "user123"        // Default customer password

// ---------- UI Defaults ----------
#define DEFAULT_UI_LANGUAGE            "en"
// Presence override (test aid): set to 1 to force all wires present
#define FORCE_ALL_WIRES_PRESENT        1
// ---------- Nichrome Wire Resistance (Ohms) ----------
#define DEFAULT_WIRE_RES_OHMS  44.0f   // default for all 10 wires

// 6-char NVS keys per wire
#define R01OHM_KEY  "R01OHM"
#define R02OHM_KEY  "R02OHM"
#define R03OHM_KEY  "R03OHM"
#define R04OHM_KEY  "R04OHM"
#define R05OHM_KEY  "R05OHM"
#define R06OHM_KEY  "R06OHM"
#define R07OHM_KEY  "R07OHM"
#define R08OHM_KEY  "R08OHM"
#define R09OHM_KEY  "R09OHM"
#define R10OHM_KEY  "R10OHM"

// Idle current calibration removed (no key)
#define WIRE_OHM_PER_M_KEY               "WOPERM"    // float: Ω per meter for installed nichrome
#define DEFAULT_WIRE_OHM_PER_M           2.0f        // 2 Ω/m nichrome (your current wire)
#define WIRE_GAUGE_KEY                   "WIREG"     // int: AWG gauge for installed nichrome
// NVS keys for persistent statistics (<= 6 chars each)

// Totals (lifetime)
#define PT_KEY_TOTAL_ENERGY_WH          "STWH"    // total energy [Wh]
#define PT_KEY_TOTAL_SESSIONS           "STCNT"   // total sessions
#define PT_KEY_TOTAL_SESSIONS_OK        "STCSOK"  // total successful sessions

// Last session snapshot
#define PT_KEY_LAST_SESS_ENERGY_WH      "LSEWH"   // last session energy [Wh]
#define PT_KEY_LAST_SESS_DURATION_S     "LSDUR"   // last session duration [s]
#define PT_KEY_LAST_SESS_PEAK_W         "LSPKW"   // last session peak power [W]
#define PT_KEY_LAST_SESS_PEAK_A         "LSPKA"   // last session peak current [A]
// --- DS18B20 identity keys (hex string of 8 bytes) ---
#define TSB0ID_KEY  "TSB0ID"  // Board sensor #0 ROM
#define TSB1ID_KEY  "TSB1ID"  // Board sensor #1 ROM
#define TSHSID_KEY  "TSHSID"  // Heatsink ROM
#define TSMAP_KEY   "TSMEP"
// Default values for all stats
#define PT_DEF_TOTAL_ENERGY_WH          0.0f
#define PT_DEF_TOTAL_SESSIONS           0
#define PT_DEF_TOTAL_SESSIONS_OK        0

#define PT_DEF_LAST_SESS_ENERGY_WH      0.0f
#define PT_DEF_LAST_SESS_DURATION_S     0
#define PT_DEF_LAST_SESS_PEAK_W         0.0f
#define PT_DEF_LAST_SESS_PEAK_A         0.0f

// ==================================================
// Default Values for Preferences
// ==================================================

// ---------- Wi-Fi Defaults ----------
#define DEVICE_WIFI_HOTSPOT_NAME       "PDis_"         // Default SSID
#define DEVICE_AP_AUTH_PASS_DEFAULT    "1234567890"    // Default password
#define DEFAULT_STA_SSID               "nothing"       // Optional default station SSID (blank)
#define DEFAULT_STA_PASS               "nothing"       // Optional default station password (blank)

// ---------- Timing & Behavior Defaults ----------
#define DEFAULT_INRUSH_DELAY           100              // ms
#define DEFAULT_LED_FEEDBACK           true             // true = LED feedback enabled
#define DEFAULT_TEMP_THRESHOLD         75.0f            // °C
#define DEFAULT_TEMP_WARN_C            65.0f            // °C (pre-trip warning)
#define DEFAULT_CHARGE_RESISTOR_OHMS   35.0f            // Ohms (VBUS- to System_GND tie / charge resistor)
#define DEFAULT_AC_FREQUENCY           500              // Hz sampling rate (50..500)
#define DEFAULT_AC_VOLTAGE             230.0f           // Volts
#define DEFAULT_DC_VOLTAGE             325.0f           // Volts (fixed target)
#define CURRENT_SRC_ESTIMATE           0                // estimate current from voltage drop
#define CURRENT_SRC_ACS                1                // ACS hall-effect current sensor
#define DEFAULT_CURRENT_SOURCE         CURRENT_SRC_ESTIMATE
#define DEFAULT_CAP_EMP_GAIN           (321.0f / 1.90f) // Default empirical ADC->bus gain
#define DEFAULT_CAP_BANK_CAP_F         0.0f             // Farads (0 => unknown until calibrated)
#define DEFAULT_TEMP_SENSOR_COUNT      12               // Default to 12 sensors unless discovered otherwise
#define DEFAULT_CURR_LIMIT_A           36.0f            // Default over-current trip [A]
#define DEFAULT_WIRE_GAUGE             20               // AWG number for installed nichrome
// NTC / analog power button defaults
#define NTC_ADC_REF_VOLTAGE            3.3f             // ADC reference [V]
#define NTC_ADC_MAX                    4095.0f          // 12-bit ADC full scale
#define DEFAULT_NTC_FIXED_RES_OHMS     9700.0f         // Divider fixed resistor [ohms]
#define DEFAULT_NTC_R0_OHMS            8063.0f         // NTC R0 at T0 [ohms]
#define DEFAULT_NTC_BETA               3977.0f          // Beta constant
#define DEFAULT_NTC_MODEL              0                // 0=beta, 1=steinhart
#define DEFAULT_NTC_SH_A               NAN              // Steinhart-Hart A (unset)
#define DEFAULT_NTC_SH_B               NAN              // Steinhart-Hart B (unset)
#define DEFAULT_NTC_SH_C               NAN              // Steinhart-Hart C (unset)
#define DEFAULT_NTC_T0_C               25.0f            // Reference temperature [degC]
#define DEFAULT_NTC_MIN_C              -40.0f           // Minimum valid temp [degC]
#define DEFAULT_NTC_MAX_C              200.0f           // Maximum valid temp [degC]
#define DEFAULT_NTC_PRESS_MV           20.0f            // Button press threshold [mV]
#define DEFAULT_NTC_RELEASE_MV         40.0f            // Button release threshold [mV]
#define DEFAULT_NTC_DEBOUNCE_MS        40               // Debounce duration [ms]
#define DEFAULT_NTC_SAMPLES            8                // ADC samples per update
#define DEFAULT_NTC_GATE_INDEX         1                // Wire index tied to NTC by default
#define DEFAULT_NTC_CAL_TARGET_C       100.0f           // NTC calibration target temp [C]
#define DEFAULT_NTC_CAL_SAMPLE_MS      500              // NTC calibration sample interval [ms]
#define DEFAULT_NTC_CAL_TIMEOUT_MS     1200000          // NTC calibration timeout [ms]
#define FLOOR_THICKNESS_MIN_MM         20.0f            // 2 cm minimum
#define FLOOR_THICKNESS_MAX_MM         50.0f            // 5 cm maximum
#define DEFAULT_FLOOR_THICKNESS_MM     0.0f             // Floor thickness [mm] (0 = unset)
#define DEFAULT_FLOOR_MAX_C            35.0f            // Max floor temperature [C]
#define DEFAULT_FLOOR_SWITCH_MARGIN_C  1.0f             // Floor margin for boost->equilibrium switch [C]
#define DEFAULT_NICHROME_FINAL_TEMP_C  0.0f             // Final temp [C] (0 = unset)

// Setup wizard defaults
#define DEFAULT_SETUP_DONE             false
#define DEFAULT_SETUP_STAGE            0
#define DEFAULT_SETUP_SUBSTAGE         0
#define DEFAULT_SETUP_WIRE_INDEX       0
#define DEFAULT_CALIB_CAP_DONE         false
#define DEFAULT_CALIB_NTC_DONE         false
#define DEFAULT_CALIB_W_DONE           false
#define DEFAULT_CALIB_FLOOR_DONE       false
#define DEFAULT_CALIB_PRESENCE_DONE    false
#define DEFAULT_PRESENCE_MIN_RATIO     0.50f           // 50% of expected current
#define DEFAULT_PRESENCE_WINDOW_MS     200
#define DEFAULT_PRESENCE_FAIL_COUNT    3
#define DEFAULT_CALIB_W_STAGE          0
#define DEFAULT_CALIB_W_RUNNING        false
#define DEFAULT_CALIB_W_TS             0
#define DEFAULT_CALIB_FLOOR_STAGE      0
#define DEFAULT_CALIB_FLOOR_RUNNING    false
#define DEFAULT_CALIB_FLOOR_TS         0
#define DEFAULT_CALIB_SCHEMA_VERSION   1
#define DEFAULT_FLOOR_MODEL_TAU        0.0
#define DEFAULT_FLOOR_MODEL_K          0.0
#define DEFAULT_FLOOR_MODEL_C          0.0
#define DEFAULT_WIRE_MODEL_TAU         35.0
#define DEFAULT_WIRE_MODEL_C           1.0
#define DEFAULT_WIRE_MODEL_K           (DEFAULT_WIRE_MODEL_C / DEFAULT_WIRE_MODEL_TAU)

// Floor material codes
#define FLOOR_MAT_WOOD                 0
#define FLOOR_MAT_EPOXY                1
#define FLOOR_MAT_CONCRETE             2
#define FLOOR_MAT_SLATE                3
#define FLOOR_MAT_MARBLE               4
#define FLOOR_MAT_GRANITE              5
#define DEFAULT_FLOOR_MATERIAL         FLOOR_MAT_WOOD


#endif // CONFIG_NVS_HPP
