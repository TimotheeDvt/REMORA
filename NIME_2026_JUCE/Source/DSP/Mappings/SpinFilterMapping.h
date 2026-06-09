#pragma once

#include "../IMappingStrategy.h"
#include <algorithm>
#include <cmath>

class SpinFilterMapping : public IMappingStrategy {
public:
    void prepare(double /*sampleRate*/) override {}

    const char *getName() const override { return "Spin Filter"; }

    void process(const StaffSoundParams &params, MappingOutput &out) override;
};