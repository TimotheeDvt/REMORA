# Remora - A Motion-Controlled Musical Instrument

**Remora** is a digital musical instrument built around a martial arts staff (bô) augmented with an IMU sensor. Physical gestures such as spinning, tilting, striking, sweeping are translated in real time into expressive audio synthesis. The name *NIME* in the codebase stands for **New Instrument for Musical Expression**, and reflects the instrument design philosophy.

The audio side ships as **REMORA** (*Real-time Expressive Motion to Output Routing Audio*), a JUCE plugin that turns the incoming motion stream into sound.

---

## Project Overview

The instrument consists of two parts:

**Firmware** - An ESP32-S2 microcontroller mounted on the staff reads a 9-DOF IMU (MPU-9250) at 100 Hz and streams orientation data (quaternion, accelerometer, gyroscope, magnetometer) over Wi-Fi as OSC packets.

**Software** - REMORA, a JUCE-based VST3/Standalone audio plugin, receives the OSC stream, applies a user-defined calibration, extracts musical parameters from the motion, and drives an internal additive synthesiser with multiple mapping strategies selectable at runtime.

The two communicate over a local Wi-Fi network using UDP/OSC on port 8000.

```
[ Bô Staff ]
  └─ MPU-9250 IMU
  └─ SparkFun ESP32-S2 Thing+
       │  Wi-Fi / UDP / OSC  (port 8000)
       ▼
[ Host Computer ]
  └─ REMORA Plugin (VST3 / Standalone)
       ├─ OscReceiverManager   - receives and parses packets
       ├─ REMORAProcessor      - calibration, quaternion maths
       ├─ BoStaffSynth         - 4-voice additive synthesiser
       └─ IMappingStrategy     - swappable gesture-to-sound mappings
```

---

## Repository Structure

```
.
├── firmware/               # PlatformIO project for the ESP32-S2
│   ├── platformio.ini          # Board, platform, and library config
│   └── src/
│       └── main.cpp            # IMU read loop + OSC transmission
│
├── software/               # JUCE CMake project (VST3 + Standalone)
│   ├── CMakeLists.txt
│   ├── Makefile                # Convenience wrapper around CMake
│   ├── readme.md               # Deep technical dive into every mapping strategy's DSP
│   ├── IDEAS.md                # Scratchpad of in-progress mapping ideas
│   ├── JUCE/                   # JUCE library (cloned locally, not tracked here)
│   ├── Assets/
│   │   └── logo.png
│   └── Source/
│       ├── PluginProcessor.{h,cpp}   # REMORAProcessor: calibration, OSC bridge
│       ├── PluginEditor.{h,cpp}      # REMORAEditor: main UI
│       ├── DATA/
│       │   ├── IMUData.h             # Lock-free IMU data store (seqlock)
│       │   └── OrientationPoint.h    # Timestamped quaternion for trail rendering
│       ├── DSP/
│       │   ├── MathHelpers.h         # Quaternion / vector maths
│       │   ├── IMappingStrategy.h    # Abstract mapping interface
│       │   ├── BoStaffSynth.{h,cpp}  # 4-voice additive synth engine
│       │   └── Mappings/             # One class per mapping strategy (see below)
│       ├── OSC/
│       │   └── OscReceiverManager.{h,cpp} # JUCE OSCReceiver wrapper
│       └── UI/                       # Calibration overlay, raw-data/debug/DSP
│                                     # diagnostic windows, spectrum analyser, styling
├── hardware/
│   ├── Assets/                           # Reference models for ESP32, MPU chip and battery holder
│   ├── PRINTABLE/                        # Exported STL/gcode ready to slice and print
│   ├── case_vscode.scad                  # Parametric casing model (OpenSCAD Customizer)
│   └── base_print.scad                   # Utility to export a flat cutting/drilling pattern
│
└── tests/                   # OSC + gesture-mapping prototyping and test tools
    ├── testOSCReceiver.py                    # Minimal Python OSC listener on port 8000
    ├── exploration_tests/                    # PyQt/matplotlib gesture-simulation sandbox
    ├── PlugData/                             # Pure Data patches + mock-data generator
    └── PurrData/Karplus-Strong/              # Karplus-Strong string synthesis patches
```

---

## Building the Firmware

### Prerequisites

