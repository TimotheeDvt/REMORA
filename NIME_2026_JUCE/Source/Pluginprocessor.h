#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>


/**
 * OSC format expected:
 *   /esp32/imu    → float ax, ay, az, gx, gy, gz, mx, my, mz
 *   /esp32/connected → string ipAddress
 */
class NIMEReceiverProcessor
    : public juce::AudioProcessor,
      private juce::OSCReceiver::Listener<juce::OSCReceiver::RealtimeCallback>,
      private juce::Timer {
public:
  // IMU data snapshot
  struct IMUData {
    // Accelerometer (g)
    std::atomic<float> ax{0.f}, ay{0.f}, az{0.f};
    // Gyroscope (deg/s)
    std::atomic<float> gx{0.f}, gy{0.f}, gz{0.f};
    // Magnetometer (µT)
    std::atomic<float> mx{0.f}, my{0.f}, mz{0.f};
  };

  NIMEReceiverProcessor();
  ~NIMEReceiverProcessor() override;

  // OSC control
  bool startOSCReceiver(int port);
  void stopOSCReceiver();
  bool isOSCConnected() const noexcept { return oscConnected.load(); }
  int getCurrentPort() const noexcept { return currentPort.load(); }

  // Stats
  float getMessagesPerSecond() const noexcept {
    return messagesPerSecond.load();
  }
  int getTotalMessageCount() const noexcept { return totalMessages.load(); }
  int getConnectedDeviceCount() const noexcept {
    return connectedDeviceCount.load();
  }

  // Last known IP of connected device
  juce::String getLastConnectedIP() const;

  // IMU data access
  const IMUData &getIMUData() const noexcept { return imuData; }

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
  // OSC callbacks
  void oscMessageReceived(const juce::OSCMessage &message) override;

  // Timer callback - computes msg/sec every second
  void timerCallback() override;

  juce::OSCReceiver oscReceiver;

  std::atomic<bool> oscConnected{false};
  std::atomic<int> currentPort{0};

  // Rate counting
  std::atomic<int> messageCountThisTick{0};
  std::atomic<float> messagesPerSecond{0.f};
  std::atomic<int> totalMessages{0};

  // Device tracking
  std::atomic<int> connectedDeviceCount{0};

  mutable juce::CriticalSection ipMutex;
  juce::String lastConnectedIP;

  // IMU data
  IMUData imuData;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NIMEReceiverProcessor)
};