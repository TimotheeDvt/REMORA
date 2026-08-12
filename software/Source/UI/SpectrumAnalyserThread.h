#pragma once

#include "../DSP/SynthManager.h"
#include <JuceHeader.h>
#include <array>

// Runs the spectrum FFT here instead of inline on the message thread, where it used to compete with GUI painting.
class SpectrumAnalyserThread : public juce::Thread {
public:
  static constexpr int fftSize = SynthManager::fftSize;
  static constexpr int numBins = fftSize / 2;

  explicit SpectrumAnalyserThread(SynthManager &synthToAnalyse);
  ~SpectrumAnalyserThread() override;

  void run() override;

  // Returns false if no new block was analysed since the last call, so callers can skip redoing repaint work.
  bool getLatestMagnitudesDb(std::array<float, numBins> &outDb);

private:
  SynthManager &synth;

  juce::dsp::FFT forwardFFT;
  juce::dsp::WindowingFunction<float> window;
  std::array<float, static_cast<size_t>(fftSize * 2)> fftWorkspace{};

  juce::SpinLock resultLock;
  std::array<float, numBins> latestMagnitudesDb{};
  std::atomic<bool> newResultAvailable{false};

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectrumAnalyserThread)
};
