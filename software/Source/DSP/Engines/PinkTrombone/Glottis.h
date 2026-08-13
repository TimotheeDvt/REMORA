#pragma once

// Glottal source: Liljencrants-Fant (LF) model pulse generator plus a breath-noise component,
// ported from cutelabnyc/pink-trombone-cpp (MIT), itself a port of Neil Thapen's "Pink Trombone".
// Frequency/tenseness changes are control-rate (set*Target + finishBlock()) and only take full
// effect at the next glottal cycle boundary, interpolated via `lambda` in the meantime - this
// matches the original's avoidance of mid-waveform discontinuities.
namespace PinkTrombone {

class Glottis {
public:
    explicit Glottis(double sampleRate);

    // Advances one sample; `noiseSource` is band-limited breath noise mixed in during the open phase.
    float runStep(float lambda, float noiseSource) noexcept;

    // Call once per block, after every runStep() in the block.
    void finishBlock();

    // Amount of breath noise this instant of the waveform wants mixed in - used to shape the
    // fricative/aspiration noise fed into the vocal tract, not just the glottis' own output.
    float getNoiseModulator() const noexcept;

    void setTargetFrequency(float hz) noexcept { targetFrequency = hz; }
    void setTargetTenseness(float t) noexcept { targetTenseness = t; }

private:
    void setupWaveform(float lambda);
    float normalizedLFWaveform(float t) const;

    double sampleRate;
    float timeInWaveform = 0.0f;
    float frequency = 140.0f, oldFrequency = 140.0f, newFrequency = 140.0f, smoothFrequency = 140.0f, targetFrequency = 140.0f;
    float oldTenseness = 0.6f, newTenseness = 0.6f, targetTenseness = 0.6f;
    float waveformLength = 1.0f / 140.0f;

    // LF model shape parameters, recomputed by setupWaveform() every glottal cycle.
    float Rd = 1.0f, alpha = 0.0f, E0 = 0.0f, epsilon = 0.0f, shift = 0.0f, Delta = 1.0f, Te = 0.0f, omega = 0.0f;

    float totalTime = 0.0f;
    // Ramps 0 -> 1 over the first few blocks (see finishBlock()) - a brief "breath catching" onset
    // rather than a hard start, matching the upstream always-voicing behavior.
    float intensity = 0.0f;
    static constexpr float loudness = 1.0f;

    static constexpr float vibratoAmount = 0.005f;
    static constexpr float vibratoFrequency = 6.0f;
};

} // namespace PinkTrombone
