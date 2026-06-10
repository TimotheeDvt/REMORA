#include "PluginEditor.h"
#include "DSP/MathHelpers.h"
#include "UI/Palette.h"
#include "UI/StyleHelpers.h"
#include <BinaryData.h>

NIMEReceiverEditor::NIMEReceiverEditor(NIMEReceiverProcessor &p)
    : AudioProcessorEditor(&p), processor(p) {
  setResizable(true, true);
  setResizeLimits(520, 300, 4096, 4096);
  setSize(520, 300);

  // Title
  styleLabel(titleLabel, "NIME  OSC  RECEIVER", 13.f, Palette::textMid,
             juce::Justification::centredLeft);
  addAndMakeVisible(titleLabel);

  // Connect button
  connectButton.setButtonText("CONNECT");
  connectButton.setColour(juce::TextButton::buttonColourId, Palette::accentDim);
  connectButton.setColour(juce::TextButton::buttonOnColourId, Palette::red);
  connectButton.setColour(juce::TextButton::textColourOffId, Palette::textHi);
  connectButton.onClick = [this] { toggleConnection(); };
  addAndMakeVisible(connectButton);

  // Sound toggle button
  soundButton.setClickingTogglesState(true);
  soundButton.setToggleState(processor.isSoundEnabled(),
                             juce::dontSendNotification);
  soundButton.setColour(juce::TextButton::textColourOffId, Palette::textHi);
  soundButton.onClick = [this] {
    const bool isEnabled = soundButton.getToggleState();
    processor.setSoundEnabled(isEnabled);
    soundButton.setButtonText(isEnabled ? "SOUND ON" : "MUTED");
    soundButton.setColour(juce::TextButton::buttonColourId,
                          isEnabled ? Palette::accentDim
                                    : Palette::red.darker(0.3f));
    soundButton.setColour(juce::TextButton::buttonOnColourId,
                          isEnabled ? Palette::accentDim
                                    : Palette::red.darker(0.3f));
  };
  soundButton.onClick(); // Trigger once to set initial text and colors
  soundButton.addShortcut(juce::KeyPress(juce::KeyPress::spaceKey, 0, 0));
  addAndMakeVisible(soundButton);

  // Show Data button
  showDataButton.setButtonText("RAW DATA");
  showDataButton.setColour(juce::TextButton::buttonColourId, Palette::panel);
  showDataButton.setColour(juce::TextButton::textColourOffId, Palette::textMid);
  showDataButton.onClick = [this] {
    if (rawDataWindow && rawDataWindow->isVisible()) {
      rawDataWindow->setVisible(false);
    } else {
      if (!rawDataWindow) {
        rawDataWindow = std::make_unique<RawDataWindow>(processor);
      }
      rawDataWindow->setVisible(true);
      rawDataWindow->toFront(true);
    }
  };
  addAndMakeVisible(showDataButton);

  calibrateButton.setButtonText("CALIBRATE");
  calibrateButton.setColour(juce::TextButton::buttonColourId, Palette::panel);
  calibrateButton.setColour(juce::TextButton::textColourOffId,
                            Palette::textMid);
  calibrateButton.onClick = [this] {
    auto state = (NIMEReceiverProcessor::CalibState)processor.getCalibState();
    if (state == NIMEReceiverProcessor::CalibState::Idle ||
        state == NIMEReceiverProcessor::CalibState::Done) {
      processor.startCalibration();
      calibrateButton.setButtonText("POSE A ->");
      calibHintLabel.setText("Hold staff HORIZONTAL pointing FORWARD, then click",
                             juce::dontSendNotification);
      calibHintLabel.setColour(juce::Label::textColourId, Palette::yellow);
    } else if (state == NIMEReceiverProcessor::CalibState::WaitingPoseA) {
      processor.recordPoseA();
      calibrateButton.setButtonText("POSE B ->");
      calibHintLabel.setText("Hold staff VERTICAL pointing UP, then click",
                             juce::dontSendNotification);
    } else if (state == NIMEReceiverProcessor::CalibState::WaitingPoseB) {
      processor.recordPoseB();
      calibrateButton.setButtonText("POSE C ->");
      calibHintLabel.setText("Hold staff HORIZONTAL pointing RIGHT, then click",
                             juce::dontSendNotification);
    } else if (state == NIMEReceiverProcessor::CalibState::WaitingPoseC) {
      processor.recordPoseC();
      calibrateButton.setButtonText("CALIBRATE");
      calibHintLabel.setText("Calibration complete.",
                             juce::dontSendNotification);
      calibHintLabel.setColour(juce::Label::textColourId, Palette::green);
    }
  };
  addAndMakeVisible(calibrateButton);

  styleLabel(calibHintLabel, "", 10.f, Palette::textLo, juce::Justification::centredLeft);
  addAndMakeVisible(calibHintLabel);

  // Mapping selector
  mappingCombo.setColour(juce::ComboBox::backgroundColourId, Palette::panel);
  mappingCombo.setColour(juce::ComboBox::textColourId, Palette::textHi);
  mappingCombo.setColour(juce::ComboBox::outlineColourId, Palette::border);
  mappingCombo.setColour(juce::ComboBox::arrowColourId, Palette::textMid);

  // Populate from the synth's mapping list
  for (int i = 0; i < processor.getSynth().getMappingCount(); ++i) {
      const char* name = processor.getSynth().getMappingName(i);
      if (name != nullptr)
          mappingCombo.addItem(name, i + 1);   // ComboBox IDs are 1-based
  }
  mappingCombo.setSelectedId(processor.getMappingStrategy() + 1,
                             juce::dontSendNotification);
  mappingCombo.onChange = [this] {
      processor.setMappingStrategy(mappingCombo.getSelectedId() - 1);
  };
  addAndMakeVisible(mappingCombo);

  // Status dot (repurposed Label as a coloured dot)
  statusDot.setOpaque(false);
  addAndMakeVisible(statusDot);

  styleLabel(latencyValueLabel, "-", 52.f, Palette::textHi,
             juce::Justification::centredRight);
  styleLabel(latencyLabel, "ms latency", 11.f, Palette::textMid,
             juce::Justification::centredLeft);
  addAndMakeVisible(latencyLabel);
  addAndMakeVisible(latencyValueLabel);

  boStaffVisualizer.setTrailLifetime(0.6f);
  addAndMakeVisible(boStaffVisualizer);

  // 60 Hz UI refresh timer
  startTimerHz(60);

  // Sync UI state with the processor's initial connection
  connected = processor.isOSCConnected();
  updateConnectionUI();
}

