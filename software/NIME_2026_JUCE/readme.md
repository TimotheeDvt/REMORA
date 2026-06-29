# NIME OSC Receiver Plugin

A real-time, dynamic **JUCE-based audio plugin** (VST3 and Standalone) engineered to receive incoming multi-sensor IMU data via **Open Sound Control (OSC)**. The software serves as a modular audio mapping engine, converting spatial movements and hardware interactions into complex sound synthesis.

## Core Features

* **Multi-Format Support:** Compiles as both a VST3 plugin and a standalone desktop application.
* **Modular Mapping Architecture:** Driven by a base `IMappingStrategy` class, making it easy to hot-swap or add new sensor-to-DSP strategies.
* **Integrated Synth Engine:** Features a built-in algorithmic synthesis framework (`BoStaffSynth`) designed specifically for performance interaction.
* **Visual Diagnostic Windows:** Includes dedicated GUI interfaces for viewing live raw IMU data streams, debugging console logs, and visually monitoring spatial movements.

---

## Performance Mapping Strategies

This technical section details how physical movements stream into the base `IMappingStrategy` classes and directly manipulate the parameters of the synthesizer engine.

---

### 1. Simple Mapping

* **Movement Inputs:** Absolute pitch tilt, yaw angles, and smooth gyroscope magnitude.
* **DSP Transformation:**
  * **Pitch Tilt & Orientation:** Directly maps lineally or exponentially onto individual voice levels or fundamental pitches.
  * **Sustained Energy:** High steady rotation increases master volume output, acting as a simple dynamic motion gate.

---

### 2. Bowed Chord Mapping

* **Movement Inputs:** Total Gyroscopic Velocity ($|Gyro|$), Absolute Yaw, Pitch, and Roll ($|\text{Roll}|$).
* **DSP Transformation:**
  * **Excitation (The Bow):** Synthesizer excitement and voice gains are scaled by gyroscopic magnitude above a floor of $12.0^\circ/\text{s}$. If the device stops, the sound dampens instantly.
  * **Chord Harmonization:** Yaw angle selects 1 of 4 chords. Tilting the device past an intense roll boundary ($>0.7\text{ rad}$) overrides the scale into secondary chord sets.
  * **Acoustic Friction & Strikes:** Linear acceleration changes ($\Delta Accel$) generate an impulse envelope ($0.9990$ decay half-life) triggering a physical modeling-style noise stroke. Absolute roll maps to vibrato depth and high Z-axis angular velocity adds dynamic tremolo.

---

### 3. Bozendo Mapping (V1)

* **Movement Inputs:** Laban Movement Analysis features (Weight, Time, Space, Flow), calculated from dynamic linear acceleration ($\text{Accel} - \text{Gravity}$) and angular differences.
* **DSP Transformation:**
  * **Quantized Note Steps:** Smoothed gyroscopic magnitude quantizes directly into a 5-note Pentatonic Minor Scale. Faster movement transitions step-wise higher up the scale.
  * **Harmonic Polyphony:** Laban Weight (integrated velocity from non-gravitational acceleration) dynamically calculates voice presence. Heavy, high-momentum gestures open up 4 concurrent polyphonic voices playing Major, Minor, 7th, or Diminished structures based on rotation plane axis (Vertical vs. Horizontal) and direction (CW vs. CCW).

---

### 4. Deep Dive: Bozendo Mapping (V2) - Best one yet

The V2 Bozendo mapping shifts the paradigm from scale-quantization to absolute geometric trajectory analysis, momentum tracking, and interactive gesture triggers.

#### A. The Directional Angular Compass (Pitch Selection) *(azimuth detection in development)*

Instead of scaling note steps by speed, **Bozendo 2 tracks the orientation angle of the active spin plane**.

* **Movement:** The rotation axis vector is mapped into world space coordinates and checked for spatial alignment. If the plane is vertical, it computes the exact **azimuth direction** (the angle the staff faces relative to standard compass headings).
* **DSP Result:** The horizontal compass domain is broken down into 4 distinct quadrants ($90^\circ$ sectors protected by an $11^\circ$ hysteresis buffer to stop note flickering):
* **Sector 0 (East/West):** Core pitch class **C** (0 semitones).
* **Sector 1 (North/South):** Core pitch class **G** (+7 semitones).
* **Sector 2 (West/East):** Core pitch class **E** (+4 semitones).
* **Sector 3 (South/North):** Core pitch class **A** (+9 semitones).


* **Polar Harmonics:** Spinning Clockwise vs. Counter-Clockwise injects an automatic $+3$ semitones modal modification offset.
* The target synthesis engines bypass traditional chord intervals here, generating absolute, powerful octaves and perfect fifth structures (`[+12, +7, -12] semitones`).

#### B. Continuous Rotational Velocity (The Filter Modulation Sweep)

* **Movement:** The mapping strategy tracks accumulated rotational angular degrees over time. Every time a full loop is achieved ($360^\circ$), a persistent variable `continuous_spin_count_` increments. If the performer breaks momentum or changes the orientation axis type, the spin registry instantly resets.
* **DSP Result:** The spin count feeds into an active phase modulator: $\text{Phase} = \text{Count} \times 1.5$. The sine wave output derived from this phase drives a global **Low-Pass Filter sweep**, fluidly sliding back and forth across a sweeping window between **$400\text{ Hz}$ and $20,000\text{ Hz}$**. Continuous physical spinning outputs a cyclic timbral wave effect.

#### C. Axial Thrust Detection (Transient Punches) *(in development)*

* **Movement:** Real-time tracking isolating linear acceleration vectors traveling purely along the staff's length (longitudinal axis) while verifying angular velocity remains low. Sudden forward stabs or strict longitudinal pulls trigger a high-resolution zero-crossing signed jerk detection routine ($>1.5\text{ g}$ threshold).
* **DSP Result:** Once validated, a fast-decaying impulse generator activates, creating immediate changes across three synth layers simultaneously:
  * **Gain Envelope Boost:** Overrides standard attenuation settings to deliver a maximum volume punch through the synthesis output line.
  * **Harmonic Waveshaping & Distortion:** Instantly multiplies internal wave saturation (`driveAmt`) upwards by $+2.0$, while forcefully injecting energy into the high upper partial elements (partials 3, 4, and 5). The tone shifts aggressively from a clean fundamental tone into a sharp, growling distortion.
  * **High-Frequency White Noise Injection:** Releases a brief, localized burst of unpitched high-frequency noise (`noiseAmount`) into the synthesis path, creating a realistic, sharp percussive strike.

---

## Project Context

This software component is a core part of the larger **timotheedvt/2026_nime_td** ecosystem. It bridges the gap between hardware physical controllers and modern digital audio workstations.