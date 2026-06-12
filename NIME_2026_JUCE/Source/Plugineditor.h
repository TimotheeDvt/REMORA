#pragma once

#include "PluginProcessor.h"
#include "UI/BoStaffVisualizer.h"
#include "UI/RawDataWindow.h"
#include <JuceHeader.h>

class NIMEReceiverEditor : public juce::AudioProcessorEditor,
                           private juce::Timer,
                           private juce::ChangeListener {
public:
  explicit NIMEReceiverEditor(NIMEReceiverProcessor &);
  ~NIMEReceiverEditor() override;

  void paint(juce::Graphics &) override;
  void resized() override;

private:
  void timerCallback() override; // 60 Hz UI refresh
  void changeListenerCallback(juce::ChangeBroadcaster *source) override;
  void refreshMainStats();
  void toggleConnection();
  void updateConnectionUI();

  NIMEReceiverProcessor &processor;

  // Connection bar
  juce::Label titleLabel;
  juce::TextButton connectButton;
  juce::TextButton soundButton;
  juce::TextButton showDataButton;
  juce::TextButton debugButton;
  juce::Label statusDot;
  juce::ComboBox mappingCombo;

  juce::Label latencyLabel;
  juce::Label latencyValueLabel;

  // State
  bool connected = false;
  int udpPort = 8000;
  juce::uint32 lastLatencyUpdateMs = 0;

  static constexpr int padding = 14;

  std::unique_ptr<RawDataWindow> rawDataWindow;

  juce::TextButton calibrateButton;
  juce::Label calibHintLabel;

  BoStaffVisualizer boStaffVisualizer;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NIMEReceiverEditor)
};