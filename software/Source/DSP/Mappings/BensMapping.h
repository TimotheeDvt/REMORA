#pragma once

#include "../IMappingStrategy.h"
#include "AzimutMapping.h"
#include <JuceHeader.h>

// Merges AzimutMapping's full body-motion mapping with a simple
// pitch/roll/yaw melody. Above the gate speed, Azimut drives the sound;
// below it, the simple melody takes over. The two are cross-faded across
// a speed band around the gate so crossing the threshold never clicks.
class BensMapping : public IMappingStrategy {
public:
    BensMapping();

    const char* getName() const override { return "Ben's"; }
    void prepare(double sampleRate) override;
    void process(const StaffSoundParams& in, MappingOutput& out) override;

private:
    AzimutMapping azimut_;

    MonitorParam& speed_monitor_;
    MonitorParam& azimut_amount_monitor_;

    float smoothed_gyroscope_magnitude_ = 0.f;
    // Time constant approx 25ms at 100Hz, matches AzimutMapping's own smoothing.
    static constexpr float kGyroscopeSmoothingCoefficient = 0.35f;

    // Gate speed (deg/s): below it, the simple melody plays; above it,
    // Azimut takes over. kGateSpeedBandDegPerSec widens the switch into a
    // ramp instead of a hard cut.
    static constexpr float kGateSpeedThresholdDegPerSec = 150.0f;
    static constexpr float kGateSpeedBandDegPerSec = 60.0f;

    static constexpr float kRootFrequencyHz = 130.81f; // C3, matches AzimutMapping

    float current_simple_semitones_ = 0.f;

    void processSimpleMelody(const StaffSoundParams& in, MappingOutput& out);
    static void blendMappingOutput(const MappingOutput& simple, const MappingOutput& azimut, float azimutAmount, MappingOutput& out);
};
