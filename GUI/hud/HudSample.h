#pragma once

#include <limits>

struct HudSample {
    double headingDeg = 0;
    double rollDeg = 0;
    double pitchDeg = 0;

    double altitudeFt = 0;
    double vspeedFpm = 0;

    // Optional raw sensors if you want to log/display later
    double ax=0, ay=0, az=0;
    double gx=0, gy=0, gz=0;
    double mx=0, my=0, mz=0;

    double pressureHpa = 0;
    double tempC = 0;

    long long tsMs = 0;

    // Timing/spec-test metrics. NaN means "not measured / unavailable" so the
    // SQLite logger can store NULL instead of a fabricated zero.
    double sensor_read_ms = std::numeric_limits<double>::quiet_NaN();
    double decode_ms = std::numeric_limits<double>::quiet_NaN();
    double display_ms = std::numeric_limits<double>::quiet_NaN();
    double end_to_end_ms = std::numeric_limits<double>::quiet_NaN();
    double sensor_rate_hz = std::numeric_limits<double>::quiet_NaN();
    double display_rate_hz = std::numeric_limits<double>::quiet_NaN();

    int invalid_packets = 0;
    int dropped_packets = 0;
    bool stale_data_flag = false;
    bool data_valid = false;
    bool data_fresh = false;

    double altitudeFt_time_meas = std::numeric_limits<double>::quiet_NaN();
    double vspeedFpm_time_meas = std::numeric_limits<double>::quiet_NaN();
    double pressureHpa_time_meas = std::numeric_limits<double>::quiet_NaN();
    double tempC_time_meas = std::numeric_limits<double>::quiet_NaN();
    double ax_time_meas = std::numeric_limits<double>::quiet_NaN();
    double ay_time_meas = std::numeric_limits<double>::quiet_NaN();
    double az_time_meas = std::numeric_limits<double>::quiet_NaN();
    double gx_time_meas = std::numeric_limits<double>::quiet_NaN();
    double gy_time_meas = std::numeric_limits<double>::quiet_NaN();
    double gz_time_meas = std::numeric_limits<double>::quiet_NaN();
    double mx_time_meas = std::numeric_limits<double>::quiet_NaN();
    double my_time_meas = std::numeric_limits<double>::quiet_NaN();
    double mz_time_meas = std::numeric_limits<double>::quiet_NaN();
    double rollDeg_time_meas = std::numeric_limits<double>::quiet_NaN();
    double pitchDeg_time_meas = std::numeric_limits<double>::quiet_NaN();
    double headingDeg_time_meas = std::numeric_limits<double>::quiet_NaN();
};
