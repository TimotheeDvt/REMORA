#pragma once

#include "DSP/BoStaffSynth.h"
#include "DSP/MathHelpers.h"
#include "DATA/IMUData.h"
#include "DATA/OrientationPoint.h"
#include "OSC/OscReceiverManager.h"
#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <vector>

// Threading model:
//  - OSC reception:   OscReceiverManager listens with RealtimeCallback, so
//                      oscMessageReceived() runs on JUCE's dedicated OSC
//                      network thread, never the message or audio thread.
//  - Sound processing: processBlock() runs on the host's real-time audio
//                      thread, as usual for an AudioProcessor.
//  - GUI:              PluginEditor and its sub-windows run on the JUCE
//                      message thread; the spectrum analyser FFT they used
//                      to run inline has been moved to SpectrumAnalyserThread
//                      (see UI/SpectrumAnalyserThread.h) so it doesn't
//                      compete with GUI painting/event handling.
// All cross-thread data (IMU samples, orientation history, calibration
// quaternions) is passed via atomics/seqlocks rather than locks so none of
// these threads ever block each other.
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

  BoStaffSynth& getSynth() { return synth; }

  // Last known IP of connected device
  int getIPVersion() const { return oscManager.getIPVersion(); }
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

  void setMappingStrategy(int index) { synth.setMappingStrategy(index); }
  int  getMappingStrategy() const noexcept { return synth.getMappingStrategy(); }

  juce::Rectangle<int> rawDataBounds;
  juce::Rectangle<int> dspBounds;
  juce::Rectangle<int> debugBounds;

  // Standard AudioProcessor overrides
  void prepareToPlay(double sampleRate, int samplesPerBlock) override;
  void releaseResources() override {}
  using juce::AudioProcessor::processBlock;
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

  void getStateInformation(juce::MemoryBlock &destData) override;
  void setStateInformation(const void *data, int sizeInBytes) override;

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

  struct AtomicQuat {
    std::atomic<float> w{1.f}, x{0.f}, y{0.f}, z{0.f};
    std::atomic<unsigned> gen{0};

    void store(MathHelpers::Quat q) {
      gen.fetch_add(1, std::memory_order_release);
      w.store(q.w, std::memory_order_relaxed);
      x.store(q.x, std::memory_order_relaxed);
      y.store(q.y, std::memory_order_relaxed);
      z.store(q.z, std::memory_order_relaxed);
      gen.fetch_add(1, std::memory_order_release);
    }

    MathHelpers::Quat load() const {
      MathHelpers::Quat q;
      unsigned g;
      do {
        g = gen.load(std::memory_order_acquire);
        q.w = w.load(std::memory_order_relaxed);
        q.x = x.load(std::memory_order_relaxed);
        q.y = y.load(std::memory_order_relaxed);
        q.z = z.load(std::memory_order_relaxed);
      } while (gen.load(std::memory_order_acquire) != g || (g & 1));
      return q;
    }
  };

  // The derived correction quaternion
  AtomicQuat corrQuat;

  // The derived alignment quaternion (cached for performance)
  AtomicQuat alignQuat;

  std::array<OrientationPoint, 512> orientationHistory;
  std::atomic<size_t> historyWriteIndex{0};

  mutable std::vector<OrientationPoint> recentOrientationsScratch;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NIMEReceiverProcessor)
};