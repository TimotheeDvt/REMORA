#pragma once

#include "PluginProcessor.h"
#include "UI/RawDataWindow.h"
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
  juce::Label portLabel;
  juce::TextEditor portEditor;
  juce::TextButton connectButton;
  juce::TextButton showDataButton;
  juce::TextButton calibrateButton;
  juce::Label statusDot;

  juce::Label latencyLabel;
  juce::Label latencyValueLabel;

  // State
  bool connected = false;
  juce::uint32 lastLatencyUpdateMs = 0;

  std::unique_ptr<RawDataWindow> rawDataWindow;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NIMEReceiverEditor)
};