- [PlatformIO](https://platformio.org/) (CLI or VS Code extension)
- The **SparkFun ESP32-S2 Thing+** connected over USB

### Steps

1. Edit `firmware/src/main.cpp` and update the Wi-Fi credentials and target IP to match your network:

   ```cpp
   const char *WIFI_SSID     = "YOUR_SSID";
   const char *WIFI_PASSWORD = "YOUR_PASSWORD";
   const IPAddress outIp(192, 168, 12, 1); // host machine running the REMORA plugin
   ```

2. Flash the firmware:

   ```bash
   cd firmware
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
- Loops at 100 Hz, sending `/esp32/imu ax ay az gx gy gz mx my mz qw qx qy qz <ip>` to the configured host address on port 8000

---

## Building the Software

### Prerequisites

- **CMake 3.22+**
- **Ninja** build system
- A C++17-capable compiler (MSVC 2022 on Windows, Clang/GCC on macOS/Linux)
- JUCE cloned as a subdirectory at `software/JUCE/`

  ```bash
  cd software
  git clone https://github.com/juce-framework/JUCE.git
  ```

### Steps

Using the provided `Makefile`:

```bash
cd software
make clean    # remove build directory
make          # configure + build
make build    # build only
```

Build artefacts are written to `build/REMORA_artefacts/`:

- `VST3/REMORA.vst3` - load in any VST3 host (DAW, Carla, etc.)
- `Standalone/REMORA` - run directly without a DAW

### Running

1. Ensure the ESP32 is streaming to your machine (or use `tests/testOSCReceiver.py` to verify packets are arriving on port 8000).
2. Launch the standalone or load the VST3 in your DAW.
3. Click **CONNECT** to start the OSC receiver on port 8000.
4. Click **CALIBRATE** and follow the three-pose procedure to align the sensor's local frame with musical space.
5. Select a mapping from the dropdown and move the staff. Use **RAW DATA**, **DEBUG**, and **DSP** to open the diagnostic windows if needed.

---

## Mapping Strategies

All mappings implement the `IMappingStrategy` interface and can be switched at runtime with no audio interruption. They are registered in `BoStaffSynth`, in the order below, from the simplest direct mappings up to the full Azimut engine and its variants. See [`software/readme.md`](software/readme.md) for a full technical breakdown of the DSP behind each one.

| Mapping | Core idea |
|---|---|
| **Simple** | Single sine wave. Tilt = frequency, absolute roll = a tilt-based volume gate. Good for testing. |
| **Bowed Chord** | Gyroscope speed = bow pressure. Yaw selects one of 4 chords, roll overrides into a secondary set past a threshold, acceleration transients add a physical-modeling-style noise stroke. |
| **Lead + Drone** | Tilt drives a major-scale melody. Three drone voices stay harmonically locked below it; yaw crossfades and pans them. |
| **Spin Filter** | Rotation speed climbs a pentatonic scale (root + fifth). Roll sweeps a movable low-pass "ceiling" over 6 harmonic partials. |
| **Martial Effort** | Full Laban Effort framework (Weight, Time, Space, Flow) extracted from the motion. Weight opens up to 4 polyphonic voices playing Major/Minor/7th/Diminished chords based on spin plane and direction. |
| **Martial Momentum** | Shifts from scale-quantization to trajectory-driven momentum: spin plane/direction picks a root note, accumulated spin count sweeps a filter, and axial jerk detection triggers percussive thrusts. |
| **Azimut** | Finishes what Martial Momentum started with a true world-frame facing direction, combined with spin plane + direction for an 8-way root note table. Keeps Martial Momentum's filter sweep and thrust detection. |
| **Azimut+** | Azimut variant where the filter cutoff tracks instantaneous rotation speed directly instead of the accumulated spin-count sweep. |
| **Azimut Reverb** | Azimut variant where Laban Flow - previously computed and discarded - drives a reverb send instead of vibrato. Free, loose motion (flow_free) opens a longer, brighter tail; bound, tense motion collapses it back toward a short, damped, near-dry space. |
| **Speed Gate** | Speed-gated crossfade: a simple pentatonic melody below ~120°/s, the full Azimut engine above ~180°/s, linearly interpolated in between. |
| **Spin Voices** | Builds a sustained chord one note at a time - each of the 4 spin-plane/direction combos permanently owns one voice, which freezes in place until that combo is revisited. |

---

## Calibration

The plugin uses a three-pose calibration to build a correction quaternion that maps the sensor's arbitrary mounting orientation to a consistent musical frame:

1. **Pose A** - staff horizontal, pointing forward
2. **Pose B** - staff vertical, pointing up
3. **Pose C** - staff horizontal, pointing right

After recording all three poses the plugin computes an orthonormal rotation matrix via polar decomposition and converts it to a quaternion applied to every subsequent reading. This means the instrument behaves identically regardless of how the sensor is physically oriented or mounted on the staff.

---

## Hardware / Casing

`hardware/case_vscode.scad` is a parametric OpenSCAD model (Customizer variables at the top of the file) of the staff-mounted enclosure for the ESP32-S2, MPU-9250, and a 2xAAA battery holder, with a strap-and-screw closure. `render_mode` switches between rendering the cap, the base, both, or neither for faster iteration; `hardware/base_print.scad` projects the base down to a flat pattern for cutting/drilling references. Ready-to-slice output lives in `hardware/PRINTABLE/` (`BASE.stl`, `CAP.stl`).

---

## Future Directions

- **Gesture-triggered mode switching** - detect specific motion signatures (e.g. a sharp axial tap while stationary) to cycle between mappings without touching the UI.
- **Merged rest/motion mapping** - blend Azimut's root-note logic with a pitch/roll-driven mode while the staff is at rest, rather than switching mappings outright.
- **Two-IMU configuration** - mount sensors at both ends of the staff to independently track each tip and derive bow speed, contact point, and crossing angle.
- **Laban Effort extensions** - the current Martial/Azimut mappings extract Weight, Time, Space, and Flow. Mapping it to other targets (e.g. filter resonance) remains unexplored.
- **Machine learning gesture recognition** - train a lightweight classifier on recorded gesture sequences to trigger discrete musical events (note attacks, chord changes, FX toggles) alongside the continuous mappings.

---

## Credits

Instrument design, firmware, and software by **Timothée D.**.

Built with [JUCE](https://juce.com/), [PlatformIO](https://platformio.org/), [hideakitai/MPU9250](https://github.com/hideakitai/MPU9250), and [CNMAT/OSC](https://github.com/CNMAT/OSC).