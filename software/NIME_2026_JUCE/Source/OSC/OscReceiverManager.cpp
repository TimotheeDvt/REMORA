#include "OscReceiverManager.h"

OscReceiverManager::OscReceiverManager() {}

OscReceiverManager::~OscReceiverManager() {
  stopOSCReceiver();
}

bool OscReceiverManager::startOSCReceiver(int port) {
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

void OscReceiverManager::stopOSCReceiver() {
  if (oscConnected.load()) {
    oscReceiver.disconnect();
    oscReceiver.removeListener(this);
    oscConnected.store(false);
    currentPort.store(0);
  }
}

juce::String OscReceiverManager::getLastConnectedIP() const {
  juce::ScopedLock sl(ipMutex);
  return lastConnectedIP;
}

void OscReceiverManager::updateMessagesPerSecond() {
  const int count = messageCountThisTick.exchange(0, std::memory_order_relaxed);
  messagesPerSecond.store(count, std::memory_order_relaxed);
}

void OscReceiverManager::oscMessageReceived(const juce::OSCMessage &message) {
  const auto address = message.getAddressPattern().toString();

  if (address == "/esp32/imu") {
    // Expected: ax ay az  gx gy gz  mx my mz  qw qx qy qz ip (13 floats + 1 string)
    if (message.size() >= 9) {
      lastMessageReceivedTicks.store(juce::Time::getHighResolutionTicks(), std::memory_order_relaxed);

      imuData.seq.fetch_add(1, std::memory_order_release);
      imuData.ax.store(message[0].getFloat32(), std::memory_order_relaxed);
      imuData.ay.store(message[1].getFloat32(), std::memory_order_relaxed);
      imuData.az.store(message[2].getFloat32(), std::memory_order_relaxed);

      imuData.gx.store(message[3].getFloat32(), std::memory_order_relaxed);
      imuData.gy.store(message[4].getFloat32(), std::memory_order_relaxed);
      imuData.gz.store(message[5].getFloat32(), std::memory_order_relaxed);

      imuData.mx.store(message[6].getFloat32(), std::memory_order_relaxed);
      imuData.my.store(message[7].getFloat32(), std::memory_order_relaxed);
      imuData.mz.store(message[8].getFloat32(), std::memory_order_relaxed);

      // Fallback in case old code sends only 9 floats
      if (message.size() >= 13) {
        imuData.qw.store(message[9].getFloat32(), std::memory_order_relaxed);
        imuData.qx.store(message[10].getFloat32(), std::memory_order_relaxed);
        imuData.qy.store(message[11].getFloat32(), std::memory_order_relaxed);
        imuData.qz.store(message[12].getFloat32(), std::memory_order_relaxed);
      }
      imuData.seq.fetch_add(1, std::memory_order_release);

      // Check for the IP address string
      if (message.size() >= 14 && message[13].isString()) {
        const juce::String ip = message[13].getString();

        juce::ScopedLock sl(ipMutex);
        if (lastConnectedIP != ip) {
          lastConnectedIP = ip;
          ipVersion.fetch_add(1, std::memory_order_release);
        }
      }
    }

    messageCountThisTick.fetch_add(1, std::memory_order_relaxed);
    totalMessages.fetch_add(1, std::memory_order_relaxed);
  } else if (address == "/esp32/connected") {
    if (message.size() >= 1 && message[0].isString()) {
      const juce::String ip = message[0].getString();
      juce::ScopedLock sl(ipMutex);
      if (lastConnectedIP != ip) {
        lastConnectedIP = ip;
        ipVersion.fetch_add(1, std::memory_order_release);
        connectedDeviceCount.fetch_add(1, std::memory_order_relaxed);
      }
    }
  }
}