NIMEReceiverEditor::~NIMEReceiverEditor() {
  stopTimer();
  rawDataWindow.reset();
}

void NIMEReceiverEditor::paint(juce::Graphics &g) {
  // Background
  g.fillAll(Palette::bg);

  const auto w = getWidth();

  // Draw the logo at the top right (next to the status dot)
  juce::Image logo = juce::ImageCache::getFromMemory(BinaryData::logo_png,
                                                     BinaryData::logo_pngSize);
  if (logo.isValid()) {
    g.drawImageWithin(logo, w - 80, 6, 40, 24,
                      juce::RectanglePlacement::centred);
  }

  // Top divider
  g.setColour(Palette::border);
  g.drawHorizontalLine(36, 12.f, static_cast<float>(w - 12));

  // Status dot
  const bool isReceivingData = processor.getMessagesPerSecond() > 0.f;
  g.setColour(isReceivingData ? Palette::green : Palette::red);
  g.fillEllipse(static_cast<float>(getWidth() - 28), 14.f, 8.f, 8.f);

  // Draw 3D Staff separation
  g.setColour(Palette::border);
  g.drawHorizontalLine(142, 24.f, static_cast<float>(w - 24));
}

void NIMEReceiverEditor::resized() {
  const int w = getWidth();

  // Title
  titleLabel.setBounds(14, 10, 220, 20);

  // Controls
  connectButton.setBounds(14, 64, 100, 24);
  soundButton.setBounds(122, 64, 100, 24);
  showDataButton.setBounds(14, 100, 80, 24);
  calibrateButton.setBounds(104, 100, 115, 24);
  calibHintLabel.setBounds(14, 128, w - 28, 14);

  mappingCombo.setBounds(228, 100, 160, 24);

  // Latency display
  latencyValueLabel.setBounds(w - 200, 44, 130, 60);
  latencyLabel.setBounds(w - 68, 82, 80, 18);

  boStaffVisualizer.setBounds(0, 144, w, getHeight() - 144);
}

