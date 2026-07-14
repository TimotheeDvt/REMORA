#include "PluginEditor.h"
#include "DSP/MathHelpers.h"
#include "UI/DebugLog.h"
#include "UI/Palette.h"
#include "UI/StyleHelpers.h"
#include <BinaryData.h>
#include <initializer_list>

NIMEReceiverEditor::NIMEReceiverEditor(NIMEReceiverProcessor &p)
    : AudioProcessorEditor(&p), processor(p) {
  debug.print.blue("Plugin Editor created.");
  setResizable(true, true);
  setResizeLimits(780, 450, 4096, 4096);
  setSize(780, 780);

  // Title
  styleLabel(titleLabel, "NIME  OSC  RECEIVER", 13.f, Palette::textMid,
             juce::Justification::centredLeft);
  addAndMakeVisible(titleLabel);

  // Connect button
  styleButton(connectButton, "CONNECT",
             {{juce::TextButton::buttonColourId, Palette::accentDim},
              {juce::TextButton::buttonOnColourId, Palette::red},
              {juce::TextButton::textColourOffId, Palette::textHi}},
             [this] { toggleConnection();
            }
  );
  addAndMakeVisible(connectButton);

  // Sound toggle button
  soundButton.setClickingTogglesState(true);
  soundButton.setToggleState(processor.isSoundEnabled(),
                             juce::dontSendNotification);
  styleButton(soundButton, "", {{juce::TextButton::textColourOffId, Palette::textHi}},
             [this] {
               const bool isEnabled = soundButton.getToggleState();
               processor.setSoundEnabled(isEnabled);
               soundButton.setButtonText(isEnabled ? "SOUND ON" : "MUTED");
               soundButton.setColour(juce::TextButton::buttonColourId,
                                     isEnabled ? Palette::accentDim
                                               : Palette::red.darker(0.3f));
               soundButton.setColour(juce::TextButton::buttonOnColourId,
                                     isEnabled ? Palette::accentDim
                                               : Palette::red.darker(0.3f));
             }
  );
  soundButton.onClick(); // Trigger once to set initial text and colors
  soundButton.addShortcut(juce::KeyPress(juce::KeyPress::spaceKey, 0, 0));
  addAndMakeVisible(soundButton);

  // Show Data button
  styleButton(showDataButton, "RAW DATA", Palette::ButtonTheme::secondary,
             [this] {
               if (rawDataWindow && rawDataWindow->isVisible()) {
                 rawDataWindow->setVisible(false);
               } else {
                 if (!rawDataWindow) {
                   rawDataWindow = std::make_unique<RawDataWindow>(processor);
                 }
                 rawDataWindow->setVisible(true);
                 rawDataWindow->toFront(true);
               }
             }
  );
  addAndMakeVisible(showDataButton);

  // Debug button
  styleButton(debugButton, "DEBUG", Palette::ButtonTheme::secondary,
             [this] {
               if (debug.isWindowOpen())
                 debug.hide();
               else
                 debug.show();
             }
  );
  addAndMakeVisible(debugButton);

  // DSP button
  styleButton(dspButton, "DSP", Palette::ButtonTheme::secondary,
             [this] {
               if (dspWindow && dspWindow->isVisible()) {
                 dspWindow->setVisible(false);
               } else {
                 if (!dspWindow) dspWindow = std::make_unique<DSPWindow>(processor);
                 dspWindow->setVisible(true);
                 dspWindow->toFront(true);
               }
             }
  );
  addAndMakeVisible(dspButton);

  styleButton(calibrateButton, "CALIBRATE", Palette::ButtonTheme::secondary,
             [this] { onCalibrateClicked(); }
  );
  addAndMakeVisible(calibrateButton);

  calibrationOverlay.onClicked = [this] { onCalibrateClicked(); };
  calibrationOverlay.setStepText("CALIBRATION REQUIRED",
                                 "Click to begin calibration", Palette::yellow);
  addAndMakeVisible(calibrationOverlay);

  // Status dot (repurposed Label as a coloured dot)
  statusDot.setOpaque(false);
  addAndMakeVisible(statusDot);

  styleLabel(latencyValueLabel, "-", 24.f, Palette::textHi,
             juce::Justification::centredRight);
  styleLabel(latencyLabel, "ms latency", 11.f, Palette::textMid,
             juce::Justification::centredRight);
  addAndMakeVisible(latencyLabel);
  addAndMakeVisible(latencyValueLabel);

  boStaffVisualizer.setTrailLifetime(0.6f);
  addAndMakeVisible(boStaffVisualizer);

  // 60 Hz UI refresh timer
  startTimerHz(60);

  // Sync UI state with the processor's initial connection
  connected = processor.isOSCConnected();
  updateConnectionUI();

  debug.addChangeListener(this);

  // Show the big calibration overlay in place of the staff view until
  // calibration has been completed at least once for this session.
  updateCalibrationVisibility();

  // Auto-open all auxiliary windows on launch
  // if (!rawDataWindow) {
  //   showDataButton.onClick();
  //   if (!processor.rawDataBounds.isEmpty()) rawDataWindow->setBounds(processor.rawDataBounds);
  // }
  // if (!dspWindow) {
  //   dspButton.onClick();
  //   if (!processor.dspBounds.isEmpty()) dspWindow->setBounds(processor.dspBounds);
  // }
  // if (!debug.isWindowOpen()) {
  //   debug.show();
  //   if (!processor.debugBounds.isEmpty()) debug.setBounds(processor.debugBounds);
  // }
}

