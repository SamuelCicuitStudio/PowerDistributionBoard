/**************************************************************
 *  Author      : Tshibangu Samuel
 *  Role        : Freelance Embedded Systems Engineer
 *  Expertise   : Secure IoT Systems, Embedded C++, RTOS, Control Logic
 *  Contact     : tshibsamuel47@gmail.com
 *  Portfolio   : https://www.freelancer.com/u/tshibsamuel477
 *  Phone       : +216 54 429 793
 **************************************************************/
#ifndef THERMAL_ESTIMATOR_H
#define THERMAL_ESTIMATOR_H

#include <Arduino.h>

// Forward declaration to avoid circular dependency with CalibrationRecorder.
class CalibrationRecorder;

/**
 * ThermalEstimator centralizes model and PI gain suggestions based on
 * current configuration and the last calibration buffer.
 */
class ThermalEstimator {
public:
    struct Result {
        float tauSec        = NAN;
        float kLoss         = NAN; // W/K
        float thermalC      = NAN; // J/K
        float maxPowerW     = NAN;
        float wireKpSuggest = NAN;
        float wireKiSuggest = NAN;
        float floorKpSuggest = NAN;
        float floorKiSuggest = NAN;
        float wireKpCurrent = NAN;
        float wireKiCurrent = NAN;
        float floorKpCurrent = NAN;
        float floorKiCurrent = NAN;
    };

    static ThermalEstimator* Get();

    /**
     * Compute suggestions from current CONF values and last calibration buffer.
     * If no calibration samples are available, falls back to conservative guesses.
     */
    Result computeSuggestions(const CalibrationRecorder* calib) const;

    /**
     * Persist thermal parameters (tau, kLoss, C) and optionally PI gains.
     * Any NAN field is ignored.
     */
    void persist(const Result& r);

private:
    ThermalEstimator() = default;
    float estimateMaxPowerW(const CalibrationRecorder* calib) const;
};

#define THERMAL_EST ThermalEstimator::Get()

#endif // THERMAL_ESTIMATOR_H
