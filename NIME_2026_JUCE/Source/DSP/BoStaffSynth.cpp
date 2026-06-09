#include "BoStaffSynth.h"
#include <cmath>
#include <algorithm>

// Chord vocabulary
// Each chord: 4 voice intervals in semitones above root
// Voice 0 = root (always 0), voices 1-3 = intervals
const ChordType BoStaffSynth::kChords[kNumChords] = {
    { "minor",       { 0.f,  3.f,  7.f, 12.f }, 4 },   // minor triad + octave
    { "power",       { 0.f,  7.f, 12.f, 19.f }, 4 },   // power chord (open, strong)
    { "major",       { 0.f,  4.f,  7.f, 12.f }, 4 },   // major triad + octave
    { "sus4",        { 0.f,  5.f,  7.f, 12.f }, 4 },   // suspended 4th
    { "minor7",      { 0.f,  3.f,  7.f, 10.f }, 4 },   // minor 7th (jazz-dark)
    { "major7",      { 0.f,  4.f,  7.f, 11.f }, 4 },   // major 7th (lush)
};

// Helpers
float BoStaffSynth::semitoneRatio(float semitones) {
    return std::pow(2.0f, semitones / 12.0f);
}

float BoStaffSynth::pitchToRootHz(float pitchRad) {
    constexpr float pi = 3.14159265f;
    // Map pitch -PI/2..+PI/2 → MIDI notes 36..60 (C2..C4, ~65..261 Hz)
    // Then snap to nearest chromatic semitone for musicality
    float norm = juce::jlimit(0.0f, 1.0f, (pitchRad + pi * 0.5f) / pi);
    float midiFloat = 36.0f + norm * 24.0f;
    // Quantize to nearest semitone
    int midiNote = static_cast<int>(std::round(midiFloat));
    // MIDI to Hz: A4=69=440Hz
    return 440.0f * std::pow(2.0f, (midiNote - 69) / 12.0f);
}

int BoStaffSynth::yawToChordIdx(float yawRad, float rollAbs) {
    constexpr float pi = 3.14159265f;
    // yaw maps across 6 chord types
    // Roll above 0.7 unlocks the 7th chords (last 2)
    float yawNorm = (yawRad + pi) / (2.0f * pi);  // 0..1
    yawNorm = juce::jlimit(0.0f, 1.0f, yawNorm);

    if (rollAbs > 0.7f) {
        // High roll: 7th chords in the same yaw positions
        return yawNorm < 0.5f ? 4 : 5;   // minor7 or major7
    }
    // Normal: minor, power, major, sus4 across yaw range
    if (yawNorm < 0.25f) return 0;   // minor
    if (yawNorm < 0.50f) return 1;   // power
    if (yawNorm < 0.75f) return 2;   // major
    return 3;                          // sus4
}

void BoStaffSynth::buildPartialTargets(int chordVoice, float bowP, float rollAbs,
                                       float accelMag, float noiseEnv,
                                       float targets[SynthVoice::kPartials])
{
    // Partial 0: fundamental — always present, bowed
    targets[0] = bowP;

    // Partial 1: octave — grows with roll (twist adds brightness)
    targets[1] = bowP * (0.3f + rollAbs * 0.4f);

    // Partial 2: 5th above fundamental — grows with bow pressure
    targets[2] = bowP * 0.35f;

    // Partial 3: major 3rd harmonic — from accel (physical energy)
    targets[3] = bowP * juce::jlimit(0.0f, 0.5f, accelMag * 0.15f);

    // Partial 4+5: upper harmonics — only during strike (noiseEnv > 0)
    float strikeHarmonics = juce::jlimit(0.0f, 0.6f, noiseEnv * 1.2f);
    targets[4] = strikeHarmonics * 0.4f;
    targets[5] = strikeHarmonics * 0.25f;

    // Voice 0 (root) is loudest; inner voices slightly quieter
    float voiceWeight = (chordVoice == 0 || chordVoice == 3) ? 1.0f : 0.75f;
    for (int p = 0; p < SynthVoice::kPartials; ++p)
        targets[p] *= voiceWeight;
}

// Lifecycle

BoStaffSynth::BoStaffSynth() {}
BoStaffSynth::~BoStaffSynth() {}

