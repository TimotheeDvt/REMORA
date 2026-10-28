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

Before flashing, copy `include/secrets.example.h` to `include/secrets.h` and fill in your network:

```cpp
#define WIFI_SSID_VALUE     "YOUR_SSID"
#define WIFI_PASSWORD_VALUE "YOUR_PASSWORD"
```

`include/secrets.h` is git-ignored, so credentials stay out of the repository. The destination IP (the host machine running the REMORA plugin) is still set at the top of `src/main.cpp`:

```cpp
const IPAddress outIp(192, 168, 12, 1);
```

These are compiled into the firmware (there's no runtime config), so a new venue or a new host IP means editing these files and reflashing.

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

- [hideakitai/MPU9250](https://github.com/hideakitai/MPU9250) - IMU driver (its own filter is switched off; orientation comes from `src/GatedMahony.h`)
- [CNMAT/OSC](https://github.com/CNMAT/OSC) - OSC message construction

## Wire Protocol

UDP/OSC, sent from the board to port `8000` of the Wi-Fi gateway (the host running the hotspot and the REMORA plugin), or of `OSC_HOST_IP_VALUE` if defined in `secrets.h`; the board itself listens locally on port `8888`.

| Address | Args | Sent |
|---|---|---|
| `/esp32/connected` | `<ip:string>` | Once, right after Wi-Fi connects |
| `/esp32/imu` | `ax ay az gx gy gz mx my mz qw qx qy qz <ip:string> <seq:int32> <t_us:int32>` (13 floats, string, 2 ints) | Every 10 ms (100 Hz) |

`ax/ay/az` = accelerometer (g), `gx/gy/gz` = gyroscope (deg/s, tracked bias removed), `mx/my/mz` = magnetometer, `qw/qx/qy/qz` = the orientation quaternion, `seq` = packet counter (lost/reordered packet detection), `t_us` = sensor clock in microseconds (the plugin's time step).

Orientation is a 6-axis Mahony filter (`src/GatedMahony.h`) run on every 200 Hz IMU sample. Its accelerometer correction fades out when the accelerometer is not reading gravity alone (|a| more than 0.1 g from 1 g, or rotation above 90 deg/s), because during spins centripetal acceleration on the staff reaches ~11 g. The gyro bias is averaged at boot (keep the staff still for ~1 s) and then tracked whenever the staff is still. The filter keeps the MPU9250 library's axis convention (x mirrored) so existing calibrations stay valid.

If Wi-Fi drops mid-run, `loop()` detects it, halts sending, and blocks on reconnection before resuming the stream.
