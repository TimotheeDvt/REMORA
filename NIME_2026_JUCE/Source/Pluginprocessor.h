#pragma once

#include "DSP/BoStaffSynth.h"
#include "DSP/MathHelpers.h"
#include "Data/IMUData.h"
#include "OSC/OscReceiverManager.h"
#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <vector>


struct OrientationPoint {
  MathHelpers::Quat orientation;
  juce::uint32 timestamp;
};

class NIMEReceiverProcessor : public juce::AudioProcessor, private juce::Timer {
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
  int getTotalMessageCount() const noexcept {
    return oscManager.getTotalMessageCount();
  }
  int getConnectedDeviceCount() const noexcept {
    return oscManager.getConnectedDeviceCount();
  }
  int64_t getLastMessageReceivedTicks() const noexcept {
    return oscManager.getLastMessageReceivedTicks();
  }

  // Last known IP of connected device
  juce::String getLastConnectedIP() const {
    return oscManager.getLastConnectedIP();
  }

  // IMU data access
  const IMUData &getIMUData() const noexcept { return oscManager.getIMUData(); }

  // Calibration
  enum class CalibState { Idle, WaitingPoseA, WaitingPoseB, WaitingPoseC, Done };
  void startCalibration();
  void recordPoseA();
  void recordPoseB();
  void recordPoseC();
  void computeCorrection();
  int getCalibState() const { return calibState.load(); }
  MathHelpers::Quat getCalibratedQuat() const;

  // Sound toggle
  void setSoundEnabled(bool shouldBeEnabled) {
    synth.setSoundEnabled(shouldBeEnabled);
  }
  bool isSoundEnabled() const { return synth.isSoundEnabled(); }

  // Standard AudioProcessor overrides
  void prepareToPlay(double sampleRate, int samplesPerBlock) override;
  void releaseResources() override {}
  void processBlock(juce::AudioBuffer<float> &buffer,
                    juce::MidiBuffer &midiMessages) override;

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

  // DSP Engine
  BoStaffSynth synth;

  std::atomic<int> calibState{(int)CalibState::Idle};

  std::atomic<float> poseAw{1.f}, poseAx{0.f}, poseAy{0.f}, poseAz{0.f};
  std::atomic<float> poseBw{1.f}, poseBx{0.f}, poseBy{0.f}, poseBz{0.f};
  std::atomic<float> poseCw{1.f}, poseCx{0.f}, poseCy{0.f}, poseCz{0.f};

  // The derived correction quaternion
  std::atomic<float> corrW{1.f}, corrX{0.f}, corrY{0.f}, corrZ{0.f};

  std::array<OrientationPoint, 2048> orientationHistory;
  std::atomic<size_t> historyWriteIndex{0};

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NIMEReceiverProcessor)
};