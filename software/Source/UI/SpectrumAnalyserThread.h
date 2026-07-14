#pragma once

#include "../DSP/BoStaffSynth.h"
#include <JuceHeader.h>
#include <array>

// Runs the spectrum-display FFT on its own background thread.
//
// The audio thread (BoStaffSynth::processBlock) only fills a lock-free
// fifo and flips `nextFFTBlockReady`. Everything CPU-heavy - windowing,
// the forward FFT, and the dB conversion - used to run inline on the
// message thread from DSPComponent's 30Hz Timer, competing with GUI
// painting/event handling. It now runs here instead, so the message
// thread only ever copies out an already-computed result.
class SpectrumAnalyserThread : public juce::Thread {
public:
  static constexpr int fftSize = BoStaffSynth::fftSize;
  static constexpr int numBins = fftSize / 2;

  explicit SpectrumAnalyserThread(BoStaffSynth &synthToAnalyse);
  ~SpectrumAnalyserThread() override;

  void run() override;

  // Called from the message thread. Copies the latest computed spectrum
  // (in dB) into outDb. Returns false if no new block has been analysed
  // since the last call (so callers don't redo path-building/repaint work
  // for a result they've already consumed).
  bool getLatestMagnitudesDb(std::array<float, numBins> &outDb);

private:
  BoStaffSynth &synth;

  juce::dsp::FFT forwardFFT;
  juce::dsp::WindowingFunction<float> window;
  std::array<float, static_cast<size_t>(fftSize * 2)> fftWorkspace{};

  juce::SpinLock resultLock;
  std::array<float, numBins> latestMagnitudesDb{};
  std::atomic<bool> newResultAvailable{false};

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectrumAnalyserThread)
};
