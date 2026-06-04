#pragma once

#include "PluginProcessor.h"
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
  void refreshStats();
  void toggleConnection();
  void updateConnectionUI();

  // Helpers
  static juce::String fmt(float v, int decimals = 3);
  static juce::Colour rateColour(float msgPerSec);

  NIMEReceiverProcessor &processor;

  // Connection bar
  juce::Label titleLabel;
  juce::Label portLabel;
  juce::TextEditor portEditor;
  juce::TextButton connectButton;
  juce::Label statusDot;

  // Stats panel
  juce::Label rateValueLabel;
  juce::Label rateUnitLabel;
  juce::Label totalLabel;
  juce::Label totalValueLabel;
  juce::Label ipLabel;
  juce::Label ipValueLabel;

  // IMU panel
  // Accelerometer
  juce::Label accelHeaderLabel;
  juce::Label axLabel, ayLabel, azLabel;
  juce::Label axVal, ayVal, azVal;

  // Gyroscope
  juce::Label gyroHeaderLabel;
  juce::Label gxLabel, gyLabel, gzLabel;
  juce::Label gxVal, gyVal, gzVal;

  // Magnetometer
  juce::Label magHeaderLabel;
  juce::Label mxLabel, myLabel, mzLabel;
  juce::Label mxVal, myVal, mzVal;

  // Sparkline (last N msg/sec values)
  static constexpr int kSparklineSize = 60;
  std::array<float, kSparklineSize> sparkline{};
  int sparklineHead = 0;

  // State
  bool connected = false;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NIMEReceiverEditor)
};