void NIMEReceiverEditor::saveWindowBoundsToProcessor() {
  if (rawDataWindow && rawDataWindow->isVisible())
    processor.rawDataBounds = rawDataWindow->getBounds();
  if (dspWindow && dspWindow->isVisible())
    processor.dspBounds = dspWindow->getBounds();
  if (debug.isWindowOpen())
    processor.debugBounds = debug.getBounds();
}

NIMEReceiverEditor::~NIMEReceiverEditor() {
  saveWindowBoundsToProcessor();
  debug.print.blue("Plugin Editor destroyed.");
  debug.removeChangeListener(this);
  stopTimer();
  rawDataWindow.reset();
  dspWindow.reset();
}

void NIMEReceiverEditor::paint(juce::Graphics &g) {
  // Background
  g.fillAll(Palette::bg);

  const auto w = getWidth();
  const auto h = getHeight();

  const int visualizerH = (h * 2) / 3;
  const int topSpace = h - visualizerH;

  const int numRows = 4;
  const int itemH = (topSpace - padding * (numRows + 1)) / numRows;

  // Draw the logo at the top right (next to the status dot)
  juce::Image logo = juce::ImageCache::getFromMemory(BinaryData::logo_png,
                                                     BinaryData::logo_pngSize);
  if (logo.isValid()) {
    g.drawImageWithin(logo, w - padding - 40, padding, 40, itemH,
                      juce::RectanglePlacement::centred);
  }

  // Top divider
  g.setColour(Palette::border);
  float div1Y = padding * 1.5f + itemH;
  g.drawHorizontalLine(static_cast<int>(div1Y), static_cast<float>(padding),
                       static_cast<float>(w - padding));

  // Status dot
  const bool isReceivingData = processor.getMessagesPerSecond() > 0.f;
  g.setColour(isReceivingData ? Palette::green : Palette::red);
  g.fillEllipse(static_cast<float>(w - padding - 40 - padding - 8),
                padding + itemH / 2.0f - 4.0f, 8.f, 8.f);

  // Draw 3D Staff separation
  g.setColour(Palette::border);
  float div2Y = topSpace - padding / 2.0f;
  g.drawHorizontalLine(static_cast<int>(div2Y), static_cast<float>(padding),
                       static_cast<float>(w - padding));
}

