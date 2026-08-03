#include "OscReceiverManager.h"
#include <cmath>

namespace {
void storeIfFinite(std::atomic<float>& dest, float value) {
  if (std::isfinite(value))
    dest.store(value, std::memory_order_relaxed);
}
}

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
      storeIfFinite(imuData.ax, message[0].getFloat32());
      storeIfFinite(imuData.ay, message[1].getFloat32());
      storeIfFinite(imuData.az, message[2].getFloat32());

      storeIfFinite(imuData.gx, message[3].getFloat32());
      storeIfFinite(imuData.gy, message[4].getFloat32());
      storeIfFinite(imuData.gz, message[5].getFloat32());

      storeIfFinite(imuData.mx, message[6].getFloat32());
      storeIfFinite(imuData.my, message[7].getFloat32());
      storeIfFinite(imuData.mz, message[8].getFloat32());

      // Fallback in case old code sends only 9 floats
      if (message.size() >= 13) {
        storeIfFinite(imuData.qw, message[9].getFloat32());
        storeIfFinite(imuData.qx, message[10].getFloat32());
        storeIfFinite(imuData.qy, message[11].getFloat32());
        storeIfFinite(imuData.qz, message[12].getFloat32());
      }
      imuData.seq.fetch_add(1, std::memory_order_release);

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