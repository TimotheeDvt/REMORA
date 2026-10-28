#pragma once

// One sensor packet, after calibration, as every mapping sees it.
struct StaffSoundParams {
    float pitch = 0.f;
    float roll  = 0.f;
    float yaw   = 0.f;
    // Raw sensor axes (chip frame), as sent by the firmware.
    float gx = 0.f, gy = 0.f, gz = 0.f;
    float ax = 0.f, ay = 0.f, az = 0.f;
    // Magnetometer (uT)
    float mx = 0.f, my = 0.f, mz = 0.f;
    // Calibrated - passed through so mappings can rotate vectors into world frame without Euler reconstruction errors.
    float qw = 1.f, qx = 0.f, qy = 0.f, qz = 0.f;
    // Angular velocity (deg/s) and specific force (g, gravity included) in the calibrated world frame.
    float worldGyroX = 0.f, worldGyroY = 0.f, worldGyroZ = 0.f;
    float worldAccelX = 0.f, worldAccelY = 0.f, worldAccelZ = 1.f;
    // Time since the previous packet, from the sensor clock.
    float deltaTimeSeconds = 0.01f;
    bool  isReceivingValidData = false;
};
