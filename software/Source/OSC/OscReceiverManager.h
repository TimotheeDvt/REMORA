#pragma once

#include <JuceHeader.h>
#include "../DATA/IMUData.h"
#include "../DATA/ImuPacketFifo.h"

class OscReceiverManager : private juce::OSCReceiver::Listener<juce::OSCReceiver::RealtimeCallback> {
public:
  OscReceiverManager();
  ~OscReceiverManager() override;

  bool startOSCReceiver(int port);
  void stopOSCReceiver();
  bool isOSCConnected() const noexcept { return oscConnected.load(); }
  int getCurrentPort() const noexcept { return currentPort.load(); }

  // Called once per second (message thread): publishes the last second's rate and timing stats.
  void updateMessagesPerSecond();
  float getMessagesPerSecond() const noexcept { return static_cast<float>(messagesPerSecond.load()); }
  int getTotalMessageCount() const noexcept { return totalMessages.load(); }
  int getConnectedDeviceCount() const noexcept { return connectedDeviceCount.load(); }
  int64_t getLastMessageReceivedTicks() const noexcept { return lastMessageReceivedTicks.load(std::memory_order_relaxed); }

  // Packet health, cumulative since the receiver started.
  int getLostPacketCount() const noexcept { return lostPackets.load(std::memory_order_relaxed); }
  int getReorderedPacketCount() const noexcept { return reorderedPackets.load(std::memory_order_relaxed); }
  int getMalformedPacketCount() const noexcept { return malformedPackets.load(std::memory_order_relaxed); }
  // Over the last second: mean and max host inter-arrival time, and network jitter (standard
  // deviation of arrival time minus sensor time, so it excludes the sensor's own send timing).
  float getMeanIntervalMs() const noexcept { return meanIntervalMs.load(std::memory_order_relaxed); }
  float getMaxIntervalMs() const noexcept { return maxIntervalMs.load(std::memory_order_relaxed); }
  float getTransitJitterMs() const noexcept { return transitJitterMs.load(std::memory_order_relaxed); }
  bool hasSensorClock() const noexcept { return sensorClockSeen.load(std::memory_order_relaxed); }

  // Writes every accepted packet to a CSV file (for timing measurements and offline replay with
  // Tools/AnalyzerCheck). Returns false if the file cannot be opened.
  // `preamble` lines (e.g. the calibration) are written first, each prefixed with '#'.
  bool startPacketLog(const juce::File &file, const juce::StringArray &preamble = {});
  void stopPacketLog();
  bool isLoggingPackets() const noexcept { return logging.load(); }

  std::atomic<int> ipVersion{0};
  int getIPVersion() const { return ipVersion.load(std::memory_order_acquire); }

  juce::String getLastConnectedIP() const;
  const IMUData &getIMUData() const noexcept { return imuData; }
  ImuPacketFifo &getPacketFifo() noexcept { return packetFifo; }

private:
  void oscMessageReceived(const juce::OSCMessage &message) override;
  // Returns false for duplicate / out-of-order packets, which are dropped.
  bool acceptSequence(uint32_t sequence);
  void accumulateTiming(const IMURawSnapshot &packet);

  juce::OSCReceiver oscReceiver;
  std::atomic<bool> oscConnected{false};
  std::atomic<int> currentPort{0};

  std::atomic<int> messageCountThisTick{0};
  std::atomic<int> messagesPerSecond{0};
  std::atomic<int> totalMessages{0};
  std::atomic<int> connectedDeviceCount{0};
  std::atomic<int64_t> lastMessageReceivedTicks{0};

  std::atomic<int> lostPackets{0}, reorderedPackets{0}, malformedPackets{0};
  std::atomic<float> meanIntervalMs{0.f}, maxIntervalMs{0.f}, transitJitterMs{0.f};
  std::atomic<bool> sensorClockSeen{false};

  // Network-thread-only state.
  bool haveSequence = false;
  uint32_t lastSequence = 0;
  int64_t lastArrivalTicks = 0;
  // Per-second accumulators, handed to the message thread under timingLock.
  juce::SpinLock timingLock;
  int intervalCount = 0;
  double intervalSumMs = 0.0, intervalMaxMs = 0.0;
  int transitCount = 0;
  uint32_t transitOriginMicros = 0;
  double transitSum = 0.0, transitSumSq = 0.0;

  juce::CriticalSection logLock;
  std::unique_ptr<juce::FileOutputStream> logStream;
  std::atomic<bool> logging{false};

  mutable juce::CriticalSection ipMutex;
  juce::String lastConnectedIP;

  IMUData imuData;
  ImuPacketFifo packetFifo;
};
