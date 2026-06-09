# NIME 2026 JUCE Project

This project is a JUCE-based audio plugin and standalone application developed as a NIME (New Interface for Musical Expression). It functions as an OSC receiver for a custom ESP32-based IMU sensor, translating physical movements and gestures directly into sound.

## How it Works

The software receives high-speed OSC packets over Wi-Fi containing 9DOF data (quaternions, accelerometer, gyroscope, magnetometer). The plugin maintains a relative orientation state based on user calibration, preventing drift and ensuring intuitive control regardless of the performer's starting position. The received motion data is smoothed to minimize latency while preventing audio artifacts, and is then fed into an internal synthesizer.

## Current Mapping

The plugin uses a 4-voice additive chord synthesiser with a bowed-string physical model.

**Pitch (tilt up/down):** Controls the root note, quantized to the chromatic scale across two octaves (C2–C4). Each degree of tilt snaps to the nearest semitone. A 150ms glide smooths transitions between notes.

**Yaw (horizontal swing):** Selects the chord type voiced above the root:
- Far left → minor triad
- Center-left → power chord (open fifth)  
- Center-right → major triad
- Far right → suspended 4th
- High roll + left → minor 7th
- High roll + right → major 7th

Chord changes crossfade over 200ms.

**Roll (twist):** Below 70% twist — controls vibrato depth and spectral brightness (timbre shifts from flute to bowed string). Above 70% twist — unlocks the 7th chord vocabulary in the yaw mapping.

**Gyroscope magnitude (motion speed):** Acts as bow pressure. The staff must be moving to produce sound. Slow motion = quiet sustain. Fast motion = loud, driven, saturated tone. Holding the staff still causes the sound to fade.

**Yaw angular velocity (gz):** Controls tremolo rate and depth — spinning the staff adds amplitude flutter.

**Accelerometer spike:** Striking or sharply changing direction triggers a percussive noise burst layered over the chord. Vertical acceleration (az) controls the brightness of the hit.

**Stereo spread:** The four chord voices are spread L → R, giving the sound spatial width.

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