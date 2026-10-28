#pragma once

#include <math.h>

// 6-axis Mahony filter whose accelerometer correction fades out when the accelerometer is not
// measuring gravity alone. On the staff the unit sits ~0.7 m from the centre of rotation, so a
// 2 rev/s spin adds ~11 g of centripetal acceleration; the library's fixed-gain Mahony (Kp = 30)
// would treat that as gravity and tilt the estimate. Here the gain is scaled by two weights:
//   - accel weight: 1 while | |a| - 1 g | < 0.1 g, 0 beyond 0.3 g
//   - gyro weight:  1 below 90 deg/s, 0 above 180 deg/s
// so fast spins are integrated from the gyroscope alone and the accelerometer pulls the tilt back
// once the staff slows down. While the staff is still, the gyro bias is tracked so that heading
// (which the accelerometer cannot correct) drifts as little as the sensor allows.
//
// Frame convention: inputs use the same axis remapping as hideakitai/MPU9250's own filter
// (accel (-ax, ay, az), gyro (gx, -gy, -gz)), so the quaternion stays compatible with the
// plugin's calibration. That remapping mirrors the chip's x axis; see the plugin's computeAlignQuat.
class GatedMahony {
public:
    float kp = 30.0f;
    // false reproduces the plain fixed-gain Mahony (used by the plugin's AnalyzerCheck tool).
    bool gatingEnabled = true;

    // ax..az in g (chip frame), gx..gz in deg/s (chip frame, bias NOT removed), dt in seconds.
    void update(float ax, float ay, float az, float gx, float gy, float gz, float dt) {
        if (!(dt > 0.0f) || dt > 0.1f)
            return;

        trackBias(ax, ay, az, gx, gy, gz, dt);
        gx -= bias_[0];
        gy -= bias_[1];
        gz -= bias_[2];

        constexpr float kDegToRad = 0.017453292519943295f;
        float fx = -ax, fy = ay, fz = az;
        float wx = gx * kDegToRad, wy = -gy * kDegToRad, wz = -gz * kDegToRad;

        const float accelNorm = sqrtf(fx * fx + fy * fy + fz * fz);
        const float gyroDegPerSec = sqrtf(gx * gx + gy * gy + gz * gz);
        accelWeight_ = gatingEnabled
            ? ramp(fabsf(accelNorm - 1.0f), 0.1f, 0.3f) * ramp(gyroDegPerSec, 90.0f, 180.0f)
            : 1.0f;

        if (accelNorm > 1e-6f && accelWeight_ > 0.0f) {
            fx /= accelNorm; fy /= accelNorm; fz /= accelNorm;
            // Estimated gravity direction in the body frame (half magnitude, as in the reference Mahony).
            const float vx = q_[1] * q_[3] - q_[0] * q_[2];
            const float vy = q_[0] * q_[1] + q_[2] * q_[3];
            const float vz = q_[0] * q_[0] - 0.5f + q_[3] * q_[3];
            const float ex = fy * vz - fz * vy;
            const float ey = fz * vx - fx * vz;
            const float ez = fx * vy - fy * vx;
            const float k = kp * accelWeight_;
            wx += k * ex;
            wy += k * ey;
            wz += k * ez;
        }

        const float h = 0.5f * dt;
        const float qa = q_[0], qb = q_[1], qc = q_[2], qd = q_[3];
        q_[0] += (-qb * wx - qc * wy - qd * wz) * h;
        q_[1] += ( qa * wx + qc * wz - qd * wy) * h;
        q_[2] += ( qa * wy - qb * wz + qd * wx) * h;
        q_[3] += ( qa * wz + qb * wy - qc * wx) * h;
        const float n = 1.0f / sqrtf(q_[0] * q_[0] + q_[1] * q_[1] + q_[2] * q_[2] + q_[3] * q_[3]);
        for (float& c : q_) c *= n;
    }

    void setQuaternion(float w, float x, float y, float z) { q_[0] = w; q_[1] = x; q_[2] = y; q_[3] = z; }

    // Seeds the bias from a still period at boot (deg/s, chip frame).
    void setGyroBias(float bx, float by, float bz) { bias_[0] = bx; bias_[1] = by; bias_[2] = bz; }

    float w() const { return q_[0]; }
    float x() const { return q_[1]; }
    float y() const { return q_[2]; }
    float z() const { return q_[3]; }
    float biasX() const { return bias_[0]; }
    float biasY() const { return bias_[1]; }
    float biasZ() const { return bias_[2]; }
    float accelWeight() const { return accelWeight_; }

private:
    // 1 at or below `full`, 0 at or above `zero`, linear in between.
    static float ramp(float value, float full, float zero) {
        if (value <= full) return 1.0f;
        if (value >= zero) return 0.0f;
        return (zero - value) / (zero - full);
    }

    void trackBias(float ax, float ay, float az, float gx, float gy, float gz, float dt) {
        constexpr float kStillGyroDegPerSec = 3.0f;  // residual rate, after bias, that still counts as still
        constexpr float kStillAccelErrorG = 0.05f;
        constexpr float kStillHoldSeconds = 0.5f;    // must be still this long before the bias adapts
        constexpr float kBiasTimeConstantSeconds = 2.0f;

        const float rx = gx - bias_[0], ry = gy - bias_[1], rz = gz - bias_[2];
        const float residual = sqrtf(rx * rx + ry * ry + rz * rz);
        const float accelError = fabsf(sqrtf(ax * ax + ay * ay + az * az) - 1.0f);

        if (residual < kStillGyroDegPerSec && accelError < kStillAccelErrorG) {
            stillSeconds_ += dt;
            if (stillSeconds_ >= kStillHoldSeconds) {
                const float a = dt / kBiasTimeConstantSeconds;
                bias_[0] += a * rx;
                bias_[1] += a * ry;
                bias_[2] += a * rz;
            }
        } else {
            stillSeconds_ = 0.0f;
        }
    }

    float q_[4] = {1.0f, 0.0f, 0.0f, 0.0f};
    float bias_[3] = {0.0f, 0.0f, 0.0f};
    float stillSeconds_ = 0.0f;
    float accelWeight_ = 1.0f;
};
