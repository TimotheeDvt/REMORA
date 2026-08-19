![REMORA logo](Assets/logo.svg)
# REMORA Plugin
### Real-time Expressive Motion to Output Routing Audio

A real-time, dynamic **JUCE-based audio application** engineered to receive incoming multi-sensor IMU data via **Open Sound Control (OSC)**. The software serves as a modular audio mapping engine, converting spatial movements and hardware interactions into complex sound synthesis.

## Core Features

* **Standalone Application:** Ships today as a standalone desktop app; JUCE's plugin formats (VST3, AU, ...) are just a `FORMATS` entry away in `CMakeLists.txt` if a hosted-in-a-DAW build is ever needed.
* **Visual Node Graph Editor:** Every mapping - built-in or your own - is a `NodeGraph`: a DAG of Source/Math/Sink nodes evaluated once per audio block. Open the DSP window's **Graph** tab to inspect, rewire, or build one from scratch on a live patching canvas, with instant audio feedback and no recompiling required.
* **Preset Management:** Create, save, and load node graphs as presets from a preset folder you configure once (via the **Options** button) and that persists across sessions - your own patches live right alongside the built-in mappings.
* **Integrated Synth Engine:** Features a built-in algorithmic synthesis framework (`BoStaffSynth`) designed specifically for performance interaction.
* **Visual Diagnostic Windows:** Includes dedicated GUI interfaces for viewing live raw IMU data streams, debugging console logs, and visually monitoring spatial movements.
* **Scalable Branding:** The editor header renders the REMORA logo from an embedded SVG (`Assets/logo.svg`), tinted to match the UI palette, so it stays crisp at any window size instead of scaling a bitmap.

---

## Building

### Prerequisites

- **CMake 3.22+**
- **Ninja** build system
- A C++17-capable compiler (MSVC 2022 on Windows, Clang/GCC on macOS/Linux)
- JUCE cloned as a subdirectory at `JUCE/`

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

Build artefacts are written to `build/REMORA_artefacts/Standalone/REMORA` - run directly, no DAW needed.

Only the `Standalone` format is built right now (see `FORMATS` in `CMakeLists.txt`). Adding `VST3` (or another JUCE plugin format) to that list and rebuilding is enough to get a loadable plugin alongside it - no other code changes required.

### Runtime Dependencies

The app is dynamically linked so you might need to install some system librairies:

```bash
# Debian/Ubuntu
sudo apt install libfontconfig1 libfreetype6 libasound2

# Arch
sudo pacman -S fontconfig freetype2 alsa-lib
```

`fontconfig`, `freetype2`, and `alsa-lib` (ALSA) are the ones most likely to be missing on a minimal install; the rest (libstdc++, libpng, zlib, ...) ship on virtually every desktop system.

---

## Running

1. Ensure the ESP32 is streaming to your machine (or use `../tests/testOSCReceiver.py` to verify packets are arriving on port 8000). See [firmware/README.md](../firmware/README.md) for flashing the board.
2. Launch the standalone app.
3. Click **CONNECT** to start the OSC receiver on port 8000.
4. Click **CALIBRATE** and follow the three-pose procedure below to align the sensor's local frame with musical space.
5. Select a mapping from the dropdown and move the staff. Use **RAW DATA**, **DEBUG**, and **DSP** to open the diagnostic windows if needed.

### Calibration

The plugin uses a three-pose calibration to build a correction quaternion that maps the sensor's arbitrary mounting orientation to a consistent musical frame:

1. **Pose A** - staff horizontal, pointing forward
2. **Pose B** - staff vertical, pointing up
3. **Pose C** - staff horizontal, pointing right

After recording all three poses the plugin computes an orthonormal rotation matrix via polar decomposition and converts it to a quaternion applied to every subsequent reading. This means the instrument behaves identically regardless of how the sensor is physically oriented or mounted on the staff.

---

## Node Graph Editor

At the heart of REMORA's mapping engine is a **visual, node-based patching system**. Instead of hardcoded mapping classes, every performance mapping - from the simplest pitch/roll patch to the full Azimut engine - is a `Graph::NodeGraph`: a directed acyclic graph of small typed nodes, wired together and re-evaluated once per audio block.

