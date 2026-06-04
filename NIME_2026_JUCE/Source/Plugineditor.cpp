#include "PluginEditor.h"
#include "DSP/MathHelpers.h"
#include "UI/Palette.h"
#include "UI/StyleHelpers.h"

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

  // Top divider
  g.setColour(Palette::border);
  g.drawHorizontalLine(36, 12.f, static_cast<float>(w - 12));

  // Status dot
  const bool isReceivingData = processor.getMessagesPerSecond() > 0.f;
  g.setColour(isReceivingData ? Palette::green : Palette::red);
  g.fillEllipse(static_cast<float>(getWidth() - 28), 14.f, 8.f, 8.f);

  // Draw 3D Staff Simulation
  g.setColour(Palette::border);
  g.drawHorizontalLine(140, 24.f, static_cast<float>(w - 24));

  MathHelpers::Quat q = processor.getCalibratedQuat();

  const float scale = 90.f;
  const float cx = w / 2.f;
  const float cy = 250.f;

  // 3D -> 2D projection (Cabinet-style isometric)
  auto project = [cx, cy, scale](MathHelpers::Vec3 v) {
    const float depth = 0.3f; // Depth scaling factor for the Y axis
    return juce::Point<float>(cx + (v.x - v.y * depth) * scale,
                              cy - (v.z - v.y * depth) * scale);
  };

  // Draw world axes (X = Right/Red, Y = Depth/Green, Z = Up/Blue)
  auto pOrigin = project({0.f, 0.f, 0.f});
  auto pX = project({1.f, 0.f, 0.f});
  auto pY = project({0.f, 1.f, 0.f});
  auto pZ = project({0.f, 0.f, 1.f});

  g.setColour(Palette::red.withAlpha(0.6f));
  g.drawLine(pOrigin.x, pOrigin.y, pX.x, pX.y, 2.f);
  g.drawText("X", juce::Rectangle<float>(pX.x - 10.f, pX.y - 10.f, 20.f, 20.f),
             juce::Justification::centred);

  g.setColour(Palette::green.withAlpha(0.6f));
  g.drawLine(pOrigin.x, pOrigin.y, pY.x, pY.y, 2.f);
  g.drawText("Y", juce::Rectangle<float>(pY.x - 10.f, pY.y - 10.f, 20.f, 20.f),
             juce::Justification::centred);

  g.setColour(Palette::accent.withAlpha(0.6f));
  g.drawLine(pOrigin.x, pOrigin.y, pZ.x, pZ.y, 2.f);
  g.drawText("Z", juce::Rectangle<float>(pZ.x - 10.f, pZ.y - 10.f, 20.f, 20.f),
             juce::Justification::centred);

  // Staff resting horizontally along the X-axis
  MathHelpers::Vec3 top = {1.f, 0.f, 0.f};
  MathHelpers::Vec3 bottom = {-1.f, 0.f, 0.f};

  top = MathHelpers::rotate(top, q);
  bottom = MathHelpers::rotate(bottom, q);

  auto pTop = project(top);
  auto pBot = project(bottom);

  // Draw the staff line
  g.setColour(Palette::textMid);
  g.drawLine(pBot.x, pBot.y, pTop.x, pTop.y, 8.f);

  // Draw colored tips to help orient
  g.setColour(Palette::accentDim);
  g.fillEllipse(pBot.x - 6.f, pBot.y - 6.f, 12.f, 12.f);
  g.setColour(Palette::red);
  g.fillEllipse(pTop.x - 6.f, pTop.y - 6.f, 12.f, 12.f);
}

void NIMEReceiverEditor::resized() {
  const int w = getWidth();

  // Title
  titleLabel.setBounds(14, 10, 220, 20);

  // Port + connect
  portLabel.setBounds(14, 50, 70, 12);
  portEditor.setBounds(14, 64, 70, 24);
  connectButton.setBounds(92, 64, 100, 24);
  showDataButton.setBounds(14, 100, 100, 24);
  calibrateButton.setBounds(122, 100, 100, 24);

  // Latency display
  latencyValueLabel.setBounds(w - 200, 44, 130, 60);
  latencyLabel.setBounds(w - 68, 82, 80, 18);
}

void NIMEReceiverEditor::timerCallback() {
  refreshMainStats();
  repaint();
}

void NIMEReceiverEditor::refreshMainStats() {
  const int64_t lastTicks = processor.getLastMessageReceivedTicks();
  if (lastTicks > 0) {
    const double latencyMs =
        juce::Time::highResolutionTicksToSeconds(
            juce::Time::getHighResolutionTicks() - lastTicks) *
        1000.0;
    // Only show valid latency if we're actively receiving (e.g. less than 1
    // second ago)
    if (latencyMs < 1000.0) {
      const juce::uint32 now = juce::Time::getMillisecondCounter();
      if (lastLatencyUpdateMs == 0 || now - lastLatencyUpdateMs >= 2000) {
        lastLatencyUpdateMs = now;
        latencyValueLabel.setText(juce::String(latencyMs, 1),
                                  juce::dontSendNotification);
        latencyValueLabel.setColour(
            juce::Label::textColourId,
            latencyMs > 10.0
                ? Palette::red
                : (latencyMs > 5.0 ? Palette::yellow : Palette::green));
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