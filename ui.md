# Web UI Practical Guide

This document explains how the web UI is organized and what settings live under
each tab. It focuses on day to day usage and the settings you can change.

## Global layout and controls

- Sidebar tabs: Dashboard, User, Control, Admin, Device, Live.
- Status strip: device state (Shutdown, Idle, Running, Error) and cooling state.
- Global actions: Mute, Power (Run or Stop), Confirm wires cool, Stop test,
  Calibration, Live Control, Disconnect.

## Dashboard tab

- Setup banner with missing items and quick actions to open Setup or Calibration.
- Live gauges: bus voltage, current, capacitance, ADC raw, and temperature
  channels (board and heatsink).
- Status indicators: AC present, relay state, ready or off LEDs.
- Wire overview: outputs and presence indicators.
- Session snapshot: energy, duration, peak power, peak current.

## User tab

- Account settings:
  - Current password.
  - New password.
  - Device ID label (display name).
- Output access:
  - Enable or disable which outputs are allowed for RUN and calibration.
- Note: the User tab is locked until setup is complete.

## Control tab (manual)

- Manual controls:
  - Fan speed slider with percent readout.
  - Relay toggle.
  - Device info: device ID, HW version, SW version.
- Manual outputs:
  - Toggle Output 1 through Output 10 directly.
  - Any manual action switches to manual mode and stops auto control.

## Admin tab

- Setup Wizard control card:
  - Status pills: Setup, Config, Calibration, Run.
  - Stage, substage, and wire index fields for resume and troubleshooting.
  - Missing config and missing calibration lists.
  - Per wire status grid (done, running, pending).
  - Floor, presence, and cap bank status.
  - Reset options: clear models, clear wire params, clear floor params.
  - Reset Setup button.
- Admin credentials:
  - Current password is required.
  - New admin username.
  - New admin password.
- Wi-Fi Station:
  - SSID and password (leave blank to keep current).
- Wi-Fi Access Point:
  - AP SSID and password (leave blank to keep current).

## Device tab

Left column: Device Settings

- Sampling and Power:
  - Sampling Rate (Hz).
  - Charge Resistor (Ohm).
  - Current Trip Limit (A).
- Thermal Safety:
  - Temp Warning (C).
  - Temp Trip (C).
- Presence:
  - Presence Min Ratio (default 0.70).
  - Presence Window (ms).
  - Presence Fail Count (default 3).
- Thermal Model:
  - No direct inputs; values come from calibration.

Right column: Nichrome Calibration (two subtabs)

- Nichrome subtab:
  - Floor settings: thickness, material, max floor temp (<= 35 C), floor switch
    margin, nichrome final temp, NTC linked channel.
  - Wire resistivity (Ohm per meter).
  - Per wire resistances R01 to R10 (Ohm).
- Energy Control subtab:
  - Wire gauge (AWG).

Actions:

- Save applies device and nichrome settings.
- Reset restores the last loaded values.

## Live tab

- Live stage visualization of outputs and wire presence.
- Current session card:
  - Session state (idle or running).
  - Energy, duration, peak power, peak current.
- Quick actions:
  - Session History.
  - Calibration.
  - Live Control chart.
  - Error details (last stop or fault).
  - Log viewer.

### Calibration modal (opened from Live)

- Temp model calibration:
  - Target temperature, start, stop, status and elapsed time.
- Wire test:
  - Target temperature, start, stop, and live status.
- NTC calibration:
  - Multi point calibration: target temp, sample interval, timeout, start, stop.
  - Beta calibration: reference temp and apply button.
- Floor model calibration:
  - Target temp, duty percent, ambient time, heat time, cool time,
    sampling interval, and wire index.
- Presence probe:
  - Run probe to update wire presence.
- History tools:
  - Load saved history, refresh list, clear buffer.
- Chart tools:
  - Jump to latest, pause or resume, and live time readout.

## Setup wizard (overlay)

- Admin only flow that blocks user access until setup is complete.
- Guides through credentials, Wi-Fi, device settings, wire config, and
  calibrations.
- Shows missing items and per step status.
- Resumes interrupted wire or floor calibration by showing the active wire and
  stage.
- Finish marks setup complete once required items are satisfied.

## Run gating and safety behavior

- RUN is disabled until required configuration and calibrations are complete.
- Missing or disabled wires are never driven.
- Any fault or over temperature requires stopping and reviewing the error log.
