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
  stopPacketLog();
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

  const juce::SpinLock::ScopedLockType sl(timingLock);
  meanIntervalMs.store(intervalCount > 0 ? static_cast<float>(intervalSumMs / intervalCount) : 0.f,
                       std::memory_order_relaxed);
  maxIntervalMs.store(static_cast<float>(intervalMaxMs), std::memory_order_relaxed);
  if (transitCount > 1) {
    const double mean = transitSum / transitCount;
    const double variance = juce::jmax(0.0, transitSumSq / transitCount - mean * mean);
    transitJitterMs.store(static_cast<float>(std::sqrt(variance)), std::memory_order_relaxed);
  }
  intervalCount = 0;
  intervalSumMs = intervalMaxMs = 0.0;
  transitCount = 0;
  transitSum = transitSumSq = 0.0;
}

bool OscReceiverManager::startPacketLog(const juce::File &file, const juce::StringArray &preamble) {
  auto stream = std::make_unique<juce::FileOutputStream>(file);
  if (!stream->openedOk())
    return false;
  stream->setPosition(0);
  stream->truncate();
  for (const auto &line : preamble)
    *stream << "# " << line << "\n";
  *stream << "host_s,sequence,sensor_us,ax,ay,az,gx,gy,gz,mx,my,mz,qw,qx,qy,qz\n";
  const juce::ScopedLock sl(logLock);
  logStream = std::move(stream);
  logging.store(true);
  return true;
}

void OscReceiverManager::stopPacketLog() {
  const juce::ScopedLock sl(logLock);
  logging.store(false);
  if (logStream)
    logStream->flush();
  logStream.reset();
}

bool OscReceiverManager::acceptSequence(uint32_t sequence) {
  if (!haveSequence) {
    haveSequence = true;
    lastSequence = sequence;
    return true;
  }
  const int32_t step = static_cast<int32_t>(sequence - lastSequence);
  // A large jump either way means the staff rebooted (counter restarted); resynchronise.
  constexpr int32_t kResyncThreshold = 1000;
  if (step > kResyncThreshold || step < -kResyncThreshold) {
    lastSequence = sequence;
    return true;
  }
  if (step <= 0) {
    reorderedPackets.fetch_add(1, std::memory_order_relaxed);
    return false;
  }
  if (step > 1)
    lostPackets.fetch_add(step - 1, std::memory_order_relaxed);
  lastSequence = sequence;
  return true;
}

void OscReceiverManager::accumulateTiming(const IMURawSnapshot &packet) {
  const juce::SpinLock::ScopedLockType sl(timingLock);
  if (lastArrivalTicks != 0) {
    const double ms = juce::Time::highResolutionTicksToSeconds(packet.hostTicks - lastArrivalTicks) * 1000.0;
    ++intervalCount;
    intervalSumMs += ms;
    intervalMaxMs = juce::jmax(intervalMaxMs, ms);
  }
  lastArrivalTicks = packet.hostTicks;

  if (packet.hasSensorClock) {
    // Arrival minus send time, up to a constant clock offset; its spread is the network jitter.
    // Wrapped to 32 bits like the sensor clock so the difference stays small.
    const uint32_t hostMicros = static_cast<uint32_t>(
        static_cast<int64_t>(juce::Time::highResolutionTicksToSeconds(packet.hostTicks) * 1e6));
    const uint32_t transitMicros = hostMicros - packet.sensorMicros;
    // Measured relative to the window's first sample, so the sums stay small and well conditioned.
    if (transitCount == 0)
      transitOriginMicros = transitMicros;
    const double transitMs = static_cast<int32_t>(transitMicros - transitOriginMicros) / 1000.0;
    ++transitCount;
    transitSum += transitMs;
    transitSumSq += transitMs * transitMs;
  }
}

void OscReceiverManager::oscMessageReceived(const juce::OSCMessage &message) {
  const auto address = message.getAddressPattern().toString();

  if (address == "/esp32/imu") {
    // Expected: ax ay az  gx gy gz  mx my mz  qw qx qy qz (13 floats), ip (string),
    // then sequence and sensor time in us (int32, firmware with a sensor clock only).
    constexpr int kFloatCount = 13;
    bool wellFormed = message.size() >= kFloatCount;
    for (int i = 0; wellFormed && i < kFloatCount; ++i)
      wellFormed = message[i].isFloat32() && std::isfinite(message[i].getFloat32());
    if (!wellFormed) {
      malformedPackets.fetch_add(1, std::memory_order_relaxed);
      return;
    }

    IMURawSnapshot packet;
    packet.ax = message[0].getFloat32();  packet.ay = message[1].getFloat32();  packet.az = message[2].getFloat32();
    packet.gx = message[3].getFloat32();  packet.gy = message[4].getFloat32();  packet.gz = message[5].getFloat32();
    packet.mx = message[6].getFloat32();  packet.my = message[7].getFloat32();  packet.mz = message[8].getFloat32();
    packet.qw = message[9].getFloat32();  packet.qx = message[10].getFloat32();
    packet.qy = message[11].getFloat32(); packet.qz = message[12].getFloat32();
    packet.hostTicks = juce::Time::getHighResolutionTicks();

    if (message.size() >= 16 && message[14].isInt32() && message[15].isInt32()) {
      packet.sequence = static_cast<uint32_t>(message[14].getInt32());
      packet.sensorMicros = static_cast<uint32_t>(message[15].getInt32());
      packet.hasSensorClock = true;
      sensorClockSeen.store(true, std::memory_order_relaxed);
      if (!acceptSequence(packet.sequence))
        return;
    }

    lastMessageReceivedTicks.store(packet.hostTicks, std::memory_order_relaxed);

    imuData.seq.fetch_add(1, std::memory_order_release);
    storeIfFinite(imuData.ax, packet.ax);
    storeIfFinite(imuData.ay, packet.ay);
    storeIfFinite(imuData.az, packet.az);
    storeIfFinite(imuData.gx, packet.gx);
    storeIfFinite(imuData.gy, packet.gy);
    storeIfFinite(imuData.gz, packet.gz);
    storeIfFinite(imuData.mx, packet.mx);
    storeIfFinite(imuData.my, packet.my);
    storeIfFinite(imuData.mz, packet.mz);
    storeIfFinite(imuData.qw, packet.qw);
    storeIfFinite(imuData.qx, packet.qx);
    storeIfFinite(imuData.qy, packet.qy);
    storeIfFinite(imuData.qz, packet.qz);
    imuData.seq.fetch_add(1, std::memory_order_release);

    packetFifo.push(packet);
    accumulateTiming(packet);

    if (logging.load(std::memory_order_relaxed)) {
      const juce::ScopedLock sl(logLock);
      if (logStream) {
        juce::String line;
        line << juce::String(juce::Time::highResolutionTicksToSeconds(packet.hostTicks), 6) << ','
             << static_cast<juce::int64>(packet.sequence) << ',' << static_cast<juce::int64>(packet.sensorMicros);
        for (float v : {packet.ax, packet.ay, packet.az, packet.gx, packet.gy, packet.gz,
                        packet.mx, packet.my, packet.mz, packet.qw, packet.qx, packet.qy, packet.qz})
          line << ',' << juce::String(v, 6);
        *logStream << line << '\n';
      }
    }

    if (message.size() >= 14 && message[13].isString()) {
      const juce::String ip = message[13].getString();

      juce::ScopedLock sl(ipMutex);
      if (lastConnectedIP != ip) {
        lastConnectedIP = ip;
        ipVersion.fetch_add(1, std::memory_order_release);
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
