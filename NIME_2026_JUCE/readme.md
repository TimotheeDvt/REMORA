# NIME 2026 JUCE Project

This project is a JUCE-based audio plugin and standalone application developed as a NIME (New Interface for Musical Expression). It functions as an OSC receiver for a custom ESP32-based IMU sensor, translating physical movements and gestures directly into sound.

## How it Works

The software receives high-speed OSC packets over Wi-Fi containing 9DOF data (quaternions, accelerometer, gyroscope, magnetometer). The plugin maintains a relative orientation state based on user calibration, preventing drift and ensuring intuitive control regardless of the performer's starting position. The received motion data is smoothed to minimize latency while preventing audio artifacts, and is then fed into an internal synthesizer.

## Current Mapping

At present, the plugin acts as a simple synthesizer mapping device orientation to sound parameters:

- **Pitch (Tilt Up/Down):** Controls the frequency of a sine wave oscillator. Pointing the device down plays a low tone (~100 Hz), while pointing it up plays a high tone (~1000 Hz).
- **Roll (Twist Left/Right):** Controls the volume (gain) of the oscillator. There is a quiet base volume (5%) that increases up to 20% as the device is twisted away from the center.

## Future Directions

- Implement mode switching triggered by specific gesture conditions (e.g., changing modes when the device is rolling but not translating).
- Extract and utilize Laban Effort Descriptors from the raw sensor data to map the qualitative feeling of the movement (Weight, Space, Time, Flow) to advanced synthesis parameters.

---

## Building

Requires **CMake 3.15+** and **Visual Studio 2022** (Desktop C++). Outputs to `build/NIMEReceiver_artefacts/`.

Run the following command in the root of the project to generate the build files:
```powershell
cmake -B build -DCMAKE_BUILD_TYPE=Release
```

Once configured, compile the plugin using:
```powershell
cmake --build build --config Release
```
Your compiled `.vst3` or standalone application will be located inside the `build/NIMEReceiver_artefacts/` directory.