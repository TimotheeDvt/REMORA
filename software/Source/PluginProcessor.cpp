#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "DSP/Calibration.h"

REMORAProcessor::REMORAProcessor()
    : AudioProcessor(BusesProperties().withOutput(
          "Output", juce::AudioChannelSet::stereo(), true)) {
  recentOrientationsScratch.reserve(512);
  startTimer(1000);

  startOSCReceiver(8000);

  // Make Azimut the default mapping on launch
  for (int i = 0; i < synth.getMappingCount(); ++i) {
    if (juce::String(synth.getMappingName(i)).containsIgnoreCase("Azimut")) {
      setMappingStrategy(i);
      break;
    }
  }
}

REMORAProcessor::~REMORAProcessor() { stopTimer(); }

// Timer - fires every 1000ms on the message thread
void REMORAProcessor::timerCallback() {
  oscManager.updateMessagesPerSecond();
}

void REMORAProcessor::startCalibration() {
  calibState.store((int)CalibState::WaitingPoseA);
}

void REMORAProcessor::recordPoseA() {
  const auto &d = getIMUData();
  poseAw.store(d.qw.load(std::memory_order_relaxed));
  poseAx.store(d.qx.load(std::memory_order_relaxed));
  poseAy.store(d.qy.load(std::memory_order_relaxed));
  poseAz.store(d.qz.load(std::memory_order_relaxed));
  calibState.store((int)CalibState::WaitingPoseB);
}

void REMORAProcessor::recordPoseB() {
  const auto &d = getIMUData();
  poseBw.store(d.qw.load(std::memory_order_relaxed));
  poseBx.store(d.qx.load(std::memory_order_relaxed));
  poseBy.store(d.qy.load(std::memory_order_relaxed));
  poseBz.store(d.qz.load(std::memory_order_relaxed));
  calibState.store((int)CalibState::WaitingPoseC);
}

void REMORAProcessor::recordPoseC() {
  const auto &d = getIMUData();
  poseCw.store(d.qw.load(std::memory_order_relaxed));
  poseCx.store(d.qx.load(std::memory_order_relaxed));
  poseCy.store(d.qy.load(std::memory_order_relaxed));
  poseCz.store(d.qz.load(std::memory_order_relaxed));
  const bool ok = computeCorrection();
  calibState.store((int)(ok ? CalibState::Done : CalibState::Failed));
}

bool REMORAProcessor::computeCorrection() {
  MathHelpers::Quat qA{poseAw.load(), poseAx.load(), poseAy.load(),
                       poseAz.load()};
  MathHelpers::Quat qB{poseBw.load(), poseBx.load(), poseBy.load(),
                       poseBz.load()};
  MathHelpers::Quat qC{poseCw.load(), poseCx.load(), poseCy.load(),
                       poseCz.load()};

  MathHelpers::Quat qAlign;
  if (!Calibration::computeAlignQuat(qA, qB, qC, qAlign))
    return false;

  const auto qCorr = Calibration::computeCorrection(qA, qB, qAlign);
  alignQuat.store(MathHelpers::multiply(qAlign, Calibration::computeRollFix(qA, qCorr, qAlign)));
  corrQuat.store(qCorr);
  return true;
}

bool REMORAProcessor::startPacketLog(const juce::File &file) {
  auto quatLine = [](const char *name, MathHelpers::Quat q) {
    return juce::String(name) + " " + juce::String(q.w, 7) + " " + juce::String(q.x, 7) + " " +
           juce::String(q.y, 7) + " " + juce::String(q.z, 7);
  };
  return oscManager.startPacketLog(file, {quatLine("align", alignQuat.load()), quatLine("corr", corrQuat.load())});
}

bool REMORAProcessor::rezeroHeading() {
  MathHelpers::Quat corrected;
  if (!Calibration::rezeroHeading(getCalibratedQuat(), corrQuat.load(), corrected))
    return false;
  corrQuat.store(corrected);
  return true;
}

MathHelpers::Quat REMORAProcessor::getCalibratedQuat() const {
  const auto &d = getIMUData();
  IMURawSnapshot snap;
  MathHelpers::Quat q_raw;
  // Bounded retry: a torn read only shifts one UI frame, so never spin indefinitely.
  bool consistent = false;
  for (int attempt = 0; attempt < 16 && !consistent; ++attempt)
    consistent = d.trySnapshot(snap);
  if (consistent)
    q_raw = {snap.qw, snap.qx, snap.qy, snap.qz};
  else
    q_raw = {d.qw.load(std::memory_order_relaxed), d.qx.load(std::memory_order_relaxed),
             d.qy.load(std::memory_order_relaxed), d.qz.load(std::memory_order_relaxed)};

  return Calibration::calibrate(q_raw, corrQuat.load(), alignQuat.load());
}

void REMORAProcessor::prepareToPlay(double sampleRate,
                                          int samplesPerBlock) {
  synth.prepareToPlay(sampleRate, samplesPerBlock);
}