* **Node Categories:**
  * **Source nodes** read live sensor/motion data (raw IMU channels, or derived motion features like gyroscopic magnitude, Laban weight/time/space/flow, facing angle, spin count).
  * **Math nodes** transform values in between - scaling, quantizing to a scale, gating, smoothing, crossfading, and more.
  * **Sink nodes** write the final result into a synth parameter (pitch, gain, filter cutoff, drive, reverb send, etc.), and automatically get a live-value monitor knob in the DSP window.
* **Live Patching Canvas:** Right-click empty canvas to add a node, drag from an output pin to an input pin to connect, right-click a node to delete it, right-click a wire to disconnect it. Pan by dragging the background, zoom with the mouse wheel. Hovering a pin shows its current live output value.
* **Auto Layout:** One click tidies the whole graph into ranked columns (flow direction) and lanes (parallel branches), with adjustable **Rank Sep** / **Node Sep** spacing sliders.
* **Non-Destructive Editing:** Edits are tracked per-graph; a **Reset Changes** button reverts the currently open graph back to how it looked when it was last loaded or saved, so experimenting with a built-in preset is always safe to undo.
* **Preset Workflow:** The selector bar above the canvas manages presets directly:
  * **New** starts a blank graph under a name you choose.
  * **Save** writes the current graph back to its file;
  * **Load** lists every `.xml` preset found in your preset folder and opens the one you pick.
  * **Options** lets you point the preset folder at any location on disk - the choice is remembered across sessions.

Because every mapping in this project - including all of the ones documented below - is implemented as a graph, none of them are fixed in stone: open any preset in the Graph tab to see exactly how it works, tweak it, or use it as the starting point for your own.

---

## Performance Mapping Strategies

This technical section details how physical movements stream through each mapping's node graph and directly manipulate the parameters of the synthesizer engine. Nine presets are currently registered in `SynthManager`, in the order below - every one of them a `NodeGraph` you can open, study, and remix in the [Node Graph Editor](#node-graph-editor).

| Mapping | Core idea |
|---|---|
| **Simple (Pitch+Roll)** | Single sine wave. Pitch angle sets the root frequency, roll amount sets the volume - the most direct staff-to-pitch mapping. |
| **Wind Noise** | No pitch, no chord - just filtered noise. Gyro+accel energy swells and dies down like gusts of wind, opening the noise filter and master lowpass brighter the faster you move. |
| **Bowed Chord** | Pitch angle quantizes to a chromatic root; yaw and roll select the chord voicing. Gyro speed acts like bow pressure, driving volume/drive/brightness, and sharp jabs add a noise strike. |
| **Lead + Drone** | Tilt drives a major-scale melody. A 4-voice drone chord stays harmonically locked below it; yaw crossfades two drone voices. |
| **Martial Momentum** | Root note is one of four fixed pitches chosen by spin plane/direction, gliding at a rate set by spin momentum. Volume is gated by movement plus Laban "Weight" and thrust jabs. |
| **Azimut Kinetic** | Root note from spin plane, direction, and facing (north/south). A single rotation-speed value fans out to filter cutoff, brightness, drive, noise, vibrato/tremolo and reverb all at once. |
| **Speed Gate** | Speed-gated crossfade: a simple pentatonic melody below a speed threshold, the full Azimut Kinetic mapping above it, with no clicks at the transition. |
| **Spin Voices (Scale)** | Roll angle selects which of 4 independent voices is "live"; its pitch snaps to the nearest note of a major scale and glides, while the other voices hold their last pitch/gain. |
| **Vocal Tract** | A physically-modeled voice (glottal source + digital-waveguide vocal tract) instead of the additive engine. Pitch sets glottal pitch, roll/yaw shape the vowel, motion and Laban "Weight" drive volume/tenseness, thrust jabs pinch the tract for consonant-like bursts. |

> Older presets (`Spin Filter`, `Martial Effort`, `Azimut`, `Azimut+`, `Azimut Reverb`, plain `Spin Voices`) still exist as `Graph::Presets::build*()` functions but are commented out of `SynthManager`'s registration list, superseded by the entries above - see [`Source/DSP/SynthManager.cpp`](Source/DSP/SynthManager.cpp).

---

### 1. Simple Mapping

* **Movement Inputs:** Absolute pitch tilt, absolute roll.
* **DSP Transformation:**
  * **Pitch Tilt:** Linearly maps pitch angle onto a single voice's fundamental frequency (100 Hz - 1000 Hz).
  * **Roll Gate:** Absolute roll angle drives master volume (0.05 - 0.20), acting as a simple tilt-based dynamic gate. A single voice plays with no partials beyond the fundamental.

