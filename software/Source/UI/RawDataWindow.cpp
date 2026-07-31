#include "RawDataWindow.h"
#include "Palette.h"
#include "StyleHelpers.h"
#include "../DSP/MathHelpers.h"

RawDataComponent::RawDataComponent(REMORAProcessor &p) : processor(p) {
  setSize(500, 350);

  styleLabel(rateValueLabel, "0", 52.f, Palette::textHi,
             juce::Justification::centredRight);
  styleLabel(rateUnitLabel, "msg / sec", 11.f, Palette::textMid,
             juce::Justification::centredLeft);
  addAndMakeVisible(rateValueLabel);
  addAndMakeVisible(rateUnitLabel);

  styleLabel(totalLabel, "TOTAL", 9.f, Palette::textLo);
  styleLabel(totalValueLabel, "0", 12.f, Palette::textMid);
  styleLabel(ipLabel, "DEVICE IP", 9.f, Palette::textLo);
  styleLabel(ipValueLabel, "-", 12.f, Palette::textMid);
  addAndMakeVisible(totalLabel);
  addAndMakeVisible(totalValueLabel);
  addAndMakeVisible(ipLabel);
  addAndMakeVisible(ipValueLabel);

  auto addIMURow = [&](juce::Label &header, const juce::String &title,
                       juce::Label &xLbl, juce::Label &yLbl, juce::Label &zLbl,
                       juce::Label &xVal, juce::Label &yVal, juce::Label &zVal,
                       const juce::String &xl, const juce::String &yl,
                       const juce::String &zl) {
    styleLabel(header, title, 9.f, Palette::accent);
    addAndMakeVisible(header);

    styleLabel(xLbl, xl, 9.f, Palette::textLo);
    styleLabel(yLbl, yl, 9.f, Palette::textLo);
    styleLabel(zLbl, zl, 9.f, Palette::textLo);
    addAndMakeVisible(xLbl);
    addAndMakeVisible(yLbl);
    addAndMakeVisible(zLbl);

    styleLabel(xVal, "0.000", 12.f, Palette::textHi,
               juce::Justification::centredRight);
    styleLabel(yVal, "0.000", 12.f, Palette::textHi,
               juce::Justification::centredRight);
    styleLabel(zVal, "0.000", 12.f, Palette::textHi,
               juce::Justification::centredRight);
    addAndMakeVisible(xVal);
    addAndMakeVisible(yVal);
    addAndMakeVisible(zVal);
  };

  addIMURow(accelHeaderLabel, "ACCELEROMETER  (g)", axLabel, ayLabel, azLabel,
            axVal, ayVal, azVal, "X", "Y", "Z");
  addIMURow(gyroHeaderLabel, "GYROSCOPE  (deg/s)", gxLabel, gyLabel, gzLabel,
            gxVal, gyVal, gzVal, "X", "Y", "Z");
  addIMURow(magHeaderLabel, "MAGNETOMETER  (microT)", mxLabel, myLabel, mzLabel,
            mxVal, myVal, mzVal, "X", "Y", "Z");

  styleLabel(eulerHeaderLabel, "EULER ANGLES  (rad)", 9.f, Palette::accent);
  addAndMakeVisible(eulerHeaderLabel);

  styleLabel(pitchLabel, "Pitch", 9.f, Palette::textLo);
  styleLabel(rollLabel, "Roll", 9.f, Palette::textLo);
  styleLabel(yawLabel, "Yaw", 9.f, Palette::textLo);
  addAndMakeVisible(pitchLabel);
  addAndMakeVisible(rollLabel);
  addAndMakeVisible(yawLabel);

  for (auto *slider : {&pitchSlider, &rollSlider, &yawSlider}) {
    slider->setSliderStyle(juce::Slider::LinearHorizontal);
    slider->setTextBoxStyle(juce::Slider::TextBoxRight, true, 60, 20);
    slider->setInterceptsMouseClicks(false, false); // Make read-only
    slider->setColour(juce::Slider::thumbColourId, juce::Colours::transparentBlack);
    slider->setColour(juce::Slider::trackColourId, Palette::accent);
    slider->setColour(juce::Slider::textBoxTextColourId, Palette::textHi);
    slider->setColour(juce::Slider::textBoxBackgroundColourId, Palette::bg);
    slider->setColour(juce::Slider::textBoxOutlineColourId, Palette::border);
    addAndMakeVisible(slider);
  }

  pitchSlider.setRange(-juce::MathConstants<float>::halfPi,
                       juce::MathConstants<float>::halfPi, 0.001);
  rollSlider.setRange(-juce::MathConstants<float>::pi,
                      juce::MathConstants<float>::pi, 0.001);
  yawSlider.setRange(-juce::MathConstants<float>::pi,
                     juce::MathConstants<float>::pi, 0.001);

  startTimerHz(60);
}

RawDataComponent::~RawDataComponent() { stopTimer(); }

void RawDataComponent::paint(juce::Graphics &g) {
  g.fillAll(Palette::bg);
  const auto w = getWidth();
  const auto h = getHeight();

  juce::Rectangle<float> ratePanel(12.f, 12.f, static_cast<float>(w - 24),
                                   70.f);
  g.setColour(Palette::panel);
  g.fillRoundedRectangle(ratePanel, 6.f);
  g.setColour(Palette::border);
  g.drawRoundedRectangle(ratePanel, 6.f, 1.f);

  juce::Rectangle<float> imuPanel(12.f, 94.f, static_cast<float>(w - 24),
                                  static_cast<float>(h - 106));
  g.setColour(Palette::panel);
  g.fillRoundedRectangle(imuPanel, 6.f);
  g.setColour(Palette::border);
  g.drawRoundedRectangle(imuPanel, 6.f, 1.f);
}

