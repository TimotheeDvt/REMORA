# Complete Setup Guide — ESP32-S2 Thing Plus + ICM-20600/AK09918

---

## Hardware Overview

### ESP32-S2 Thing Plus
- Single-core 240MHz Xtensa LX7
- Built-in WiFi (no Bluetooth)
- Native USB (appears directly as serial port)
- Qwiic connector — **2 pins exposed** (this is normal, it carries all 4 signals: VCC, GND, SDA, SCL on the same connector internally)

### IMU Board — Seeed Grove 9DOF v1.1
- **ICM-20600** — Accelerometer ±2/±4/±8/±16g + Gyroscope ±250/±500/±1000/±2000°/s
- **AK09918C** — Magnetometer ±4900µT
- I2C addresses: ICM-20600 @ `0x69`, AK09918C @ `0x0C`
- Qwiic/STEMMA QT output connector

---

## Wiring

### Manual jumper wires
```
IMU Board        ESP32-S2 Thing Plus
─────────────────────────────────────
VCC (3.3V)  →   3.3V
GND         →   GND
SCL         →   GPIO 40
SDA         →   GPIO 39
```

---

## Software Installation

### 1. Install VS Code
Download from [code.visualstudio.com](https://code.visualstudio.com)

### 2. Install PlatformIO
VS Code → Extensions (Ctrl+Shift+X) → search **PlatformIO IDE** → Install
Restart VS Code after installation.

### 3. Create a new project
```
PlatformIO Home → New Project
Name:      rod-nime
Board:     SparkFun ESP32-S2 Thing Plus
Framework: Arduino
```

### 4. Replace `platformio.ini` with:
```ini
[env:sparkfun_esp32s2_thing_plus]
platform = espressif32
board = sparkfun_esp32s2_thing_plus
framework = arduino

monitor_speed = 115200

lib_deps =
    seeed-studio/Grove - IMU 9DOF ICM20600+AK09918 @ ^1.0.0
    CNMAT/OSC @ ^1.3.5
```

### 5. Flash workflow
- Connect ESP32 to computer via USB
- On first connection: may need to **hold BOOT button while pressing RESET** to enter flash mode
- PlatformIO toolbar → **Upload** (→ arrow)
- PlatformIO toolbar → **Serial Monitor** (plug icon)

---

## Finding Your Computer's IP Address

The ESP32 sends OSC packets to your computer — you need its local IP:

```bash
# macOS / Linux
ifconfig | grep "inet "

# Windows
ipconfig
```

Look for something like `192.168.1.42` on your local network.

---

## Complete Firmware

Replace `src/main.cpp` entirely with this:

```cpp
#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <OSCMessage.h>
#include "ICM20600.h"
#include "AK09918.h"

// ─── WiFi / OSC Configuration ─────────────────────────────────────────────────
const char*   WIFI_SSID     = "YOUR_SSID";
const char*   WIFI_PASSWORD = "YOUR_PASSWORD";
const char*   OSC_HOST      = "192.168.1.XXX";  // ← your computer's IP
const int     OSC_PORT      = 9000;

// ─── Sampling ─────────────────────────────────────────────────────────────────
#define SAMPLE_RATE_HZ    100
#define SAMPLE_INTERVAL   (1000000 / SAMPLE_RATE_HZ)  // µs

// ─── Madgwick Filter ──────────────────────────────────────────────────────────
// Beta: higher = faster convergence but noisier
//       lower  = smoother but slower to track fast movement
// 0.1 is a good starting point — tune physically
#define MADGWICK_BETA     0.1f

// ─── Shake Detection ──────────────────────────────────────────────────────────
// Threshold in g above which a shake is detected (tune physically)
#define SHAKE_THRESHOLD   1.5f
// Decay per sample — controls how long shake "rings" after impact
#define SHAKE_DECAY       0.92f

// ─── Hardware Objects ─────────────────────────────────────────────────────────
ICM20600  icm(true);   // true = I2C mode
AK09918   ak;

// ─── WiFi / UDP ───────────────────────────────────────────────────────────────
WiFiUDP   udp;

// ─── Madgwick State ───────────────────────────────────────────────────────────
float q0 = 1.0f, q1 = 0.0f, q2 = 0.0f, q3 = 0.0f;

// ─── Runtime State ────────────────────────────────────────────────────────────
uint64_t  lastSampleTime = 0;
float     shakeEnvelope  = 0.0f;

// ─────────────────────────────────────────────────────────────────────────────
// Madgwick AHRS Update
// Inputs: acceleration (g), gyroscope (rad/s), timestep (s)
// Updates global quaternion q0..q3
// ─────────────────────────────────────────────────────────────────────────────
void madgwickUpdate(float ax, float ay, float az,
                    float gx, float gy, float gz, float dt) {
    float recipNorm;
    float s0, s1, s2, s3;
    float qDot0, qDot1, qDot2, qDot3;
    float _2q0, _2q1, _2q2, _2q3;
    float _4q0, _4q1, _4q2;
    float _8q1, _8q2;
    float q0q0, q1q1, q2q2, q3q3;

    // Rate of change of quaternion from gyroscope
    qDot0 = 0.5f * (-q1*gx - q2*gy - q3*gz);
    qDot1 = 0.5f * ( q0*gx + q2*gz - q3*gy);
    qDot2 = 0.5f * ( q0*gy - q1*gz + q3*gx);
    qDot3 = 0.5f * ( q0*gz + q1*gy - q2*gx);

    // Apply feedback only if accelerometer reading is valid
    if (!((ax == 0.0f) && (ay == 0.0f) && (az == 0.0f))) {
        recipNorm = 1.0f / sqrtf(ax*ax + ay*ay + az*az);
        ax *= recipNorm; ay *= recipNorm; az *= recipNorm;

        _2q0 = 2.0f*q0; _2q1 = 2.0f*q1;
        _2q2 = 2.0f*q2; _2q3 = 2.0f*q3;
        _4q0 = 4.0f*q0; _4q1 = 4.0f*q1; _4q2 = 4.0f*q2;
        _8q1 = 8.0f*q1; _8q2 = 8.0f*q2;
        q0q0 = q0*q0; q1q1 = q1*q1; q2q2 = q2*q2; q3q3 = q3*q3;

        s0 = _4q0*q2q2 + _2q2*ax + _4q0*q1q1 - _2q1*ay;
        s1 = _4q1*q3q3 - _2q3*ax + 4.0f*q0q0*q1
           - _2q0*ay - _4q1 + _8q1*q1q1 + _8q1*q2q2 + _4q1*az;
        s2 = 4.0f*q0q0*q2 + _2q0*ax + _4q2*q3q3
           - _2q3*ay - _4q2 + _8q2*q1q1 + _8q2*q2q2 + _4q2*az;
        s3 = 4.0f*q1q1*q3 - _2q1*ax + 4.0f*q2q2*q3 - _2q2*ay;

        recipNorm = 1.0f / sqrtf(s0*s0 + s1*s1 + s2*s2 + s3*s3);
        s0 *= recipNorm; s1 *= recipNorm;
        s2 *= recipNorm; s3 *= recipNorm;

        qDot0 -= MADGWICK_BETA * s0;
        qDot1 -= MADGWICK_BETA * s1;
        qDot2 -= MADGWICK_BETA * s2;
        qDot3 -= MADGWICK_BETA * s3;
    }

    // Integrate to get new quaternion
    q0 += qDot0 * dt; q1 += qDot1 * dt;
    q2 += qDot2 * dt; q3 += qDot3 * dt;

    // Normalize
    recipNorm = 1.0f / sqrtf(q0*q0 + q1*q1 + q2*q2 + q3*q3);
    q0 *= recipNorm; q1 *= recipNorm;
    q2 *= recipNorm; q3 *= recipNorm;
}

// ─────────────────────────────────────────────────────────────────────────────
// Quaternion → Euler Angles (degrees)
// ─────────────────────────────────────────────────────────────────────────────
void quaternionToEuler(float& pitch, float& roll, float& yaw) {
    float sinp = 2.0f * (q0*q2 - q3*q1);
    pitch = (fabsf(sinp) >= 1.0f)
          ? copysignf(90.0f, sinp)
          : asinf(sinp) * 57.2957795f;

    roll = atan2f(2.0f*(q0*q1 + q2*q3),
                  1.0f - 2.0f*(q1*q1 + q2*q2)) * 57.2957795f;

    yaw  = atan2f(2.0f*(q0*q3 + q1*q2),
                  1.0f - 2.0f*(q2*q2 + q3*q3)) * 57.2957795f;
}

// ─────────────────────────────────────────────────────────────────────────────
// OSC Helpers
// ─────────────────────────────────────────────────────────────────────────────
inline void sendFloat(const char* address, float value) {
    OSCMessage msg(address);
    msg.add(value);
    udp.beginPacket(OSC_HOST, OSC_PORT);
    msg.send(udp);
    udp.endPacket();
    msg.empty();
}

void sendAllOSC(float pitch, float roll, float yaw,
                float ax,    float ay,   float az,
                float gyroMag, float shake,
                float mx,    float my,   float mz) {
    sendFloat("/rod/orientation/pitch", pitch);
    sendFloat("/rod/orientation/roll",  roll);
    sendFloat("/rod/orientation/yaw",   yaw);
    sendFloat("/rod/accel/x",           ax);
    sendFloat("/rod/accel/y",           ay);
    sendFloat("/rod/accel/z",           az);
    sendFloat("/rod/gyro/magnitude",    gyroMag);
    sendFloat("/rod/gesture/shake",     shake);
    sendFloat("/rod/mag/x",             mx);
    sendFloat("/rod/mag/y",             my);
    sendFloat("/rod/mag/z",             mz);
}

// ─────────────────────────────────────────────────────────────────────────────
// Setup
// ─────────────────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(500);

    // I2C — fast mode
    Wire.begin();
    Wire.setClock(400000);

    // ── ICM-20600 init ────────────────────────────────────────────────────────
    icm.initialize();
    // ±8g — good range for expressive gestures without clipping
    icm.setAccelScaleRange(ICM20600_ACCEL_RANGE_8G);
    // ±1000°/s — covers fast rod swings
    icm.setGyroScaleRange(ICM20600_GYRO_RANGE_1000DPS);
    Serial.println("ICM-20600 initialized");

    // ── AK09918 init ──────────────────────────────────────────────────────────
    // AK09918 is connected through ICM-20600's aux I2C in some boards,
    // or directly on main I2C bus — try direct first
    err = ak.initialize();
    ak.setMode(AK09918_POWER_DOWN);
    ak.setMode(AK09918_CONTINUOUS_100HZ);
    Serial.println("AK09918 initialized");

    // ── WiFi ──────────────────────────────────────────────────────────────────
    Serial.printf("Connecting to %s", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500); Serial.print(".");
    }
    Serial.printf("\nConnected — ESP32 IP: %s\n",
                  WiFi.localIP().toString().c_str());

    udp.begin(OSC_PORT);
    lastSampleTime = micros();
}

// ─────────────────────────────────────────────────────────────────────────────
// Main Loop
// ─────────────────────────────────────────────────────────────────────────────
void loop() {
    uint64_t now = micros();
    if (now - lastSampleTime < SAMPLE_INTERVAL) return;

    float dt = (now - lastSampleTime) / 1000000.0f;
    lastSampleTime = now;

    // ── Read ICM-20600 ────────────────────────────────────────────────────────
    float ax = icm.getAccelerationX() / 1000.0f;  // mg → g
    float ay = icm.getAccelerationY() / 1000.0f;
    float az = icm.getAccelerationZ() / 1000.0f;

    // Gyro comes in deg/s, convert to rad/s for Madgwick
    float gx = icm.getGyroscopeX() * 0.01745329f;
    float gy = icm.getGyroscopeY() * 0.01745329f;
    float gz = icm.getGyroscopeZ() * 0.01745329f;

    // ── Read AK09918 ──────────────────────────────────────────────────────────
    int32_t mx_raw, my_raw, mz_raw;
    ak.getData(&mx_raw, &my_raw, &mz_raw);
    // Scale to µT (AK09918 resolution: 0.15µT/LSB)
    float mx = mx_raw * 0.15f;
    float my = my_raw * 0.15f;
    float mz = mz_raw * 0.15f;

    // ── Madgwick fusion ───────────────────────────────────────────────────────
    madgwickUpdate(ax, ay, az, gx, gy, gz, dt);

    // ── Euler angles ──────────────────────────────────────────────────────────
    float pitch, roll, yaw;
    quaternionToEuler(pitch, roll, yaw);

    // ── Gyro magnitude (overall rotation speed, deg/s) ───────────────────────
    float gyroMag = sqrtf(gx*gx + gy*gy + gz*gz) * 57.2957795f;

    // ── Shake envelope ────────────────────────────────────────────────────────
    float accelMag     = sqrtf(ax*ax + ay*ay + az*az);
    float accelDynamic = fabsf(accelMag - 1.0f);  // subtract gravity
    if (accelDynamic > SHAKE_THRESHOLD) shakeEnvelope = 1.0f;
    shakeEnvelope *= SHAKE_DECAY;

    // ── Send all OSC ──────────────────────────────────────────────────────────
    sendAllOSC(pitch, roll, yaw, ax, ay, az, gyroMag, shakeEnvelope,
               mx, my, mz);

    // ── Debug — uncomment to verify in Serial Monitor ─────────────────────────
    // Serial.printf("P:%.1f R:%.1f Y:%.1f | shake:%.2f | mag:%.1f,%.1f,%.1f\n",
    //               pitch, roll, yaw, shakeEnvelope, mx, my, mz);
}
```

---

## OSC Messages Reference

Everything the ESP sends, and what it represents musically:

```
/rod/orientation/pitch   float  [-90,  90]   tilt forward/back
/rod/orientation/roll    float  [-180, 180]  tilt left/right
/rod/orientation/yaw     float  [-180, 180]  rotation around vertical axis
/rod/accel/x             float  [g units]    raw acceleration X
/rod/accel/y             float  [g units]    raw acceleration Y
/rod/accel/z             float  [g units]    raw acceleration Z
/rod/gyro/magnitude      float  [deg/s]      overall rotation speed
/rod/gesture/shake       float  [0, 1]       impact/shake intensity envelope
/rod/mag/x               float  [µT]         magnetic field X
/rod/mag/y               float  [µT]         magnetic field Y
/rod/mag/z               float  [µT]         magnetic field Z
```

---

## Verify Data — Python Listener

Before touching any music software, run this to confirm packets are arriving:

```python
# pip install python-osc
from pythonosc import dispatcher, osc_server

def handler(address, *args):
    print(f"{address}: {[f'{a:.2f}' if isinstance(a, float) else a for a in args]}")

d = dispatcher.Dispatcher()
d.map("/rod/*", handler)

server = osc_server.ThreadingOSCUDPServer(("0.0.0.0", 9000), d)
print("Listening on port 9000 — move the rod...")
server.serve_forever()
```

---

## Troubleshooting

| Symptom | Likely cause | Fix |
|---------|-------------|-----|
| ESP not detected by computer | S2 needs boot mode | Hold BOOT + press RESET before uploading |
| "ICM-20600 not found" | I2C wiring or address | Check SDA/SCL pins, confirm AD pin sets `0x69` |
| No OSC packets arriving | Wrong IP or firewall | Double-check `OSC_HOST`, disable firewall temporarily |
| Orientation drifts slowly | Madgwick beta too low | Increase `MADGWICK_BETA` to 0.15–0.2 |
| Orientation jittery | Madgwick beta too high | Decrease `MADGWICK_BETA` to 0.05 |
| Shake never triggers | Threshold too high | Lower `SHAKE_THRESHOLD` to 0.8–1.0 |