---

### 2. Bowed Chord Mapping

* **Movement Inputs:** Total Gyroscopic Velocity ($|Gyro|$), Absolute Yaw, Pitch, and Roll ($|\text{Roll}|$).
* **DSP Transformation:**
  * **Excitation (The Bow):** Synthesizer excitement and voice gains are scaled by gyroscopic magnitude above a floor of $12.0^\circ/\text{s}$. If the device stops, the sound dampens instantly.
  * **Chord Harmonization:** Yaw angle selects 1 of 4 chords. Tilting the device past an intense roll boundary ($>0.7\text{ rad}$) overrides the scale into secondary chord sets.
  * **Acoustic Friction & Strikes:** Linear acceleration changes ($\Delta Accel$) generate an impulse envelope ($0.9990$ decay half-life) triggering a physical modeling-style noise stroke. Absolute roll maps to vibrato depth and high Z-axis angular velocity adds dynamic tremolo.

---

### 3. Lead + Drone Mapping

* **Movement Inputs:** Absolute Pitch (lead melody), Absolute Yaw (drone balance), Linear Acceleration Magnitude, Gyroscopic Magnitude.
* **DSP Transformation:**
  * **Lead Voice:** Pitch is quantized to a Major scale spanning several octaves around A3 ($220\text{ Hz}$).
  * **Locked Drone Voices:** Three drone voices sit at fixed intervals below the lead (an octave, two octaves, and a fifth) but are transposed by the *same* amount as the lead root, so they stay harmonically locked to whichever scale degree is currently playing.
  * **Timbre & Swell:** Acceleration magnitude drives lead voice gain, drone "swell" (amplitude growth), drive amount, noise injection, and upper-partial brightness. Yaw crossfades gain and hard-pans the two side drone voices against each other.

---

### 4. Spin Filter Mapping

* **Movement Inputs:** Gyroscopic Magnitude, Pitch, Yaw, Roll, Linear Acceleration Magnitude.
* **DSP Transformation:**
  * **Quantized Steps:** Gyroscopic magnitude steps through a 5-note Pentatonic Scale (2 voices: root + perfect fifth), rooted at C3 ($130.81\text{ Hz}$).
  * **Roll as Brightness Ceiling:** Absolute roll acts as a movable low-pass "ceiling" over the 6 harmonic partials - as roll increases, partials progressively unmute with a one-partial-wide crossfade at the boundary, rather than a hard cutoff.
  * **Motion Energy:** Combined gyroscopic and acceleration energy drives master gain and noise amount; pitch drives vibrato depth and yaw drives stereo panning.

---

### 5. Martial Effort Mapping

* **Movement Inputs:** Laban Movement Analysis features (Weight, Time, Space, Flow), calculated from dynamic linear acceleration ($\text{Accel} - \text{Gravity}$) and angular differences.
* **DSP Transformation:**
  * **Quantized Note Steps:** Smoothed gyroscopic magnitude quantizes directly into a 5-note Pentatonic Minor Scale. Faster movement transitions step-wise higher up the scale.
  * **Harmonic Polyphony:** Laban Weight (integrated velocity from non-gravitational acceleration) dynamically calculates voice presence. Heavy, high-momentum gestures open up 4 concurrent polyphonic voices playing Major, Minor, 7th, or Diminished structures based on rotation plane axis (Vertical vs. Horizontal) and direction (CW vs. CCW).

---

### 6. Martial Momentum Mapping

Martial Momentum shifts the paradigm from scale-quantization to trajectory-driven momentum tracking and gesture triggers. It shares its Laban engine (weight/time/space/flow) with Martial Effort, but replaces scale-stepping with a spin-plane root note, a spin-count-driven filter sweep, and axial thrust detection. Its rotation-direction classifier resolves CW/CCW *relative to the direction first captured when the gesture began* rather than a true world-frame compass - that absolute-compass feature is what the **Azimut Mapping** below completes.

