#pragma once

#include "../IMappingStrategy.h"
#include <algorithm>
#include <cmath>


class LeadDroneMapping : public IMappingStrategy {
public:
  void prepare(double /*sampleRate*/) override {}

  const char *getName() const override { return "Lead + Drone"; }

  void process(const StaffSoundParams &params, MappingOutput &out) override;
};