void RawDataComponent::resized() {
  const int w = getWidth();

  rateValueLabel.setBounds(w - 200, 16, 130, 60);
  rateUnitLabel.setBounds(w - 68, 48, 60, 18);

  totalLabel.setBounds(24, 28, 60, 14);
  totalValueLabel.setBounds(24, 42, 100, 16);
  ipLabel.setBounds(140, 28, 80, 14);
  ipValueLabel.setBounds(140, 42, 180, 16);

  const int imuX = 20;
  const int imuY0 = 100;
  const int rowH = 44;
  const int colW = (w - 40) / 3;

  auto placeIMURow = [&](juce::Label &header, juce::Label &xLbl,
                         juce::Label &yLbl, juce::Label &zLbl,
                         juce::Label &xVal, juce::Label &yVal,
                         juce::Label &zVal, int rowIndex) {
    const int y = imuY0 + rowIndex * rowH;
    header.setBounds(imuX, y, w - 40, 13);
    for (int col = 0; col < 3; ++col) {
      juce::Label *lbl = (col == 0) ? &xLbl : (col == 1) ? &yLbl : &zLbl;
      juce::Label *val = (col == 0) ? &xVal : (col == 1) ? &yVal : &zVal;
      const int cx = imuX + col * colW;
      lbl->setBounds(cx, y + 15, 20, 12);
      val->setBounds(cx + 20, y + 13, colW - 22, 16);
    }
  };

  placeIMURow(accelHeaderLabel, axLabel, ayLabel, azLabel, axVal, ayVal, azVal,
              0);
  placeIMURow(gyroHeaderLabel, gxLabel, gyLabel, gzLabel, gxVal, gyVal, gzVal,
              1);
  placeIMURow(magHeaderLabel, mxLabel, myLabel, mzLabel, mxVal, myVal, mzVal,
              2);

  const int eulerY = imuY0 + 3 * rowH + 10;
  eulerHeaderLabel.setBounds(imuX, eulerY, w - 40, 13);

  pitchLabel.setBounds(imuX, eulerY + 20, 40, 12);
  pitchSlider.setBounds(imuX + 40, eulerY + 16, w - 80, 20);

  rollLabel.setBounds(imuX, eulerY + 45, 40, 12);
  rollSlider.setBounds(imuX + 40, eulerY + 41, w - 80, 20);

  yawLabel.setBounds(imuX, eulerY + 70, 40, 12);
  yawSlider.setBounds(imuX + 40, eulerY + 66, w - 80, 20);
}

void RawDataComponent::timerCallback() {
  if (!isShowing())
    return; // Prevent formatting UI strings constantly if window is hidden

  const float mps = processor.getMessagesPerSecond();
  rateValueLabel.setText(juce::String(static_cast<int>(mps)),
                         juce::dontSendNotification);
  rateValueLabel.setColour(juce::Label::textColourId, rateColour(mps));

  totalValueLabel.setText(juce::String(processor.getTotalMessageCount()),
                          juce::dontSendNotification);

  static int lastIpVersion = -1;
  if (processor.getIPVersion() != lastIpVersion) {
    lastIpVersion = processor.getIPVersion();
    const juce::String ip = processor.getLastConnectedIP();
    ipValueLabel.setText(ip.isEmpty() ? "-" : ip, juce::dontSendNotification);
  }

  const auto &d = processor.getIMUData();

  static float prevAx = 0.f, prevAy = 0.f, prevAz = 0.f;
  static float prevGx = 0.f, prevGy = 0.f, prevGz = 0.f;
  static float prevMx = 0.f, prevMy = 0.f, prevMz = 0.f;

  auto updateIfChanged = [&](juce::Label& lbl, float& prev, float next) {
    if (std::abs(next - prev) > 0.0005f) {
      prev = next;
      lbl.setText(fmt(next), juce::dontSendNotification);
    }
  };

  IMURawSnapshot snap;
  if (d.trySnapshot(snap)) {
    updateIfChanged(axVal, prevAx, snap.ax);
    updateIfChanged(ayVal, prevAy, snap.ay);
    updateIfChanged(azVal, prevAz, snap.az);
    updateIfChanged(gxVal, prevGx, snap.gx);
    updateIfChanged(gyVal, prevGy, snap.gy);
    updateIfChanged(gzVal, prevGz, snap.gz);
    updateIfChanged(mxVal, prevMx, snap.mx);
    updateIfChanged(myVal, prevMy, snap.my);
    updateIfChanged(mzVal, prevMz, snap.mz);
  }

  auto q = processor.getCalibratedQuat();
  auto euler = MathHelpers::toEuler(q);
  pitchSlider.setValue(euler.pitch, juce::dontSendNotification);
  rollSlider.setValue(euler.roll, juce::dontSendNotification);
  yawSlider.setValue(euler.yaw, juce::dontSendNotification);
}

juce::String RawDataComponent::fmt(float v, int decimals) {
  return juce::String(v, decimals);
}

juce::Colour RawDataComponent::rateColour(float mps) {
  if (mps <= 10.f)
    return Palette::red;
  if (mps < 50.f)
    return Palette::yellow;
  if (mps < 90.f)
    return Palette::accent;
  return Palette::green;
}

RawDataWindow::RawDataWindow(REMORAProcessor &p)
    : DocumentWindow("Raw Data", Palette::bg, DocumentWindow::closeButton),
      content(p) {
  setContentNonOwned(&content, true);
  setResizable(false, false);
}

void RawDataWindow::closeButtonPressed() { setVisible(false); }