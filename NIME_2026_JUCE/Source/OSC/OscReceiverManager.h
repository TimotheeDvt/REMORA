#pragma once

#include <JuceHeader.h>
#include "../Data/IMUData.h"

class OscReceiverManager : private juce::OSCReceiver::Listener<juce::OSCReceiver::RealtimeCallback> {
public:
  OscReceiverManager();
  ~OscReceiverManager() override;

  bool startOSCReceiver(int port);
  void stopOSCReceiver();
  bool isOSCConnected() const noexcept { return oscConnected.load(); }
  int getCurrentPort() const noexcept { return currentPort.load(); }

  void updateMessagesPerSecond();
  float getMessagesPerSecond() const noexcept { return messagesPerSecond.load(); }
  int getTotalMessageCount() const noexcept { return totalMessages.load(); }
  int getConnectedDeviceCount() const noexcept { return connectedDeviceCount.load(); }
  int64_t getLastMessageReceivedTicks() const noexcept { return lastMessageReceivedTicks.load(std::memory_order_relaxed); }

  juce::String getLastConnectedIP() const;
  const IMUData &getIMUData() const noexcept { return imuData; }

  std::function<void()> onNewMessage;

private:
  void oscMessageReceived(const juce::OSCMessage &message) override;

  juce::OSCReceiver oscReceiver;
  std::atomic<bool> oscConnected{false};
  std::atomic<int> currentPort{0};

  std::atomic<int> messageCountThisTick{0};
  std::atomic<float> messagesPerSecond{0.f};
  std::atomic<int> totalMessages{0};
  std::atomic<int> connectedDeviceCount{0};
  std::atomic<int64_t> lastMessageReceivedTicks{0};

  mutable juce::CriticalSection ipMutex;
  juce::String lastConnectedIP;

  IMUData imuData;
};