void NIMEReceiverEditor::resized() {
  const int w = getWidth();
  const int h = getHeight();

  const int visualizerH = (h * 2) / 3;
  const int topSpace = h - visualizerH;

  const int numRows = 4;
  const int itemH = (topSpace - padding * (numRows + 1)) / numRows;

  int currentY = padding;

  auto layoutRow = [&](int y, std::initializer_list<juce::Component *> comps) {
    int n = static_cast<int>(comps.size());
    if (n == 0)
      return;
    int itemW = (w - padding * (n + 1)) / n;
    int currentX = padding;
    for (auto *c : comps) {
      if (c != nullptr)
        c->setBounds(currentX, y, itemW, itemH);
      currentX += itemW + padding;
    }
  };

  // Row 0: Title
  layoutRow(currentY, {&titleLabel});
  currentY += itemH + padding;

  // Row 1: Connect, Sound, Latency Value
  layoutRow(currentY, {&connectButton, &soundButton, &latencyValueLabel});
  currentY += itemH + padding;

  // Row 2: Raw Data, DSP, Debug, Latency Label
  layoutRow(currentY, {&showDataButton, &dspButton, &debugButton, &latencyLabel});
  currentY += itemH + padding;

  // Row 3: Calibrate
  layoutRow(currentY, {&calibrateButton});
  currentY += itemH + padding;

  // Bottom 2/3 space: the staff visualizer once calibrated, or the big
  // calibration overlay until then.
  const juce::Rectangle<int> bottomArea(padding, topSpace, w - 2 * padding,
                                        visualizerH - padding);
  boStaffVisualizer.setBounds(bottomArea);
  calibrationOverlay.setBounds(bottomArea);
}

void NIMEReceiverEditor::changeListenerCallback(
    juce::ChangeBroadcaster *source) {
  juce::ignoreUnused(source);
}

void NIMEReceiverEditor::timerCallback() {
  refreshMainStats();

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
      audioBufferMs +=
          (processor.getLatencySamples() / cachedSampleRate) * 1000.0;
    }

    double oscLatencyMs = (processor.getMessagesPerSecond() > 0.f)
                          ? (1000.0 / processor.getMessagesPerSecond())
                          : 0.0;
    const double totalLatencyMs = audioBufferMs + oscLatencyMs;

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
      debug.print.green("OSC Receiver connected on port", port);
      connected = true;
      updateConnectionUI();
    }
  } else {
    processor.stopOSCReceiver();
    debug.print.red("OSC Receiver disconnected.");
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

void NIMEReceiverEditor::onCalibrateClicked() {
  auto state = (NIMEReceiverProcessor::CalibState)processor.getCalibState();

  juce::String overlayButtonText;
  juce::String hintText;
  juce::Colour hintColour = Palette::yellow;
  bool nowDone = false;

  if (state == NIMEReceiverProcessor::CalibState::Idle ||
      state == NIMEReceiverProcessor::CalibState::Done) {
    processor.startCalibration();
    debug.print.yellow("Calibration started: Waiting for Pose A");
    overlayButtonText = "POSE A ->";
    hintText = "Hold staff HORIZONTAL pointing FORWARD, then click";
  } else if (state == NIMEReceiverProcessor::CalibState::WaitingPoseA) {
    processor.recordPoseA();
    debug.print.yellow("Recorded Pose A. Waiting for Pose B");
    overlayButtonText = "POSE B ->";
    hintText = "Hold staff VERTICAL pointing UP, then click";
  } else if (state == NIMEReceiverProcessor::CalibState::WaitingPoseB) {
    processor.recordPoseB();
    debug.print.yellow("Recorded Pose B. Waiting for Pose C");
    overlayButtonText = "POSE C ->";
    hintText = "Hold staff HORIZONTAL pointing RIGHT, then click";
  } else if (state == NIMEReceiverProcessor::CalibState::WaitingPoseC) {
    processor.recordPoseC();
    debug.print.green("Recorded Pose C. Calibration complete.");
    overlayButtonText = "CALIBRATE";
    hintText = "Calibration complete.";
    hintColour = Palette::green;
    nowDone = true;
  }

  calibrateButton.setButtonText(nowDone ? "CALIBRATE" : "Calibrating...");

  calibrationOverlay.setStepText(overlayButtonText, hintText, hintColour);

  updateCalibrationVisibility();
}

void NIMEReceiverEditor::updateCalibrationVisibility() {
  const bool calibrationDone =
      (NIMEReceiverProcessor::CalibState)processor.getCalibState() ==
      NIMEReceiverProcessor::CalibState::Done;

  boStaffVisualizer.setVisible(calibrationDone);
  calibrationOverlay.setVisible(!calibrationDone);
}