void NIMEReceiverEditor::timerCallback() {
  refreshMainStats();

  if (!mappingCombo.isPopupActive()) {
    const int currentMapping = processor.getMappingStrategy();
    if (mappingCombo.getSelectedId() != currentMapping + 1)
        mappingCombo.setSelectedId(currentMapping + 1, juce::dontSendNotification);
  }

  boStaffVisualizer.updateStaff(
      processor.getCalibratedQuat(),
      processor.getRecentOrientations(boStaffVisualizer.getTrailLifetimeMs()));
  repaint();
}

void NIMEReceiverEditor::refreshMainStats() {
  static double cachedSampleRate = 0.0;
  static int cachedBlockSize = 0;

  if (cachedSampleRate <= 0.0)
    cachedSampleRate = processor.getSampleRate();
  if (cachedBlockSize <= 0)
    cachedBlockSize = processor.getBlockSize();

  const int64_t lastTicks = processor.getLastMessageReceivedTicks();
  if (lastTicks > 0) {
    const juce::uint32 now = juce::Time::getMillisecondCounter();
    if (lastLatencyUpdateMs != 0 && now - lastLatencyUpdateMs < 2000)
      return; // nothing to update yet

    const double dataAgeMs =
        juce::Time::highResolutionTicksToSeconds(
            juce::Time::getHighResolutionTicks() - lastTicks) *
        1000.0;

    double audioBufferMs = 0.0;
    if (cachedSampleRate > 0.0) {
      audioBufferMs += (cachedBlockSize / cachedSampleRate) * 1000.0;
      audioBufferMs += (processor.getLatencySamples() / cachedSampleRate) * 1000.0;
    }

    const double totalLatencyMs = dataAgeMs + audioBufferMs;

    // Only show valid latency if we're actively receiving (e.g. less than 1
    // second ago)
    if (dataAgeMs < 1000.0) {
      lastLatencyUpdateMs = now;
      latencyValueLabel.setText(juce::String(totalLatencyMs, 1),
                                juce::dontSendNotification);
      latencyValueLabel.setColour(
          juce::Label::textColourId,
          totalLatencyMs > 20.0
              ? Palette::red
              : (totalLatencyMs > 10.0 ? Palette::yellow : Palette::green));
    } else {
      latencyValueLabel.setText("-", juce::dontSendNotification);
      latencyValueLabel.setColour(juce::Label::textColourId, Palette::textMid);
      lastLatencyUpdateMs = 0; // reset so it instantly updates when reconnected
    }
  } else {
    latencyValueLabel.setText("-", juce::dontSendNotification);
    latencyValueLabel.setColour(juce::Label::textColourId, Palette::textMid);
    lastLatencyUpdateMs = 0;
  }
}

void NIMEReceiverEditor::toggleConnection() {
  if (!connected) {
    const int port = udpPort;

    if (processor.startOSCReceiver(port)) {
      connected = true;
      updateConnectionUI();
    }
  } else {
    processor.stopOSCReceiver();
    connected = false;
    updateConnectionUI();
  }
}

void NIMEReceiverEditor::updateConnectionUI() {
  if (connected) {
    connectButton.setButtonText("DISCONNECT");
    connectButton.setColour(juce::TextButton::buttonColourId,
                            Palette::red.darker(0.3f));
  } else {
    connectButton.setButtonText("CONNECT");
    connectButton.setColour(juce::TextButton::buttonColourId,
                            Palette::accentDim);
  }
}