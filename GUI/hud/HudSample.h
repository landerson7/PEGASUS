#pragma once

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
};
