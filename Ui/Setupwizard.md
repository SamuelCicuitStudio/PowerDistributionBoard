Setup Wizard (full-screen module)
=================================

Purpose
-------
- Full-screen setup wizard (not a tab, not an overlay) that fills the shell.
- Used on first boot to guide the user through initial configuration in 10 steps.
- Can also be opened from the Dashboard tab via the Setup button.
- If not completed, Dashboard shows a reminder notification with Resume and Cancel.
- Each step is self-contained and does not reference existing tabs directly.
- Some steps are skippable; others are mandatory and block completion.

General behavior
----------------
- Progress shows 1 to 10 with step titles.
- Back and Next are available unless the step blocks it.
- Skip is shown only on skippable steps.
- Each step saves its own data before moving forward.
- Cancel marks wizard as incomplete and triggers Dashboard reminder.
- Completion marks setup complete and hides the reminder.

Step list and required settings
-------------------------------

Step 1 - Welcome (required)
- Goal: explain the process and what will be configured.
- Content:
  - Short description of the setup flow and estimated time.
  - Notice about which steps are skippable and which are mandatory.
- Actions: Start, Cancel.

Step 2 - Credentials (skippable)
- Goal: set user and/or admin credentials.
- User credentials:
  - Current password
  - New password
- Admin credentials:
  - Current admin password (required to apply admin changes)
  - New admin username (optional)
  - New admin password (optional)
- Actions: Save and Continue, Skip.

Step 3 - Wi-Fi setup (skippable)
- Goal: configure Station and AP credentials.
- Station:
  - Wi-Fi SSID
  - Wi-Fi password
- Access Point (AP):
  - AP SSID
  - AP password
- Actions: Save and Continue, Skip.

Step 4 - Allowed outputs (required)
- Goal: choose which outputs the user is allowed to use.
- Settings:
  - Output allow list (Outputs 1 to 10, toggle per output).
- Actions: Save and Continue (required).

Step 5 - Sensor zero + presence check (required)
- Goal: run required hardware checks before using outputs.
- Flow (must be done in order):
  1) Capacitance measurement
  2) ACS current sensor zero
  3) Presence check
- Presence results:
  - Wire 01 to Wire 10, status per wire (connected / not connected).
- Actions:
  - Run Capacitance Measurement
  - Run ACS Zero
  - Run Presence Probe
  - Continue only when all required checks succeed.

Step 6 - NTC parameters (skippable)
- Goal: configure NTC sensor parameters.
- Settings:
  - Beta
  - T0 (C)
  - R0 / R25 (Ohm)
  - Fixed pull-up resistor (Ohm)
- Actions: Save and Continue, Skip.

Step 7 - Device settings (required)
- Goal: configure core device behavior (from current Device tab set).
- Sampling and Power:
  - Sampling Rate (Hz, 50 to 500)
  - Charge Resistor (Ohm)
  - Current Trip Limit (A)
  - Current Source (ACS Sensor, CSP Discharge Estimate)
- Thermal Safety:
  - Temp Warning (C)
  - Temp Trip Shutdown (C)
- Thermal Model:
  - Model Target (Wire 1 to Wire 10, Floor)
  - Tau (s)
  - K Loss (W/K)
  - Thermal C (J/K)
- Presence:
  - Presence Min Voltage Drop (V)
- Floor Settings:
  - Floor Thickness (20 to 50 mm)
  - Nichrome Final Temp (C)
  - NTC Linked Channel (1 to 10)
  - Floor Material (Wood, Epoxy, Concrete, Slate, Marble, Granite)
  - Max Floor Temp (<= 35 C)
  - Floor Switch Margin (C)
- Nichrome Calibration:
  - Wire Resistivity (Ohm/m)
  - R01 to R10 (Ohm)
- Energy Control:
  - Wire Gauge (AWG 1 to 40)
- Actions: Save and Continue (required).

Step 8 - Wire calibration (skippable)
- Goal: run a single-wire calibration if needed.
- Settings and controls:
  - Target Temp (C)
  - Start Wire Test / Stop Wire Test
- Status fields (read-only):
  - State, Mode, Purpose, Active Wire
  - NTC Temp, Model Temp
  - Packet, Frame
- Actions: Start, Stop, Continue, Skip.

Step 9 - Floor calibration (skippable)
- Goal: run floor calibration recipe if needed.
- Settings:
  - Target Temp (C)
  - Duty (%) (5 to 100)
  - Ambient (min) (>= 1)
  - Heat (min) (>= 1)
  - Cool (min) (>= 0)
  - Interval (ms) (50 to 5000)
  - Wire Index (1 to 10)
- Status fields (read-only):
  - Stage, Running, Done
  - Tau, K, C
- Actions: Start Floor Cal, Continue, Skip.

Step 10 - Finish (required)
- Goal: finalize setup and mark as complete.
- Content:
  - Summary of completed and skipped steps.
  - Reminder that settings can be adjusted later.
- Actions: Finish (marks setup complete).

Notifications and reminders
---------------------------
- If the wizard is not completed:
  - Dashboard shows a reminder to finish setup.
  - User can Resume or Cancel from the reminder.
- During the wizard:
  - Each step can surface inline errors and success notices.
  - Required steps block Next until valid.
