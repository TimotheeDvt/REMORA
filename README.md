# Bô - A Motion-Controlled Musical Instrument

**Bô** is a digital musical instrument built around a martial arts staff (bô) augmented with an IMU sensor. Physical gestures such as spinning, tilting, striking, sweeping are translated in real time into expressive audio synthesis. The name *NIME* in the codebase stands for **New Instrument for Musical Expression**, and reflects the instrument design philosophy.

---

## Project Overview

The instrument consists of two parts:

**Firmware** - An ESP32-S2 microcontroller mounted on the staff reads a 9-DOF IMU (MPU-9250) at 100 Hz and streams orientation data (quaternion, accelerometer, gyroscope, magnetometer) over Wi-Fi as OSC packets.

**Software** - A JUCE-based VST3/Standalone audio plugin receives the OSC stream, applies a user-defined calibration, extracts musical parameters from the motion, and drives an internal additive synthesiser with multiple mapping strategies selectable at runtime.

The two communicate over a local Wi-Fi network using UDP/OSC on port 8000.

```
[ Bô Staff ]
  └─ MPU-9250 IMU
  └─ SparkFun ESP32-S2 Thing+
       │  Wi-Fi / UDP / OSC  (port 8000)
       ▼
[ Host Computer ]
  └─ JUCE Plugin (VST3 / Standalone)
       ├─ OscReceiverManager   - receives and parses packets
       ├─ PluginProcessor      - calibration, quaternion maths
       ├─ BoStaffSynth         - 4-voice additive synthesiser
       └─ IMappingStrategy     - swappable gesture-to-sound mappings
```

---

## Repository Structure

```
.
├── firmware/                       # PlatformIO project for the ESP32-S2
│   ├── platformio.ini          # Board, platform, and library config
│   └── src/
│       └── main.cpp            # IMU read loop + OSC transmission
│
├── software/                       # JUCE CMake project (VST3 + Standalone)
│   ├── CMakeLists.txt
│   ├── Makefile                # Convenience wrapper around CMake
│   ├── JUCE/                   # JUCE library (not tracked here)
│   ├── Assets/
│   │   └── logo.png
│   └── Source/
│       ├── PluginProcessor.{h,cpp}   # Audio processor, calibration, OSC bridge
│       ├── PluginEditor.{h,cpp}      # Main UI
│       ├── DATA/
│       │   ├── IMUData.h             # Lock-free IMU data store (seqlock)
│       │   └── OrientationPoint.h    # Timestamped quaternion for trail rendering
│       ├── DSP/
│       │   ├── MathHelpers.h         # Quaternion / vector maths
│       │   ├── IMappingStrategy.h    # Abstract mapping interface
│       │   ├── BoStaffSynth.{h,cpp}  # 4-voice additive synth engine
│       │   └── Mappings/             # Mappings implementations
│       ├── OSC/
│       │   └── OscReceiverManager.{h,cpp} # JUCE OSCReceiver wrapper
│       └── UI/                       # UI related utilities and windows
├── hardware/
│   ├── Assets/                           # Models for ESP32, MPU chips and battery holder
│   ├── PRINTABLE/                        # Printable STL for 3D printer
│   ├── case_vscode.scad                  # Main casing model
│   └── base_print.scad                   # Utils to export a pdf design pattern
│
└── tests/                                # OSC Receiver test
```

---

## Building the Firmware

### Prerequisites

