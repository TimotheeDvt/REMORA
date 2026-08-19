![REMORA logo](Assets/logo.svg)
# REMORA Firmware
### Real-time Expressive Motion to Output Routing Audio

PlatformIO project for the staff-mounted microcontroller. It reads the IMU over I2C and streams orientation data to the host computer over Wi-Fi/OSC for the REMORA plugin (see the [top-level README](../README.md) for the full system picture).

## Hardware

| Part | Role |
|---|---|
| [SparkFun ESP32-S2 Thing Plus](https://www.sparkfun.com/sparkfun-thing-plus-esp32-s2-wroom.html) | Microcontroller, Wi-Fi, onboard LiPo charge circuit |
| MPU-9250 | 9-DOF IMU (accelerometer, gyroscope, magnetometer), wired over I2C |
| 2000 mAh LiPo battery | Powers the unit via the Thing Plus's JST-PH connector |

- I2C: SDA = GPIO 1, SCL = GPIO 2 (`Wire.begin(1, 2)` in `src/main.cpp`), 400 kHz clock.
- MPU-9250 I2C address: `0x68`.
- GPIO 13 drives the board's blue STAT LED: blinking while attempting to connect to Wi-Fi, solid once connected.
- Charging: plug the LiPo into the Thing Plus's JST connector, then power the board over USB. The onboard charge IC handles charging automatically - no separate charger needed. Battery runtime under the firmware's steady 100 Hz sampling rate has not been measured yet.

## Prerequisites

- [PlatformIO](https://platformio.org/) (CLI or the VS Code extension)
- SparkFun ESP32-S2 Thing Plus connected over USB

## Configuration

Before flashing, edit the network settings at the top of `src/main.cpp`:

```cpp
const char *WIFI_SSID     = "YOUR_SSID";
const char *WIFI_PASSWORD = "YOUR_PASSWORD";
const IPAddress outIp(192, 168, 12, 1); // host machine running the REMORA plugin
```

These are compiled into the firmware (there's no runtime config), so a new venue or a new host IP means editing this file and reflashing. `src/main.cpp` currently has real credentials checked into the repo history - swap them for your own network before flashing, and avoid committing real credentials again since this repo is public.

## Build / Flash / Monitor

```bash
cd firmware
pio run                        # build only
pio run --target upload        # build + flash
pio device monitor --baud 115200
```

The PlatformIO environment is `sparkfun_esp32s2_thing_plus` (see `platformio.ini`). On boot the board prints its assigned IP and confirms it is streaming.

## Dependencies

Declared in `platformio.ini`:

- [hideakitai/MPU9250](https://github.com/hideakitai/MPU9250) - IMU driver, also computes the fused quaternion
- [CNMAT/OSC](https://github.com/CNMAT/OSC) - OSC message construction

## Wire Protocol

UDP/OSC, sent from the board to `outIp:8000` (the host running the REMORA plugin); the board itself listens locally on port `8888`.

| Address | Args | Sent |
|---|---|---|
| `/esp32/connected` | `<ip:string>` | Once, right after Wi-Fi connects |
| `/esp32/imu` | `ax ay az gx gy gz mx my mz qw qx qy qz <ip:string>` (12 floats + string) | Every 10 ms (100 Hz), while `mpu.update()` has fresh data |

`ax/ay/az` = accelerometer, `gx/gy/gz` = gyroscope, `mx/my/mz` = magnetometer, `qw/qx/qy/qz` = the fused orientation quaternion computed onboard by the MPU9250 library.

If Wi-Fi drops mid-run, `loop()` detects it, halts sending, and blocks on reconnection before resuming the stream.