void BoStaffSynth::prepareToPlay(double sampleRate, int) {
    currentSampleRate = static_cast<float>(sampleRate);
    sampleRateRecip   = 1.0f / currentSampleRate;

    masterGain.reset(sampleRate, 0.010);
    masterGain.setCurrentAndTargetValue(0.0f);

    bowPressure.reset(sampleRate, 0.040);
    bowPressure.setCurrentAndTargetValue(0.0f);

    rootFreq.reset(sampleRate, 0.150);  // 150ms pitch glide between notes
    rootFreq.setCurrentAndTargetValue(110.0f);

    chordBlendSmoother.reset(sampleRate, 0.200);  // 200ms chord crossfade
    chordBlendSmoother.setCurrentAndTargetValue(1.0f);

    for (int v = 0; v < kNumVoices; ++v) {
        voices[v].prepare(sampleRate);
        chorusPhase[v] = static_cast<float>(v) * 1.57f;  // 90deg apart
    }

    noiseEnvelope    = 0.0f;
    prevAccelMag     = 0.0f;
    noiseFilterState = 0.0f;
}

void BoStaffSynth::setSoundEnabled(bool b) { soundEnabled.store(b); }
bool BoStaffSynth::isSoundEnabled() const { return soundEnabled.load(); }

// Main process
void BoStaffSynth::processBlock(juce::AudioBuffer<float> &buffer,
                                const StaffSoundParams& params)
{
    buffer.clear();
    const int numSamples  = buffer.getNumSamples();
    if (buffer.getNumChannels() == 0) return;

    auto* left  = buffer.getWritePointer(0);
    auto* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;

    constexpr float twoPi = 6.28318530717958f;
    constexpr float pi    = 3.14159265358979f;

    // Muted path
    if (!params.isReceivingValidData || !isSoundEnabled()) {
        masterGain.setTargetValue(0.0f);
        bowPressure.setTargetValue(0.0f);
        for (int v = 0; v < kNumVoices; ++v)
            voices[v].voiceAmp.setTargetValue(0.0f);
        // Drain smoothers silently
        for (int i = 0; i < numSamples; ++i) {
            masterGain.getNextValue();
            bowPressure.getNextValue();
            rootFreq.getNextValue();
            for (int v = 0; v < kNumVoices; ++v) {
                voices[v].voiceAmp.getNextValue();
                for (int p = 0; p < SynthVoice::kPartials; ++p)
                    voices[v].partialAmp[p].getNextValue();
            }
            left[i] = 0.f;
        }
        if (right) juce::FloatVectorOperations::copy(right, left, numSamples);
        return;
    }

    // Per-block parameter computation

    // Root frequency — quantized to chromatic scale
    float targetRoot = pitchToRootHz(params.pitch);
    rootFreq.setTargetValue(targetRoot);

    // Bow pressure from gyroscope magnitude
    float gyroMag = std::sqrt(params.gx*params.gx + params.gy*params.gy + params.gz*params.gz);
    constexpr float kBowThresh = 12.0f;   // deg/s
    constexpr float kBowSat    = 150.0f;
    float bow = (gyroMag < kBowThresh) ? 0.0f
                : juce::jlimit(0.0f, 1.0f, (gyroMag - kBowThresh) / (kBowSat - kBowThresh));
    bowPressure.setTargetValue(bow);
    masterGain.setTargetValue(0.05f + bow * 0.20f);  // 5–25% master gain

    // Accel magnitude for spectral brightness
    float accelMag = std::sqrt(params.ax*params.ax + params.ay*params.ay + params.az*params.az);

    // Roll controls: vibrato, timbre unlock, chord vocab
    float rollAbs = juce::jlimit(0.0f, 1.0f, std::abs(params.roll) / pi);

    // Vibrato: roll adds pitch wobble, gz adds speed variation
    float vibratoDepth = rollAbs * 0.022f;   // ±2.2% max = ~half semitone
    float vibratoRate  = 4.5f + std::abs(params.gz) * 0.03f;

    // Tremolo: yaw spin modulates amplitude flutter
    float tremoloDepth = juce::jlimit(0.0f, 0.35f, std::abs(params.gz) / 90.0f);
    float tremoloRate  = 3.0f + std::abs(params.gz) * 0.05f;

    // Chorus: slight per-voice detuning, very slow drift (~0.3 Hz)
    constexpr float kChorusDepth = 0.003f;  // ±0.3% detune max
    constexpr float kChorusRate  = 0.3f;

    // Strike detection from accelerometer spike
    float accelDelta = accelMag - prevAccelMag;
    prevAccelMag = accelMag;
    constexpr float kStrikeThresh = 3.0f;   // g delta
    if (accelDelta > kStrikeThresh)
        noiseEnvelope = juce::jlimit(0.0f, 1.0f,
                        noiseEnvelope + (accelDelta - kStrikeThresh) * 0.5f);

    // Noise brightness: accel Z → LP filter coefficient (high az = brighter hit)
    float noiseCoef = 1.0f - juce::jlimit(0.05f, 0.90f, std::abs(params.az) * 0.18f);

    // Soft-clip drive from bow pressure
    float driveAmt = bow * 1.8f;  // 0 → no drive, full bow → heavy saturation

    // Chord selection & crossfade
    int newChordIdx = yawToChordIdx(params.yaw, rollAbs);
    if (newChordIdx != targetChordIdx) {
        currentChordIdx = targetChordIdx;
        targetChordIdx  = newChordIdx;
        chordBlend      = 0.0f;
        chordBlendSmoother.setCurrentAndTargetValue(0.0f);
        chordBlendSmoother.setTargetValue(1.0f);
    }
    // chordBlend advances in the sample loop via the smoother

    // Pre-compute partial targets for current and target chord
    // (computed once per block, applied per sample via smoothers)
    const ChordType& chordA = kChords[currentChordIdx];
    const ChordType& chordB = kChords[targetChordIdx];

    // Sample loop
    float partialTargets[SynthVoice::kPartials];

    for (int i = 0; i < numSamples; ++i)
    {
        // Advance LFOs
        vibratoPhase += twoPi * vibratoRate * sampleRateRecip;
        if (vibratoPhase >= twoPi) vibratoPhase -= twoPi;

        tremoloPhase += twoPi * tremoloRate * sampleRateRecip;
        if (tremoloPhase >= twoPi) tremoloPhase -= twoPi;

        // Chord blend
        float blend = chordBlendSmoother.getNextValue();

        // Vibrato
        float vibratoMod = 1.0f + vibratoDepth * std::sin(vibratoPhase);

        // Tremolo
        float tremoloMod = 1.0f - tremoloDepth * (0.5f + 0.5f * std::sin(tremoloPhase));

        // Noise envelope decay (~25ms at 44100Hz)
        noiseEnvelope *= 0.9990f;
        float noiseFilterIn = [&]() -> float {
            rngState ^= rngState << 13;
            rngState ^= rngState >> 17;
            rngState ^= rngState << 5;
            return static_cast<float>(rngState) * (2.0f / 4294967296.0f) - 1.0f;
        }();
        noiseFilterState = noiseFilterState * noiseCoef + noiseFilterIn * (1.0f - noiseCoef);
        float noiseOut = noiseFilterState * noiseEnvelope;

        // Master values this sample
        const float bowNow    = bowPressure.getNextValue();
        const float masterNow = masterGain.getNextValue() * tremoloMod;
        const float rootNow   = rootFreq.getNextValue() * vibratoMod;

        // Accumulate all voices
        float mixL = 0.f, mixR = 0.f;

        for (int v = 0; v < kNumVoices; ++v)
        {
            // Chorus: per-voice detuning via LFO
            chorusPhase[v] += twoPi * kChorusRate * sampleRateRecip;
            if (chorusPhase[v] >= twoPi) chorusPhase[v] -= twoPi;
            float chorusMod = 1.0f + kChorusDepth * std::sin(chorusPhase[v]);

            // Voice frequency: blend between chord A and chord B intervals
            float semiA = (v < chordA.numVoices) ? chordA.semitones[v] : chordA.semitones[chordA.numVoices - 1];
            float semiB = (v < chordB.numVoices) ? chordB.semitones[v] : chordB.semitones[chordB.numVoices - 1];
            float semi  = semiA + (semiB - semiA) * blend;
            float voiceFreq = rootNow * semitoneRatio(semi) * chorusMod;

            // Per-voice partial targets (based on blended chord voice index)
            buildPartialTargets(v, bowNow, rollAbs, accelMag,
                                noiseEnvelope, partialTargets);

            // Voice amplitude — voice 0 and 3 (root+octave) slightly louder
            float voiceGainTarget = (v == 0 || v == 3) ? bowNow * 0.90f : bowNow * 0.70f;
            voices[v].voiceAmp.setTargetValue(voiceGainTarget);
            float voiceGain = voices[v].voiceAmp.getNextValue();

            // Tick the voice oscillator
            float voiceSample = voices[v].tick(voiceFreq, sampleRateRecip,
                                               partialTargets, noiseOut * 0.3f,
                                               rngState, driveAmt);
            voiceSample *= voiceGain;

            // Stereo spread: voices panned L-Center-Center-R
            constexpr float kPanL[kNumVoices] = { 0.85f, 0.55f, 0.45f, 0.15f };
            constexpr float kPanR[kNumVoices] = { 0.15f, 0.45f, 0.55f, 0.85f };
            mixL += voiceSample * kPanL[v];
            mixR += voiceSample * kPanR[v];
        }

        left[i]  = mixL * masterNow;
        if (right) right[i] = mixR * masterNow;
    }
}