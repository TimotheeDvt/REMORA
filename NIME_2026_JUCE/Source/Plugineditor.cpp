#include "PluginEditor.h"
#include "DSP/MathHelpers.h"
#include "UI/Palette.h"
#include "UI/StyleHelpers.h"
#include <BinaryData.h>

NIMEReceiverEditor::NIMEReceiverEditor(NIMEReceiverProcessor &p)
    : AudioProcessorEditor(&p), processor(p) {
  setSize(520, 360);
  setResizable(false, false);

  // Title
  styleLabel(titleLabel, "NIME  OSC  RECEIVER", 13.f, Palette::textMid,
             juce::Justification::centredLeft);
  addAndMakeVisible(titleLabel);

  // Port
  styleLabel(portLabel, "UDP PORT", 10.f, Palette::textLo);
  addAndMakeVisible(portLabel);

  portEditor.setText("8000");
  portEditor.setFont(juce::Font(juce::FontOptions().withHeight(13.f)));
  portEditor.setColour(juce::TextEditor::backgroundColourId, Palette::panel);
  portEditor.setColour(juce::TextEditor::textColourId, Palette::textHi);
  portEditor.setColour(juce::TextEditor::outlineColourId, Palette::border);
  portEditor.setColour(juce::TextEditor::focusedOutlineColourId,
                       Palette::accent);
  portEditor.setInputRestrictions(5, "0123456789");
  portEditor.setJustification(juce::Justification::centred);
  addAndMakeVisible(portEditor);

  // Connect button
  connectButton.setButtonText("CONNECT");
  connectButton.setColour(juce::TextButton::buttonColourId, Palette::accentDim);
  connectButton.setColour(juce::TextButton::buttonOnColourId, Palette::red);
  connectButton.setColour(juce::TextButton::textColourOffId, Palette::textHi);
  connectButton.onClick = [this] { toggleConnection(); };
  addAndMakeVisible(connectButton);

  // Sound toggle button
  soundButton.setClickingTogglesState(true);
  soundButton.setToggleState(processor.isSoundEnabled(), juce::dontSendNotification);
  soundButton.setColour(juce::TextButton::textColourOffId, Palette::textHi);
  soundButton.onClick = [this] {
    const bool isEnabled = soundButton.getToggleState();
    processor.setSoundEnabled(isEnabled);
    soundButton.setButtonText(isEnabled ? "SOUND ON" : "MUTED");
    soundButton.setColour(juce::TextButton::buttonColourId, isEnabled ? Palette::accentDim : Palette::red.darker(0.3f));
    soundButton.setColour(juce::TextButton::buttonOnColourId, isEnabled ? Palette::accentDim : Palette::red.darker(0.3f));
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

  // Calibrate button
  calibrateButton.setButtonText("CALIBRATE");
  calibrateButton.setColour(juce::TextButton::buttonColourId, Palette::panel);
  calibrateButton.setColour(juce::TextButton::textColourOffId, Palette::textMid);
  calibrateButton.onClick = [this] { processor.calibrate(); };
  addAndMakeVisible(calibrateButton);

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
  juce::Image logo = juce::ImageCache::getFromMemory(BinaryData::logo_png, BinaryData::logo_pngSize);
  if (logo.isValid()) {
    g.drawImageWithin(logo, w - 80, 6.f, 40, 24, juce::RectanglePlacement::centred);
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
  g.drawHorizontalLine(140, 24.f, static_cast<float>(w - 24));
}

void NIMEReceiverEditor::resized() {
  const int w = getWidth();

  // Title
  titleLabel.setBounds(14, 10, 220, 20);

  // Port + connect
  portLabel.setBounds(14, 50, 70, 12);
  portEditor.setBounds(14, 64, 70, 24);
  connectButton.setBounds(92, 64, 100, 24);
  soundButton.setBounds(200, 64, 100, 24);
  showDataButton.setBounds(14, 100, 100, 24);
  calibrateButton.setBounds(122, 100, 100, 24);

  // Latency display
  latencyValueLabel.setBounds(w - 200, 44, 130, 60);
  latencyLabel.setBounds(w - 68, 82, 80, 18);

  boStaffVisualizer.setBounds(0, 140, w, getHeight() - 140);
}

void NIMEReceiverEditor::timerCallback() {
  refreshMainStats();
  boStaffVisualizer.updateStaff(processor.getCalibratedQuat());
  repaint();
}

void NIMEReceiverEditor::refreshMainStats() {
  const int64_t lastTicks = processor.getLastMessageReceivedTicks();
  if (lastTicks > 0) {
    const double dataAgeMs =
        juce::Time::highResolutionTicksToSeconds(
            juce::Time::getHighResolutionTicks() - lastTicks) *
        1000.0;

    double audioBufferMs = 0.0;
    const double sampleRate = processor.getSampleRate();
    if (sampleRate > 0.0) {
      audioBufferMs += (processor.getBlockSize() / sampleRate) * 1000.0;
      audioBufferMs += (processor.getLatencySamples() / sampleRate) * 1000.0;
    }

    const double totalLatencyMs = dataAgeMs + audioBufferMs;

    // Only show valid latency if we're actively receiving (e.g. less than 1
    // second ago)
    if (dataAgeMs < 1000.0) {
      const juce::uint32 now = juce::Time::getMillisecondCounter();
      if (lastLatencyUpdateMs == 0 || now - lastLatencyUpdateMs >= 2000) {
        lastLatencyUpdateMs = now;
        latencyValueLabel.setText(juce::String(totalLatencyMs, 1),
                                  juce::dontSendNotification);
        latencyValueLabel.setColour(
            juce::Label::textColourId,
            totalLatencyMs > 10.0
                ? Palette::red
                : (totalLatencyMs > 20.0 ? Palette::yellow : Palette::green));
      }
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
    const int port = portEditor.getText().getIntValue();
    if (port < 1 || port > 65535) {
      portEditor.setColour(juce::TextEditor::outlineColourId, Palette::red);
      return;
    }
    portEditor.setColour(juce::TextEditor::outlineColourId, Palette::border);

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
    portEditor.setEnabled(false);
  } else {
    connectButton.setButtonText("CONNECT");
    connectButton.setColour(juce::TextButton::buttonColourId,
                            Palette::accentDim);
    portEditor.setEnabled(true);
  }
}