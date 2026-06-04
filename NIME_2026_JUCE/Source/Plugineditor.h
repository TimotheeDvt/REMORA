#pragma once

#include "PluginProcessor.h"
#include <JuceHeader.h>

class RawDataComponent : public juce::Component, private juce::Timer {
public:
  explicit RawDataComponent(NIMEReceiverProcessor &);
  ~RawDataComponent() override;

  void paint(juce::Graphics &) override;
  void resized() override;
  void timerCallback() override;

private:
  NIMEReceiverProcessor &processor;

  juce::Label rateValueLabel;
  juce::Label rateUnitLabel;
  juce::Label totalLabel;
  juce::Label totalValueLabel;
  juce::Label ipLabel;
  juce::Label ipValueLabel;

  juce::Label accelHeaderLabel;
  juce::Label axLabel, ayLabel, azLabel;
  juce::Label axVal, ayVal, azVal;

  juce::Label gyroHeaderLabel;
  juce::Label gxLabel, gyLabel, gzLabel;
  juce::Label gxVal, gyVal, gzVal;

  juce::Label magHeaderLabel;
  juce::Label mxLabel, myLabel, mzLabel;
  juce::Label mxVal, myVal, mzVal;

  static juce::String fmt(float v, int decimals = 3);
  static juce::Colour rateColour(float msgPerSec);

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RawDataComponent)
};

class RawDataWindow : public juce::DocumentWindow {
public:
  explicit RawDataWindow(NIMEReceiverProcessor &p);
  void closeButtonPressed() override;

private:
  RawDataComponent content;
  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RawDataWindow)
};

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
  juce::Label statusDot;

  juce::Label latencyLabel;
  juce::Label latencyValueLabel;

  // State
  bool connected = false;
  juce::uint32 lastLatencyUpdateMs = 0;

  std::unique_ptr<RawDataWindow> rawDataWindow;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NIMEReceiverEditor)
};