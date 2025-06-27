#ifndef BUZZER_MANAGER_H
#define BUZZER_MANAGER_H

#include <Arduino.h>
#include "Config.h"
#include "ConfigManager.h"

class BuzzerManager {
public:
  enum BuzzerMode {
    BUZZER_SUCCESS,
    BUZZER_FAILED,
    BUZZER_WIFI_CONNECTED,
    BUZZER_WIFI_OFF,
    BUZZER_OVER_TEMPERATURE,
    BUZZER_FAULT,
    BUZZER_STARTUP,
    BUZZER_READY,
    BUZZER_SHUTDOWN,
    BUZZER_CLIENT_CONNECTED,      // NEW
    BUZZER_CLIENT_DISCONNECTED    // NEW
  };

  explicit BuzzerManager() {}

  void begin() {
    if (DEBUGMODE) {
      Serial.println("###########################################################");
      Serial.println("#                 Starting BuzzerManager                  #");
      Serial.println("###########################################################");
    }

    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, HIGH); // Ensure silent (active-low)
  }

  // Public API
  void bip()                      { tone(BUZZER_PIN, 1000, 50); digitalWrite(BUZZER_PIN, HIGH); }
  void successSound()            { playSequence(BUZZER_SUCCESS); }
  void failedSound()             { playSequence(BUZZER_FAILED); }
  void bipWiFiConnected()        { playSequence(BUZZER_WIFI_CONNECTED); }
  void bipWiFiOff()              { playSequence(BUZZER_WIFI_OFF); }
  void bipOverTemperature()      { playSequence(BUZZER_OVER_TEMPERATURE); }
  void bipFault()                { playSequence(BUZZER_FAULT); }
  void bipStartupSequence()      { playSequence(BUZZER_STARTUP); }
  void bipSystemReady()          { playSequence(BUZZER_READY); }
  void bipSystemShutdown()       { playSequence(BUZZER_SHUTDOWN); }
  void bipClientConnected()      { playSequence(BUZZER_CLIENT_CONNECTED); }     // NEW
  void bipClientDisconnected()   { playSequence(BUZZER_CLIENT_DISCONNECTED); }  // NEW

private:
  static void BuzzerTask(void* pvParameters) {
    BuzzerMode mode = *static_cast<BuzzerMode*>(pvParameters);
    free(pvParameters); // Clean up heap memory

    auto playTone = [](int freq, int duration) {
      tone(BUZZER_PIN, freq, duration);
      vTaskDelay(pdMS_TO_TICKS(duration + 10));
      digitalWrite(BUZZER_PIN, HIGH); // Pull high to silence buzzer (active-low)
    };

    switch (mode) {
      case BUZZER_SUCCESS:
        playTone(1000, 40);
        vTaskDelay(pdMS_TO_TICKS(30));
        playTone(1300, 40);
        vTaskDelay(pdMS_TO_TICKS(30));
        playTone(1600, 60);
        break;

      case BUZZER_FAILED:
        for (int i = 0; i < 2; ++i) {
          playTone(500, 50);
          vTaskDelay(pdMS_TO_TICKS(50));
        }
        break;

      case BUZZER_WIFI_CONNECTED:
        playTone(1200, 100);
        vTaskDelay(pdMS_TO_TICKS(50));
        playTone(1500, 100);
        break;

      case BUZZER_WIFI_OFF:
        playTone(800, 150);
        break;

      case BUZZER_OVER_TEMPERATURE:
        for (int i = 0; i < 4; ++i) {
          playTone(2000, 40);
          vTaskDelay(pdMS_TO_TICKS(60));
        }
        break;

      case BUZZER_FAULT:
        for (int i = 0; i < 5; ++i) {
          playTone(300, 80);
          vTaskDelay(pdMS_TO_TICKS(40));
        }
        break;

      case BUZZER_STARTUP:
        playTone(600, 80);
        vTaskDelay(pdMS_TO_TICKS(50));
        playTone(1000, 80);
        vTaskDelay(pdMS_TO_TICKS(50));
        playTone(1400, 80);
        break;

      case BUZZER_READY:
        playTone(2000, 50);
        vTaskDelay(pdMS_TO_TICKS(50));
        playTone(2500, 50);
        break;

      case BUZZER_SHUTDOWN:
        playTone(1500, 80);
        vTaskDelay(pdMS_TO_TICKS(50));
        playTone(1000, 80);
        vTaskDelay(pdMS_TO_TICKS(50));
        playTone(600, 80);
        break;

      case BUZZER_CLIENT_CONNECTED:
        playTone(1100, 50);
        vTaskDelay(pdMS_TO_TICKS(30));
        playTone(1300, 60);
        break;

      case BUZZER_CLIENT_DISCONNECTED:
        playTone(1200, 80);
        vTaskDelay(pdMS_TO_TICKS(40));
        playTone(900, 60);
        break;
    }

    digitalWrite(BUZZER_PIN, HIGH); // Extra safety
    vTaskDelete(nullptr);
  }

  void playSequence(BuzzerMode mode) {
    BuzzerMode* arg = static_cast<BuzzerMode*>(malloc(sizeof(BuzzerMode)));
    if (!arg) return;
    *arg = mode;
    xTaskCreate(BuzzerTask, "BuzzerSequence", 1024, arg, 1, nullptr);
  }
};

#endif // BUZZER_MANAGER_H
