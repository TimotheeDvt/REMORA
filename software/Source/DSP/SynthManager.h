#pragma once

#include "MathHelpers.h"
#include <JuceHeader.h>
#include <atomic>
#include <cstdint>

#include "Engines/AdditiveSynthEngine.h"
#include "Engines/GranularSynthEngine.h"
#include "Engines/ISynthEngine.h"
#include "Engines/PinkTromboneEngine.h"
#include "Graph/NodeGraph.h"
#include "IMappingStrategy.h"
#include <memory>
#include <vector>

struct StaffSoundParams {
    float pitch = 0.f;
    float roll  = 0.f;
    float yaw   = 0.f;
    float gx = 0.f, gy = 0.f, gz = 0.f;
    float ax = 0.f, ay = 0.f, az = 0.f;
    // Magnetometer (uT)
    float mx = 0.f, my = 0.f, mz = 0.f;
    // Calibrated - passed through so mappings can rotate vectors into world frame without Euler reconstruction errors.
    float qw = 1.f, qx = 0.f, qy = 0.f, qz = 0.f;
    bool  isReceivingValidData = false;
};

// Owns every synth engine (additive, granular, ...) and the active motion-to-parameter mapping.
// Each block: evaluate the active mapping into one MappingOutput, let every engine render its own
// param sub-struct into its own scratch buffer, sum them, then apply shared gain/mute/volume.
class SynthManager {
public:
  SynthManager();
  ~SynthManager();

  void prepareToPlay(double sampleRate, int samplesPerBlock);

  void processBlock(juce::AudioBuffer<float> &buffer,
                    const StaffSoundParams& params);

  void setSoundEnabled(bool shouldBeEnabled);
  bool isSoundEnabled() const;

  void setMappingStrategy(int index);
  int  getMappingStrategy() const noexcept;

  const char* getMappingName(int index) const;
  int         getMappingCount() const noexcept;

  IMappingStrategy* getMapping(int index) const noexcept;

  int addGraphMapping(const juce::String& name, std::unique_ptr<Graph::NodeGraph> graph);

  int getBuiltInMappingCount() const noexcept { return builtInMappingCount; }

private:
  float currentSampleRate = 44100.f;

  std::vector<std::unique_ptr<IMappingStrategy>> mappings;
  int builtInMappingCount = 0;

  mutable juce::CriticalSection mappingsLock;

  std::atomic<int> activeMappingIndex{1};

  MappingOutput mappingOut;

  // Non-owning views into `engines`, for setParams() calls (each engine's param struct is its own
  // type, so this can't be done generically through ISynthEngine).
  std::vector<std::unique_ptr<ISynthEngine>> engines;
  std::vector<juce::AudioBuffer<float>> engineScratch;
  AdditiveSynthEngine* additiveEngine = nullptr;
  GranularSynthEngine* granularEngine = nullptr;
  PinkTromboneEngine* pinkTromboneEngine = nullptr;

  juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> masterGain{0.f};
  juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> muteGain{1.f};
  bool wasReceivingValidData = true;

  std::atomic<bool> soundEnabled{true};

public:
  std::atomic<float> uiGlobalVolume { 1.0f };

  static constexpr int fftOrder = 11;
  static constexpr int fftSize = 1 << fftOrder;
  std::array<float, fftSize> fifo {};
  std::array<float, static_cast<size_t>(fftSize * 2)> fftData {};
  size_t fifoIndex = 0;
  std::atomic<bool> nextFFTBlockReady { false };

  void pushNextSampleIntoFifo(float sample) noexcept;

  float getCurrentRootFreq() const { return additiveEngine != nullptr ? additiveEngine->getCurrentRootHz() : 110.0f; }
  float getCurrentLpfCutoff() const { return additiveEngine != nullptr ? additiveEngine->getCurrentLpfCutoffHz() : 20000.0f; }
  float getSampleRate() const { return currentSampleRate; }
};
