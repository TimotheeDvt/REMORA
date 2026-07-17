# REMORA Plugin
### Real-time Expressive Motion to Output Routing Audio

A real-time, dynamic **JUCE-based audio plugin** (VST3 and Standalone) engineered to receive incoming multi-sensor IMU data via **Open Sound Control (OSC)**. The software serves as a modular audio mapping engine, converting spatial movements and hardware interactions into complex sound synthesis.

## Core Features

* **Multi-Format Support:** Compiles as both a VST3 plugin and a standalone desktop application.
* **Modular Mapping Architecture:** Driven by a base `IMappingStrategy` class, making it easy to hot-swap or add new sensor-to-DSP strategies.
* **Integrated Synth Engine:** Features a built-in algorithmic synthesis framework (`BoStaffSynth`) designed specifically for performance interaction.
* **Visual Diagnostic Windows:** Includes dedicated GUI interfaces for viewing live raw IMU data streams, debugging console logs, and visually monitoring spatial movements.

---

## Performance Mapping Strategies

This technical section details how physical movements stream into the base `IMappingStrategy` classes and directly manipulate the parameters of the synthesizer engine. Ten strategies are registered in `BoStaffSynth`, in the order below, from the simplest direct mappings up to the full Azimut engine and its variants.

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

### 5. Bozendo Mapping (V1)

* **Movement Inputs:** Laban Movement Analysis features (Weight, Time, Space, Flow), calculated from dynamic linear acceleration ($\text{Accel} - \text{Gravity}$) and angular differences.
* **DSP Transformation:**
  * **Quantized Note Steps:** Smoothed gyroscopic magnitude quantizes directly into a 5-note Pentatonic Minor Scale. Faster movement transitions step-wise higher up the scale.
  * **Harmonic Polyphony:** Laban Weight (integrated velocity from non-gravitational acceleration) dynamically calculates voice presence. Heavy, high-momentum gestures open up 4 concurrent polyphonic voices playing Major, Minor, 7th, or Diminished structures based on rotation plane axis (Vertical vs. Horizontal) and direction (CW vs. CCW).

---

### 6. Bozendo Mapping (V2)

The V2 Bozendo mapping shifts the paradigm from scale-quantization to trajectory-driven momentum tracking and gesture triggers. It shares its Laban engine (weight/time/space/flow) with V1, but replaces scale-stepping with a spin-plane root note, a spin-count-driven filter sweep, and axial thrust detection. Its rotation-direction classifier resolves CW/CCW *relative to the direction first captured when the gesture began* rather than a true world-frame compass - that absolute-compass feature is what the **Azimut Mapping** below completes.

* **Root Note by Spin Plane + Direction:** The rotation axis is classified as Vertical or Horizontal, and CW/CCW relative to the first spin direction observed. This selects a fixed pitch class (C / E for Vertical CW / CCW, G / A for Horizontal CW / CCW) instead of scale steps, played as bare octaves and fifths rather than a full chord.
* **Continuous Rotational Velocity (Filter Sweep):** The mapping accumulates rotational degrees over time; every full $360^\circ$ loop increments `continuous_spin_count_`, resetting whenever the spin plane or direction changes. The spin count drives a phase modulator ($\text{Phase} = \text{Count} \times 1.5$), whose sine output sweeps a global Low-Pass Filter between $400\text{ Hz}$ and $20{,}000\text{ Hz}$.
* **Axial Thrust Detection:** Real-time zero-crossing jerk detection isolates sudden linear acceleration along the staff's longitudinal axis ($>1.5\text{ g}$) while gyroscopic magnitude stays low (below $90^\circ/\text{s}$, to exclude spins). Each validated thrust punches through the gain gate, boosts upper partials (3rd-5th) and drive amount, and injects a short burst of high-frequency noise.

---

## Deep Dive: Azimut Mapping

**Azimut** is the mapping that finishes what Bozendo V2 started: instead of a relative CW/CCW reference, it computes a true world-frame **facing direction** and combines it with spin plane + spin direction for an 8-way root note table. It keeps V2's Laban engine, spin-count filter sweep, and axial thrust detection, and adds the facing axis on top. It ships in two flavors - `AzimutMapping` (`"Azimut"`) and a variant, `AzimutPlusMapping` (`"Azimut+"`) - described at the end of this section.

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

### Ben's Mapping

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