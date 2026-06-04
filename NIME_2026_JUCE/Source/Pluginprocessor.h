#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include "Data/IMUData.h"
#include "OSC/OscReceiverManager.h"

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

  // Standard AudioProcessor overrides
  void prepareToPlay(double, int) override {}
  void releaseResources() override {}
  void processBlock(juce::AudioBuffer<float> &, juce::MidiBuffer &) override {}

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

  // Broadcast to editor that new data arrived
  std::function<void()> onNewMessage;

private:
  // Timer callback - computes msg/sec every second
  void timerCallback() override;

  OscReceiverManager oscManager;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NIMEReceiverProcessor)
};