* **Root Note by Spin Plane + Direction:** The rotation axis is classified as Vertical or Horizontal, and CW/CCW relative to the first spin direction observed. This selects a fixed pitch class (C / E for Vertical CW / CCW, G / A for Horizontal CW / CCW) instead of scale steps, played as bare octaves and fifths rather than a full chord.
* **Continuous Rotational Velocity (Filter Sweep):** The mapping accumulates rotational degrees over time; every full $360^\circ$ loop increments `continuous_spin_count_`, resetting whenever the spin plane or direction changes. The spin count drives a phase modulator ($\text{Phase} = \text{Count} \times 1.5$), whose sine output sweeps a global Low-Pass Filter between $400\text{ Hz}$ and $20{,}000\text{ Hz}$.
* **Axial Thrust Detection:** Real-time zero-crossing jerk detection isolates sudden linear acceleration along the staff's longitudinal axis ($>1.5\text{ g}$) while gyroscopic magnitude stays low (below $90^\circ/\text{s}$, to exclude spins). Each validated thrust punches through the gain gate, boosts upper partials (3rd-5th) and drive amount, and injects a short burst of high-frequency noise.

---

## Deep Dive: Azimut Mapping

**Azimut** is the mapping that finishes what Martial Momentum started: instead of a relative CW/CCW reference, it computes a true world-frame **facing direction** and combines it with spin plane + spin direction for an 8-way root note table. It keeps V2's Laban engine, spin-count filter sweep, and axial thrust detection, and adds the facing axis on top. It ships in two flavors - `AzimutMapping` (`"Azimut"`) and a variant, `AzimutPlusMapping` (`"Azimut+"`) - described at the end of this section.

#### A. The Directional Angular Compass (Facing + Root Note Selection)

* **Reference Frame:** "Facing" is measured relative to the calibration gesture's own forward pose (virtual world $+X$ = North, $+Y$ = East), not magnetic north - the magnetometer is unreliable this close to the staff's own hardware and stage equipment.
* **Vertical-Plane Spins:** When the rotation axis is Vertical, the smoothed rotation axis itself is used as the facing vector. The horizontal domain is split into North/East with an $\approx1.15$ hysteresis ratio between the two axes ($\approx$ 48°/42° switch points) to stop the root note flickering near the boundary.
* **Horizontal-Plane Spins:** Facing instead tracks the staff's own long axis (smoothed tip direction) in world space — but as the table below shows, the root note only ends up depending on spin direction here, not facing.

| Spin Plane | Spin Direction | Facing | Root Note | Semitones |
|---|---|---|---|---|
| Vertical   | CW  | North | **C** | 0    |
| Vertical   | CW  | East  | **G** | +7   |
| Vertical   | CCW | North | **E** | +4   |
| Vertical   | CCW | East  | **B** | +11  |
| Horizontal | CW  | - | **G** | +7   |
| Horizontal | CCW | -  | **A** | +9   |
* **Morphing:** The target root note morphs to the current one via a one-pole filter whose speed scales with staff speed, with an extra boost right after the staff leaves rest so the pitch snaps to the new root quickly instead of crawling into it.
* **Voicing:** Rather than a chord, only octaves and a fifth are layered on the root (`[+12, +7, -12]` semitones) to preserve the pitch class.

#### B. Continuous Rotational Velocity (The Filter Modulation Sweep)

* **Movement:** Accumulated rotational degrees are tracked over time (independent of facing). Every full $360^\circ$ loop increments a persistent `continuous_spin_count_`; the counter (and accumulated degrees) reset instantly whenever the spin plane or spin direction changes.
* **DSP Result:** The spin count feeds an active phase modulator: $\text{Phase} = \text{Count} \times 1.5$. The resulting sine wave drives a global **Low-Pass Filter sweep** between **$400\text{ Hz}$** and **$20{,}000\text{ Hz}$**. Continuous physical spinning in one direction produces a cyclic timbral wave effect.

#### C. Axial Thrust Detection (Transient Punches)

* **Movement:** Dynamic linear acceleration is rotated into world space and projected onto the staff's own long axis; a signed zero-crossing jerk detector flags a peak once the axial acceleration exceeds $1.5\text{ g}$, gated to gyroscopic magnitude below $90^\circ/\text{s}$ so spins aren't mistaken for stabs, and rate-limited by a 200 ms cooldown.
* **DSP Result:** Each validated thrust fires a fast-decaying impulse ($\approx150\text{ ms}$ half-life) that simultaneously:
  * **Gain Envelope Boost:** Punches through the master gain gate.
  * **Harmonic Waveshaping & Distortion:** Boosts `driveAmt` and injects energy into upper partials 3-5, pushing the tone from a clean fundamental into a sharp, growling distortion.
  * **High-Frequency White Noise Injection:** Releases a brief, unpitched high-frequency noise burst (`noiseAmount`), giving the strike a percussive edge.