float REMORAProcessor::packetDeltaSeconds(const IMURawSnapshot &packet) {
  constexpr float kNominalSeconds = 0.01f;
  float dt = kNominalSeconds;
  if (hasPreviousPacket) {
    const double elapsed = (packet.hasSensorClock && previousPacket.hasSensorClock)
        ? static_cast<uint32_t>(packet.sensorMicros - previousPacket.sensorMicros) * 1e-6
        : juce::Time::highResolutionTicksToSeconds(packet.hostTicks - previousPacket.hostTicks);
    // Outside this range the stream restarted or stalled; fall back to the nominal interval.
    if (elapsed > 0.0005 && elapsed < 0.1)
      dt = static_cast<float>(elapsed);
  }
  previousPacket = packet;
  hasPreviousPacket = true;
  return dt;
}

void REMORAProcessor::processBlock(juce::AudioBuffer<float> &buffer,
                                         juce::MidiBuffer &) {
  constexpr double kMaxDataAgeMs = 150.0;
  const int64_t lastTicks = oscManager.getLastMessageReceivedTicks();
  const double dataAgeMs = (lastTicks > 0)
      ? juce::Time::highResolutionTicksToSeconds(
            juce::Time::getHighResolutionTicks() - lastTicks) * 1000.0
      : 1e9;
  const bool isReceivingValidData = isOSCConnected() && (dataAgeMs < kMaxDataAgeMs);

  // Every packet that arrived since the last block is analysed once, in order, with the time step
  // measured by the sensor. A backlog (e.g. the audio device was paused) is cut to its newest
  // packets so the mapping does not replay stale motion.
  const int received = oscManager.getPacketFifo().pop(pendingPackets.data(), static_cast<int>(pendingPackets.size()));
  constexpr int kMaxPacketsPerBlock = 16;
  const int first = juce::jmax(0, received - kMaxPacketsPerBlock);
  if (first > 0)
    hasPreviousPacket = false;

  const auto corr = corrQuat.load();
  const auto align = alignQuat.load();
  for (int i = first; i < received; ++i) {
    const IMURawSnapshot &packet = pendingPackets[static_cast<size_t>(i)];
    const StaffSoundParams params = Calibration::toStaffSoundParams(packet, corr, align, packetDeltaSeconds(packet));

    auto now = juce::Time::getMillisecondCounter();
    size_t idx = historyWriteIndex.load(std::memory_order_relaxed);
    orientationHistory[idx % orientationHistory.size()] = {
        MathHelpers::Quat{params.qw, params.qx, params.qy, params.qz}, now};
    historyWriteIndex.store(idx + 1, std::memory_order_release);

    if (isReceivingValidData)
      synth.processPacket(params);
  }
  if (!isReceivingValidData)
    hasPreviousPacket = false;

  synth.processBlock(buffer, isReceivingValidData);
}

std::vector<OrientationPoint>
REMORAProcessor::getRecentOrientations(float maxAgeMs) const {
  recentOrientationsScratch.clear();
  auto now = juce::Time::getMillisecondCounter();
  size_t writeIdx = historyWriteIndex.load(std::memory_order_acquire);

  size_t count = std::min(writeIdx, orientationHistory.size());
  for (size_t i = 0; i < count; ++i) {
    size_t idx = writeIdx - 1 - i;
    auto point = orientationHistory[idx % orientationHistory.size()];
    if (now - point.timestamp <= maxAgeMs) {
      recentOrientationsScratch.push_back(point);
    } else {
      break; // Older entries will be even further back
    }
  }
  std::reverse(recentOrientationsScratch.begin(),
               recentOrientationsScratch.end());
  return recentOrientationsScratch;
}

juce::AudioProcessorEditor *REMORAProcessor::createEditor() {
  return new REMORAEditor(*this);
}

juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter() {
  return new REMORAProcessor();
}

void REMORAProcessor::getStateInformation(juce::MemoryBlock &destData) {
  if (auto* ed = dynamic_cast<REMORAEditor*>(getActiveEditor()))
    ed->saveWindowBoundsToProcessor();

  juce::XmlElement xml("REMORAState");
  xml.setAttribute("rawDataBounds", rawDataBounds.toString());
  xml.setAttribute("dspBounds", dspBounds.toString());
  xml.setAttribute("debugBounds", debugBounds.toString());
  xml.setAttribute("globalVolume", (double)synth.uiGlobalVolume.load());
  xml.setAttribute("mappingStrategy", synth.getMappingStrategy());

  copyXmlToBinary(xml, destData);
}

void REMORAProcessor::setStateInformation(const void *data, int sizeInBytes) {
  std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
  if (xmlState != nullptr && xmlState->hasTagName("REMORAState")) {
    rawDataBounds = juce::Rectangle<int>::fromString(xmlState->getStringAttribute("rawDataBounds"));
    dspBounds = juce::Rectangle<int>::fromString(xmlState->getStringAttribute("dspBounds"));
    debugBounds = juce::Rectangle<int>::fromString(xmlState->getStringAttribute("debugBounds"));

    synth.uiGlobalVolume.store((float)xmlState->getDoubleAttribute("globalVolume", 1.0));
    setMappingStrategy(xmlState->getIntAttribute("mappingStrategy", getMappingStrategy()));
  }
}