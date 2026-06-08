#pragma once

#include "PluginProcessor.h"
#include "UI/RawDataWindow.h"
#include "UI/BoStaffVisualizer.h"
#include <JuceHeader.h>


class NIMEReceiverEditor : public juce::AudioProcessorEditor,
                           private juce::Timer {
public:
  explicit NIMEReceiverEditor(NIMEReceiverProcessor &);
  ~NIMEReceiverEditor() override;

  void paint(juce::Graphics &) override;
  void resized() override;

private:
  void timerCallback() override; // 60 Hz UI refresh
  void refreshMainStats();
  void toggleConnection();
  void updateConnectionUI();

  NIMEReceiverProcessor &processor;

  // Connection bar
  juce::Label titleLabel;
  juce::TextButton connectButton;
  juce::TextButton soundButton;
  juce::TextButton showDataButton;
  juce::TextButton calibrateButton;
  juce::Label statusDot;

  juce::Label latencyLabel;
  juce::Label latencyValueLabel;

  // State
  bool connected = false;
  int udpPort = 8000;
  juce::uint32 lastLatencyUpdateMs = 0;

  std::unique_ptr<RawDataWindow> rawDataWindow;

  BoStaffVisualizer boStaffVisualizer;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NIMEReceiverEditor)
};