![REMORA logo](software/Assets/logo.svg)
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
│   ├── README.md               # Board/wiring, build/flash steps, OSC wire protocol
│   ├── platformio.ini          # Board, platform, and library config
│   └── src/
│       └── main.cpp            # IMU read loop + OSC transmission
│
├── software/               # JUCE CMake project (VST3 + Standalone)
│   ├── CMakeLists.txt
│   ├── Makefile                # Convenience wrapper around CMake
│   ├── README.md                # Build/run steps, calibration, and a deep dive into every mapping strategy's DSP
│   ├── IDEAS.md                # Scratchpad of in-progress mapping ideas
│   ├── JUCE/                   # JUCE library (cloned locally, not tracked here)
│   ├── Assets/
│   │   └── logo.svg
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
│   ├── README.md                         # Casing model, printing, and battery notes
│   ├── Assets/                           # Reference models for the ESP32 board and MPU chip
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

## Firmware

ESP32-S2 + MPU-9250 read loop, streaming orientation/motion data as OSC over Wi-Fi. See [`firmware/README.md`](firmware/README.md) for wiring, configuration, build/flash steps, and the full OSC wire protocol.

---

## Software

JUCE VST3/Standalone plugin with a visual node-graph mapping engine. See [`software/README.md`](software/README.md) for build steps, running the plugin, calibration, the Node Graph Editor, and a full technical breakdown of every mapping strategy (Simple, Bowed Chord, Lead + Drone, Spin Filter, Martial Effort/Momentum, Azimut and its variants, Speed Gate, Spin Voices).

---

## Hardware

Staff-mounted enclosure carrying the ESP32-S2, MPU-9250, and a 2000 mAh LiPo battery (charged over USB via the board's onboard circuit). See [`hardware/README.md`](hardware/README.md) for the casing model and battery notes.

---

## Credits

Instrument design, firmware, and software by **Timothée D.**.

Built with [JUCE](https://juce.com/), [PlatformIO](https://platformio.org/), [hideakitai/MPU9250](https://github.com/hideakitai/MPU9250), and [CNMAT/OSC](https://github.com/CNMAT/OSC).