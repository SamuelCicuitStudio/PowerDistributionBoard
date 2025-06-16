# 🔌 ESP32 WiFiManager REST API – Control & Monitoring Interface

This repository implements a fully embedded HTTP server hosted on an ESP32 device for managing a power delivery controller. The system exposes a RESTful API via Wi-Fi for device monitoring and control. This document describes the available API endpoints and their functionality.

---

## 📡 Connection Overview

The device starts in **Access Point (AP) mode**, broadcasting a Wi-Fi hotspot. A client (admin or user) connects to this AP and communicates via HTTP endpoints. Only **one authenticated device** can be connected at a time.

- Admin and User access are authenticated via username/password.
- Every client must regularly send a **heartbeat** (`/heartbeat`) to stay connected.
- If no heartbeat is received in 3 seconds, the client is disconnected automatically.

---

## 🌐 Endpoints

### ⚡ `/heartbeat`
- **Method:** `GET`  
- **Purpose:** Notifies the server the client is still alive.  
- **Behavior:**
  - Must be called every **3 seconds** by the connected client.
  - Resets the watchdog timer internally.
  - If missed, resets connection state to `NotConnected`.

---

### 🔐 `/connect`
- **Method:** `POST`  
- **Purpose:** Authenticates a user (admin or customer).  
- **Request Body:**
```json
{
  "username": "admin",
  "password": "1234"
}
````

* **Behavior:**

  * Authenticates against stored credentials in preferences:

    * `ADMIN_ID_KEY` / `ADMIN_PASS_KEY`
    * `USER_ID_KEY` / `USER_PASS_KEY`
  * On success, updates `wifiStatus` to either `AdminConnected` or `UserConnected`.
  * Rejects login if another client is already authenticated.

---

### ❌ `/disconnect`

* **Method:** `GET`
* **Purpose:** Forces a logout for the current user.
* **Behavior:**

  * Clears the connection state (`wifiStatus = NotConnected`).
  * May be triggered automatically after heartbeat timeout or manually.

---

### 📈 `/monitor`

* **Method:** `GET`
* **Purpose:** Retrieves current sensor values and system readings.
* **Returns Example:**

```json
{
  "capVoltage": 320.5,
  "current": 1.42,
  "temperatures": [24.3, 25.0, 24.9]
}
```

* **Includes:**

  * Capacitor voltage
  * Current sensor value
  * Temperatures from all available DS18B20 sensors

---

### 🛠️ `/control`

* **Method:** `POST`
* **Purpose:** Main configuration and control endpoint.
* **Request Body Format:**

```json
{
  "action": "set",
  "target": "ledFeedback",
  "value": true
}
```

---

#### ✅ Valid Targets

| Target           | Action | Description                                            |
| ---------------- | ------ | ------------------------------------------------------ |
| `reboot`         | `set`  | Reboots the ESP32                                      |
| `reset`          | `set`  | Resets all preferences and restarts the device         |
| `ledFeedback`    | `set`  | Enables or disables LED feedback (pref: `LEDFB`)       |
| `onTime`         | `set`  | Sets ON duration in ms (pref: `ONTIM`)                 |
| `offTime`        | `set`  | Sets OFF duration in ms (pref: `OFFTIM`)               |
| `relay`          | `set`  | Turns main input relay ON or OFF                       |
| `output1–10`     | `set`  | Controls individual output pins                        |
| `desiredVoltage` | `set`  | Sets target output voltage (pref: `DOUTV`)             |
| `acFrequency`    | `set`  | Sets AC line frequency (pref: `ACFRQ`)                 |
| `chargeResistor` | `set`  | Sets charge resistor value (pref: `CHRES`)             |
| `dcVoltage`      | `set`  | Sets DC bus voltage (pref: `DCVLT`)                    |
| `status`         | `get`  | Returns current system state (`Idle`, `Running`, etc.) |
| `outputAccess`   | `set`  | Updates access flags for 10 outputs (`OUTxxF`)         |

* **Note:** Supports **batched updates** in one JSON body.

---

## 🔐 Security & Session Handling

* Only **one device may be connected** at a time — either a user or an admin.
* Credentials are stored in ESP32 NVS preferences and can be changed via control interface.
* If no `/heartbeat` is received from the connected device for **3 seconds**, the system:

  * Marks the client as disconnected.
  * Resets `wifiStatus` to `NotConnected`.

---

## 🧠 System Architecture Notes

* All preferences (timings, credentials, voltage settings) are managed via `ConfigManager` and persist across reboots.
* Sensor readings are abstracted through dedicated classes: `TempSensor`, `CurrentSensor`, `HeaterManager`, etc.
* The device runs on a modular RTOS-based architecture and minimizes CPU usage by using FreeRTOS tasks and watchdogs.

---

## 📁 Example Configuration Keys

| Key Name          | Description                     |
| ----------------- | ------------------------------- |
| `ONTIM`           | ON-time duration in ms          |
| `OFFTIM`          | OFF-time duration in ms         |
| `DCVLT`           | DC voltage in volts             |
| `DOUTV`           | Desired output voltage          |
| `LEDFB`           | LED feedback (bool)             |
| `ACFRQ`           | AC frequency                    |
| `CHRES`           | Charge resistor                 |
| `OUT1F`–`OUT10F`  | Access flags per output channel |
| `ADMID` / `ADMPW` | Admin login credentials         |
| `USRID` / `USRPW` | User login credentials          |

---

## 🚀 Getting Started

To connect and control the device:

1. Join the ESP32’s Wi-Fi AP (default SSID: `PDis_`, password: `1234567890`)
2. Open a browser at `http://192.168.4.1`
3. Authenticate via `/connect`
4. Poll `/monitor` or send control commands via `/control`

---

## 📞 Support

For support or contributions, feel free to open an issue or submit a pull request.

---

```

