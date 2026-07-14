#pragma once

#include "PluginProcessor.h"
#include "UI/BoStaffVisualizer.h"
#include "UI/CalibrationOverlay.h"
#include "UI/RawDataWindow.h"
#include "UI/DSPWindow.h"
#include <JuceHeader.h>

class REMORAEditor : public juce::AudioProcessorEditor,
                           private juce::Timer,
                           private juce::ChangeListener {
public:
  explicit REMORAEditor(REMORAProcessor &);
  ~REMORAEditor() override;

  void paint(juce::Graphics &) override;
  void resized() override;

  void saveWindowBoundsToProcessor();

private:
  void timerCallback() override; // 60 Hz UI refresh
  void changeListenerCallback(juce::ChangeBroadcaster *source) override;
  void refreshMainStats();
  void toggleConnection();
  void updateConnectionUI();
  void onCalibrateClicked();
  void updateCalibrationVisibility();

  REMORAProcessor &processor;

  // Connection bar
  juce::Label titleLabel;
  juce::TextButton connectButton;
  juce::TextButton soundButton;
  juce::TextButton showDataButton;
  juce::TextButton debugButton;
  juce::TextButton dspButton;
  juce::Label statusDot;

  juce::Label latencyLabel;
  juce::Label latencyValueLabel;

  // State
  bool connected = false;
  int udpPort = 8000;
  juce::uint32 lastLatencyUpdateMs = 0;

  static constexpr int padding = 14;

  std::unique_ptr<RawDataWindow> rawDataWindow;
  std::unique_ptr<DSPWindow> dspWindow;

  juce::TextButton calibrateButton;

  BoStaffVisualizer boStaffVisualizer;
  CalibrationOverlay calibrationOverlay;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(REMORAEditor)
};