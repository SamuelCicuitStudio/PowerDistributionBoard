# Calibration & Wire Test Quick Guide

## NTC Calibration
1) Ensure the NTC is attached to the reference wire and the heatsink sensor is stable.
2) Open the Calibration modal in the Live tab.
3) In “NTC Calibrate”, optionally enter a reference temp; leave blank to use the heatsink sensor.
4) Press **Calibrate NTC**. The class recomputes R0 using the reference and saves it.

## Temperature Model Calibration
1) Select **Temp Model Calibration** to start capture.
2) The backend drives the selected wire with fixed PWM while logging V/I/NTC temperature vs time.
3) After stopping, fit τ, k, C so the simulated curve matches the measured NTC curve.
4) Persist the fitted τ/k/C to NVS via the settings panel.

## Wire Test (setpoint check)
1) In the Calibration modal, enter a target temperature and (optionally) a wire index.
2) Click **Start Wire Test**: the PI loop modulates PWM to hold the target on the NTC wire.
3) Observe live temp, duty, and on/off times; click **Stop Wire Test** to end.

## Notes
- NTC calibration never energizes the wire.
- Model calibration uses PWM to get a clean, repeatable heating profile while recording.
- Wire Test is for validating control at a setpoint; it does not record history.
