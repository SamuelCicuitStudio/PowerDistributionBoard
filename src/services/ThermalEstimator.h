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
        double tauSec         = NAN;
        double kLoss          = NAN; // W/K
        double thermalC       = NAN; // J/K
        double maxPowerW      = NAN;
        double wireKpSuggest  = NAN;
        double wireKiSuggest  = NAN;
        double floorKpSuggest = NAN;
        double floorKiSuggest = NAN;
        double wireKpCurrent  = NAN;
        double wireKiCurrent  = NAN;
        double floorKpCurrent = NAN;
        double floorKiCurrent = NAN;
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
    double estimateMaxPowerW(const CalibrationRecorder* calib) const;
};

#define THERMAL_EST ThermalEstimator::Get()

#endif // THERMAL_ESTIMATOR_H
