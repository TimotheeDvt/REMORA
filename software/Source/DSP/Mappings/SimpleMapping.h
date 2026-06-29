#pragma once
#include "../IMappingStrategy.h"
#include <JuceHeader.h>
#include <cmath>

class SimpleMapping : public IMappingStrategy {
public:
    const char* getName() const override { return "Simple (Pitch+Roll)"; }
    void prepare(double sampleRate) override;
    void process(const StaffSoundParams& in, MappingOutput& out) override;
};