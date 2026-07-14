#include "SpectrumAnalyserThread.h"

SpectrumAnalyserThread::SpectrumAnalyserThread(BoStaffSynth &synthToAnalyse)
    : juce::Thread("Spectrum Analyser"),
      synth(synthToAnalyse),
      forwardFFT(BoStaffSynth::fftOrder),
      window(BoStaffSynth::fftSize, juce::dsp::WindowingFunction<float>::hann) {
  startThread();
}

SpectrumAnalyserThread::~SpectrumAnalyserThread() {
  stopThread(2000);
}

void SpectrumAnalyserThread::run() {
  while (!threadShouldExit()) {
    if (!synth.nextFFTBlockReady.exchange(false)) {
      // No new audio block since the last pass - avoid busy-spinning.
      wait(5);
      continue;
    }

    std::copy(synth.fftData.begin(), synth.fftData.begin() + fftSize,
               fftWorkspace.begin());
    std::fill(fftWorkspace.begin() + fftSize, fftWorkspace.end(), 0.0f);

    window.multiplyWithWindowingTable(fftWorkspace.data(), fftSize);
    forwardFFT.performFrequencyOnlyForwardTransform(fftWorkspace.data());

    constexpr float minDB = -100.0f;
    constexpr float maxDB = 0.0f;
    const float fftSizeDb = juce::Decibels::gainToDecibels((float) fftSize);

    std::array<float, numBins> localResult;
    for (size_t i = 0; i < static_cast<size_t>(numBins); ++i) {
      float level = juce::Decibels::gainToDecibels(fftWorkspace[i]) - fftSizeDb;
      localResult[i] = juce::jlimit(minDB, maxDB, level);
    }

    {
      const juce::SpinLock::ScopedLockType sl(resultLock);
      latestMagnitudesDb = localResult;
    }
    newResultAvailable.store(true, std::memory_order_release);
  }
}

bool SpectrumAnalyserThread::getLatestMagnitudesDb(std::array<float, numBins> &outDb) {
  if (!newResultAvailable.exchange(false, std::memory_order_acq_rel))
    return false;
  const juce::SpinLock::ScopedLockType sl(resultLock);
  outDb = latestMagnitudesDb;
  return true;
}