#### Azimut+ Variant

`AzimutPlusMapping` reuses the exact same body-motion engine (Laban weight/time/space/flow, facing, thrust detection) as Azimut, with one change: the Low-Pass Filter cutoff tracks the staff's **instantaneous rotation speed** directly (linearly mapped from the $30^\circ/\text{s}$ floor to the $750^\circ/\text{s}$ ceiling, 400 Hz-20 kHz) instead of the accumulated spin count from section B. This trades the cyclic sweep-per-loop character of Azimut for a filter that opens and closes continuously with how fast the performer is currently moving.

#### Azimut Reverb Variant

`AzimutReverbMapping` reuses the exact same body-motion engine as Azimut (facing, spin-count filter sweep, thrust detection, section A-C above are unchanged), with one change: Laban Flow - computed every block but discarded via `ignoreUnused` in Azimut and Azimut+ - now drives a reverb send.

* **Flow → Reverb Send:** `flow_free` (loose, unrestrained motion; the complement of `flow_bound`, see the Laban Flow definition in section B) sets the wet level, scaled by Laban Weight so a loose but tiny gesture doesn't flood the sound: $\text{wetLevel} = \text{flow\_free} \times (0.25 + \text{weight} \times 0.75)$, clamped to $[0, 1]$.
* **Flow → Room Size / Damping:** the same `flow_free` also maps linearly to the reverb's room size ($0.25 \rightarrow 0.95$, i.e. decay length / "feedback") and inversely to its damping ($0.80 \rightarrow 0.15$, i.e. high-frequency absorption). Free, loose motion therefore opens a longer, brighter tail; bound, tense motion collapses it back toward a short, damped, near-dry space.
* **DSP Implementation:** `BoStaffSynth` owns a single shared `juce::dsp::Reverb` instance, gated by `MappingOutput::reverbWetLevel/reverbRoomSize/reverbDamping`. The wet level is smoothed over 50 ms and the wet/dry mix is done manually per sample (rather than via the Reverb's own internal wet/dry parameters) so switching mappings, or Flow itself swinging quickly, never zippers. `BoStaffSynth` resets `reverbWetLevel` to 0 before calling into whichever mapping is active, so only Azimut Reverb ever turns the send on - every other mapping stays untouched and fully dry.

---

### Speed Gate Mapping

A speed-gated crossfade between a simple melody and the full Azimut engine, so slow, deliberate gestures and fast, dynamic ones each get a purpose-built sound.

* **Below the Gate ($<120^\circ/\text{s}$):** A simple melody plays - pitch is quantized to a Major Pentatonic scale across 2 octaves, roll drives master gain, and yaw pans the voice across the stereo field.
* **Above the Gate ($>180^\circ/\text{s}$):** The full Azimut Mapping (facing + spin plane/direction root selection, filter sweep, thrust detection) takes over completely.
* **The Crossfade:** Between $120^\circ/\text{s}$ and $180^\circ/\text{s}$ (a $60^\circ/\text{s}$ band centered on a $150^\circ/\text{s}$ gate), every output parameter - pitch, gains, timbre, panning, filter cutoff - is linearly interpolated between the two mappings' outputs, so crossing the threshold never clicks.

---

### Spin Voices Mapping

Builds a sustained chord one note at a time by letting spin state "freeze" voices in place.

* **Movement:** Spin plane (Vertical/Horizontal) and spin direction (CW/CCW) select 1 of 4 controllable voices, each permanently assigned to one combo (CW+Vertical, CCW+Vertical, CW+Horizontal, CCW+Horizontal). Facing is deliberately not part of this mapping.
* **DSP Result:** Only the currently active voice updates each frame - its pitch bends upward from its base chord tone (a C major arpeggio across two octaves: `[0, 7, 12, 19]` semitones) by up to +12 semitones with staff speed, and its gain follows Laban Weight. The other three voices hold their last pitch and gain exactly where they were left. Master gain is fixed at 1.0, so the chord stays sustained even at rest - moving through all 4 spin combos in sequence builds up the full chord note by note.

---

## Project Context

This software component is a core part of the larger **timotheedvt/2026_nime_td** ecosystem. It bridges the gap between hardware physical controllers and modern digital audio workstations.