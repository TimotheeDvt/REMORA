#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <vector>
#include "Data/IMUData.h"
#include "DSP/MathHelpers.h"
#include "DSP/BoStaffSynth.h"
#include "OSC/OscReceiverManager.h"

struct OrientationPoint {
  MathHelpers::Quat orientation;
  juce::uint32 timestamp;
};

class NIMEReceiverProcessor
    : public juce::AudioProcessor,
      private juce::Timer {
public:
  NIMEReceiverProcessor();
  ~NIMEReceiverProcessor() override;

  // OSC control
  bool startOSCReceiver(int port) { return oscManager.startOSCReceiver(port); }
  void stopOSCReceiver() { oscManager.stopOSCReceiver(); }
  bool isOSCConnected() const noexcept { return oscManager.isOSCConnected(); }
  int getCurrentPort() const noexcept { return oscManager.getCurrentPort(); }

  // Stats
  float getMessagesPerSecond() const noexcept {
    return oscManager.getMessagesPerSecond();
  }
  int getTotalMessageCount() const noexcept { return oscManager.getTotalMessageCount(); }
  int getConnectedDeviceCount() const noexcept {
    return oscManager.getConnectedDeviceCount();
  }
  int64_t getLastMessageReceivedTicks() const noexcept { return oscManager.getLastMessageReceivedTicks(); }

  // Last known IP of connected device
  juce::String getLastConnectedIP() const { return oscManager.getLastConnectedIP(); }

  // IMU data access
  const IMUData &getIMUData() const noexcept { return oscManager.getIMUData(); }

  // Calibration
  void calibrate();
  MathHelpers::Quat getCalibratedQuat() const;

  MathHelpers::Quat getMappedRawQuat() const;

  int getAxisMap(int axisIndex) const;
  void setAxisMap(int axisIndex, int mapCode);

  // Sound toggle
  void setSoundEnabled(bool shouldBeEnabled) { synth.setSoundEnabled(shouldBeEnabled); }
  bool isSoundEnabled() const { return synth.isSoundEnabled(); }

  // Standard AudioProcessor overrides
  void prepareToPlay(double sampleRate, int samplesPerBlock) override;
  void releaseResources() override {}
  void processBlock(juce::AudioBuffer<float> &buffer, juce::MidiBuffer &midiMessages) override;

  std::vector<OrientationPoint> getRecentOrientations(float maxAgeMs) const;

  // Tell the host we only support Mono or Stereo output
  bool isBusesLayoutSupported(const BusesLayout &layouts) const override {
    // Reject any input buses
    if (layouts.getMainInputChannelSet() != juce::AudioChannelSet::disabled())
      return false;

    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono() &&
        layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
      return false;
    return true;
  }

  juce::AudioProcessorEditor *createEditor() override;
  bool hasEditor() const override { return true; }

  const juce::String getName() const override { return "NIME OSC Receiver"; }
  bool acceptsMidi() const override { return false; }
  bool producesMidi() const override { return false; }
  bool isMidiEffect() const override { return false; }
  double getTailLengthSeconds() const override { return 0.0; }

  int getNumPrograms() override { return 1; }
  int getCurrentProgram() override { return 0; }
  void setCurrentProgram(int) override {}
  const juce::String getProgramName(int) override { return {}; }
  void changeProgramName(int, const juce::String &) override {}

  void getStateInformation(juce::MemoryBlock &) override {}
  void setStateInformation(const void *, int) override {}

private:
  // Timer callback - computes msg/sec every second
  void timerCallback() override;

  OscReceiverManager oscManager;

  std::atomic<float> calibW{1.f};
  std::atomic<float> calibX{0.f};
  std::atomic<float> calibY{0.f};
  std::atomic<float> calibZ{0.f};

  // DSP Engine
  BoStaffSynth synth;

  std::atomic<int> axisMapX { 0 }; // default: +Raw X
  std::atomic<int> axisMapY { 2 }; // default: +Raw Y
  std::atomic<int> axisMapZ { 4 }; // default: +Raw Z

  std::array<OrientationPoint, 2048> orientationHistory;
  std::atomic<size_t> historyWriteIndex { 0 };

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NIMEReceiverProcessor)
};