- [PlatformIO](https://platformio.org/) (CLI or VS Code extension)
- A **SparkFun ESP32-S2 Thing+** connected over USB

### Steps

1. Edit `firmware/NIME_2026/src/main.cpp` and update the Wi-Fi credentials and target IP to match your network:

   ```cpp
   const char *WIFI_SSID     = "YOUR_SSID";
   const char *WIFI_PASSWORD = "YOUR_PASSWORD";
   const IPAddress outIp(10, 42, 0, 255); // broadcast or host IP
   ```

2. Flash the firmware:

   ```bash
   cd firmware/NIME_2026
   pio run --target upload
   ```

3. Monitor serial output to confirm connection:

   ```bash
   pio device monitor --baud 115200
   ```

   You should see the board print its local IP and begin streaming at 100 Hz.

### What the Firmware Does

- Initialises the MPU-9250 over I2C (SDA = GPIO 1, SCL = GPIO 2)
- Connects to Wi-Fi and sends a `/esp32/connected <ip>` handshake packet
- Loops at 100 Hz, sending `/esp32/imu ax ay az gx gy gz mx my mz qw qx qy qz <ip>` to the configured broadcast/host address on port 8000

---

## Building the Software

### Prerequisites

- **CMake 3.22+**
- **Ninja** build system
- A C++17-capable compiler (MSVC 2022 on Windows, Clang/GCC on macOS/Linux)
- JUCE cloned as a subdirectory at `software/NIME_2026_JUCE/JUCE/`

  ```bash
  cd software/NIME_2026_JUCE
  git clone https://github.com/juce-framework/JUCE.git
  ```

### Steps

```bash
cd software/NIME_2026_JUCE

# Configure
cmake -B build -DCMAKE_BUILD_TYPE=Release -G Ninja

# Build
cmake --build build --config Release
```

Or use the provided `Makefile`:

```bash
make          # configure + build
make build    # build only
make clean    # remove build directory
```

Build artefacts are written to `build/NIMEReceiver_artefacts/`:

- `VST3/NIMEReceiver.vst3` - load in any VST3 host (DAW, Carla, etc.)
- `Standalone/NIMEReceiver` - run directly without a DAW

### Running

1. Ensure the ESP32 is streaming to your machine (or use `other_tests/testOSCReceiver.py` to verify packets are arriving on port 8000).
2. Launch the standalone or load the VST3 in your DAW.
3. The plugin auto-connects to port 8000 on startup.
4. Optionally click **CALIBRATE** and follow the three-pose procedure to align the sensor's local frame with musical space.
5. Select a mapping from the dropdown and move the staff.

---

## Mapping Strategies

All mappings implement the `IMappingStrategy` interface and can be switched at runtime with no audio interruption.

| Mapping | Core idea |
|---|---|
| **Simple (Pitch+Roll)** | Single sine wave. Tilt = frequency, twist = volume. Good for testing. |
| **Bowed Chord** | Gyroscope speed = bow pressure. Tilt = root note, yaw = chord quality, roll = timbre/vibrato. |
| **Lead + Drone** | Tilt drives a major-scale melody. Yaw modulates a sustained drone bass underneath. |
| **Spin Filter** | Rotation speed climbs a pentatonic scale. Roll sweeps a harmonic cutoff filter. |
| **Bozendo** | Full Laban Effort framework. Weight, Space, Time, and Flow extracted from the motion and mapped to gain, timbre, note selection, and modulation. Classifies spins by axis (horizontal/vertical) and direction. |
| **Bozendo 2** | Bozendo variant. Plays a single pitch class (C/E/G/A) instead of chords. Tilt selects octave. Bow speed fades in octave doublings. |

---

## Calibration

The plugin uses a three-pose calibration to build a correction quaternion that maps the sensor's arbitrary mounting orientation to a consistent musical frame:

1. **Pose A** - staff horizontal, pointing forward
2. **Pose B** - staff vertical, pointing up
3. **Pose C** - staff horizontal, pointing right

After recording all three poses the plugin computes an orthonormal rotation matrix via polar decomposition and converts it to a quaternion applied to every subsequent reading. This means the instrument behaves identically regardless of how the sensor is physically oriented or mounted on the staff.

---

## Future Directions

- **Gesture-triggered mode switching** - detect specific motion signatures (e.g. a sharp axial tap while stationary) to cycle between mappings without touching the UI.
- **Two-IMU configuration** - mount sensors at both ends of the staff to independently track each tip and derive bow speed, contact point, and crossing angle.
- **Laban Effort extensions** - the current Bozendo mappings extract Weight, Time, Space, and Flow. Richer parameterisation (e.g. mapping Flow directly to reverb feedback or filter resonance) remains unexplored.
- **Machine learning gesture recognition** - train a lightweight classifier on recorded gesture sequences to trigger discrete musical events (note attacks, chord changes, FX toggles) alongside the continuous mappings.

---

## Credits

Instrument design, firmware, and software by **Timothée D.**.

Built with [JUCE](https://juce.com/), [PlatformIO](https://platformio.org/), [hideakitai/MPU9250](https://github.com/hideakitai/MPU9250), and [CNMAT/OSC](https://github.com/CNMAT/OSC).