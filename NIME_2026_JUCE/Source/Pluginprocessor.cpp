#include "PluginProcessor.h"
#include "PluginEditor.h"

NIMEReceiverProcessor::NIMEReceiverProcessor()
    : AudioProcessor(BusesProperties()) {
  startTimer(1000);
}

NIMEReceiverProcessor::~NIMEReceiverProcessor() {
  stopTimer();
  stopOSCReceiver();
}

bool NIMEReceiverProcessor::startOSCReceiver(int port) {
  stopOSCReceiver(); // clean up any previous connection

  oscReceiver.addListener(this);

  if (oscReceiver.connect(port)) {
    oscConnected.store(true);
    currentPort.store(port);
    return true;
  }

  oscReceiver.removeListener(this);
  return false;
}

void NIMEReceiverProcessor::stopOSCReceiver() {
  if (oscConnected.load()) {
    oscReceiver.disconnect();
    oscReceiver.removeListener(this);
    oscConnected.store(false);
    currentPort.store(0);
  }
}

juce::String NIMEReceiverProcessor::getLastConnectedIP() const {
  juce::ScopedLock sl(ipMutex);
  return lastConnectedIP;
}

// OSC message handler
void NIMEReceiverProcessor::oscMessageReceived(
    const juce::OSCMessage &message) {
  const auto address = message.getAddressPattern().toString();

  if (address == "/esp32/imu") {
    // Expected: ax ay az  gx gy gz  mx my mz  qw qx qy qz ip (13 floats + 1 string)
    if (message.size() >= 9) {
      auto getFloat = [&](int idx) -> float {
        const auto &arg = message[idx];
        if (arg.isFloat32())
          return arg.getFloat32();
        if (arg.isInt32())
          return static_cast<float>(arg.getInt32());
        return 0.f;
      };

      imuData.ax.store(getFloat(0), std::memory_order_relaxed);
      imuData.ay.store(getFloat(1), std::memory_order_relaxed);
      imuData.az.store(getFloat(2), std::memory_order_relaxed);

      imuData.gx.store(getFloat(3), std::memory_order_relaxed);
      imuData.gy.store(getFloat(4), std::memory_order_relaxed);
      imuData.gz.store(getFloat(5), std::memory_order_relaxed);

      imuData.mx.store(getFloat(6), std::memory_order_relaxed);
      imuData.my.store(getFloat(7), std::memory_order_relaxed);
      imuData.mz.store(getFloat(8), std::memory_order_relaxed);

      // Fallback in case old code sends only 9 floats
      if (message.size() >= 13) {
        imuData.qw.store(getFloat(9), std::memory_order_relaxed);
        imuData.qx.store(getFloat(10), std::memory_order_relaxed);
        imuData.qy.store(getFloat(11), std::memory_order_relaxed);
        imuData.qz.store(getFloat(12), std::memory_order_relaxed);
      }

      // Check for the IP address string
      if (message.size() >= 14 && message[13].isString()) {
        const juce::String ip = message[13].getString();

        juce::ScopedLock sl(ipMutex);
        lastConnectedIP = ip;
      }
    }

    messageCountThisTick.fetch_add(1, std::memory_order_relaxed);
    totalMessages.fetch_add(1, std::memory_order_relaxed);

    if (onNewMessage)
      juce::MessageManager::callAsync([cb = onNewMessage] { cb(); });
  } else if (address == "/esp32/connected") {
    if (message.size() >= 1 && message[0].isString()) {
      const juce::String ip = message[0].getString();

      {
        juce::ScopedLock sl(ipMutex);
        lastConnectedIP = ip;
      }

      connectedDeviceCount.fetch_add(1, std::memory_order_relaxed);

      DBG("ESP32 connected from: " + ip);
    }
  }
}

// Timer - fires every 1000ms on the message thread
void NIMEReceiverProcessor::timerCallback() {
  const int count = messageCountThisTick.exchange(0, std::memory_order_relaxed);
  messagesPerSecond.store(static_cast<float>(count), std::memory_order_relaxed);
}

juce::AudioProcessorEditor *NIMEReceiverProcessor::createEditor() {
  return new NIMEReceiverEditor(*this);
}

juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter() {
  return new NIMEReceiverProcessor();
}