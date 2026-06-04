#include "PluginEditor.h"

// Colour palette
namespace Palette {
const juce::Colour bg{0xFF2A2A2A};
const juce::Colour panel{0xFF333333};
const juce::Colour border{0xFF555555};
const juce::Colour accent{0xFF5B9BD5};
const juce::Colour accentDim{0xFF3A5A7A};
const juce::Colour textHi{0xFFFFFFFF};
const juce::Colour textMid{0xFFCCCCCC};
const juce::Colour textLo{0xFF999999};
const juce::Colour red{0xFFD9534F};
const juce::Colour yellow{0xFFF0AD4E};
const juce::Colour green{0xFF5CB85C};
} // namespace Palette

namespace
{
    struct Vec3 { float x, y, z; };
    struct Quat { float w, x, y, z; };

    static Vec3 rotate(Vec3 v, Quat q)
    {
        float tx = 2.0f * (q.y * v.z - q.z * v.y);
        float ty = 2.0f * (q.z * v.x - q.x * v.z);
        float tz = 2.0f * (q.x * v.y - q.y * v.x);
        return {
            v.x + q.w * tx + (q.y * tz - q.z * ty),
            v.y + q.w * ty + (q.z * tx - q.x * tz),
            v.z + q.w * tz + (q.x * ty - q.y * tx)
        };
    }
}

static void
styleLabel(juce::Label &l, const juce::String &text, float fontSize,
           juce::Colour colour,
           juce::Justification just = juce::Justification::centredLeft) {
  l.setText(text, juce::dontSendNotification);
  l.setFont(juce::Font(juce::FontOptions().withHeight(fontSize)));
  l.setColour(juce::Label::textColourId, colour);
  l.setJustificationType(just);
}

NIMEReceiverEditor::NIMEReceiverEditor(NIMEReceiverProcessor &p)
    : AudioProcessorEditor(&p), processor(p) {
  setSize(520, 520);
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

  // Status dot (repurposed Label as a coloured dot)
  statusDot.setOpaque(false);
  addAndMakeVisible(statusDot);

  // Rate display
  styleLabel(rateValueLabel, "0", 52.f, Palette::textHi,
             juce::Justification::centredRight);
  addAndMakeVisible(rateValueLabel);

  styleLabel(rateUnitLabel, "msg / sec", 11.f, Palette::textMid,
             juce::Justification::centredLeft);
  addAndMakeVisible(rateUnitLabel);

  // Totals / IP
  styleLabel(totalLabel, "TOTAL", 9.f, Palette::textLo);
  styleLabel(totalValueLabel, "0", 12.f, Palette::textMid);
  styleLabel(ipLabel, "DEVICE IP", 9.f, Palette::textLo);
  styleLabel(ipValueLabel, "-", 12.f, Palette::textMid);
  addAndMakeVisible(totalLabel);
  addAndMakeVisible(totalValueLabel);
  addAndMakeVisible(ipLabel);
  addAndMakeVisible(ipValueLabel);

  // IMU labels - helper lambda
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

  addIMURow(gyroHeaderLabel, "GYROSCOPE  (°/s)", gxLabel, gyLabel, gzLabel,
            gxVal, gyVal, gzVal, "X", "Y", "Z");

  addIMURow(magHeaderLabel, "MAGNETOMETER  (µT)", mxLabel, myLabel, mzLabel,
            mxVal, myVal, mzVal, "X", "Y", "Z");

  // 60 Hz UI refresh timer
  startTimerHz(60);
}

NIMEReceiverEditor::~NIMEReceiverEditor() { stopTimer(); }

void NIMEReceiverEditor::paint(juce::Graphics &g) {
  // Background
  g.fillAll(Palette::bg);

  const auto w = getWidth();
  const auto h = getHeight();

  // Top divider
  g.setColour(Palette::border);
  g.drawHorizontalLine(36, 12.f, static_cast<float>(w - 12));

  // Rate panel outline
  juce::Rectangle<float> ratePanel(12.f, 44.f, static_cast<float>(w - 24),
                                   110.f);
  g.setColour(Palette::panel);
  g.fillRoundedRectangle(ratePanel, 6.f);
  g.setColour(Palette::border);
  g.drawRoundedRectangle(ratePanel, 6.f, 1.f);

  // IMU panel outline
  juce::Rectangle<float> imuPanel(12.f, 164.f, static_cast<float>(w - 24),
                                  static_cast<float>(h - 176));
  g.setColour(Palette::panel);
  g.fillRoundedRectangle(imuPanel, 6.f);
  g.setColour(Palette::border);
  g.drawRoundedRectangle(imuPanel, 6.f, 1.f);

  // Status dot
  const bool isReceivingData = processor.getMessagesPerSecond() > 0.f;
  g.setColour(isReceivingData ? Palette::green : Palette::red);
  g.fillEllipse(static_cast<float>(getWidth() - 28), 14.f, 8.f, 8.f);

  // Draw 3D Staff Simulation
  g.setColour(Palette::border);
  g.drawHorizontalLine(312, 24.f, static_cast<float>(w - 24));

  const auto &d = processor.getIMUData();
  Quat q{d.qw.load(), d.qx.load(), d.qy.load(), d.qz.load()};

  // Staff resting horizontally along the X-axis
  Vec3 top = {1.f, 0.f, 0.f};
  Vec3 bottom = {-1.f, 0.f, 0.f};

  top = rotate(top, q);
  bottom = rotate(bottom, q);

  const float scale = 90.f;
  const float cx = w / 2.f;
  const float cy = 410.f; // Centered in the empty space below IMU numbers

  // Orthographic 3D -> 2D projection (X -> Screen X, Z -> Screen Y inverted)
  float topX = cx + top.x * scale;
  float topY = cy - top.z * scale;
  float botX = cx + bottom.x * scale;
  float botY = cy - bottom.z * scale;

  // Draw the staff line
  g.setColour(Palette::textMid);
  g.drawLine(botX, botY, topX, topY, 8.f);

  // Draw colored tips to help orient
  g.setColour(Palette::accentDim);
  g.fillEllipse(botX - 6.f, botY - 6.f, 12.f, 12.f);
  g.setColour(Palette::red);
  g.fillEllipse(topX - 6.f, topY - 6.f, 12.f, 12.f);
}

void NIMEReceiverEditor::resized() {
  const int w = getWidth();

  // Title
  titleLabel.setBounds(14, 10, 220, 20);

  // Port + connect
  portLabel.setBounds(14, 50, 70, 12);
  portEditor.setBounds(14, 64, 70, 24);
  connectButton.setBounds(92, 64, 100, 24);

  // Rate display
  rateValueLabel.setBounds(w - 200, 50, 130, 60);
  rateUnitLabel.setBounds(w - 68, 88, 60, 18);

  // Totals / IP
  totalLabel.setBounds(14, 98, 60, 14);
  totalValueLabel.setBounds(14, 112, 100, 16);
  ipLabel.setBounds(130, 98, 80, 14);
  ipValueLabel.setBounds(130, 112, 200, 16);

  // IMU rows
  const int imuX = 20;
  const int imuY0 = 172;
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
}

void NIMEReceiverEditor::timerCallback() {
  refreshStats();
  repaint();
}

void NIMEReceiverEditor::refreshStats() {
  const float mps = processor.getMessagesPerSecond();

  // Rate label - colour changes with throughput
  rateValueLabel.setText(juce::String(static_cast<int>(mps)),
                         juce::dontSendNotification);
  rateValueLabel.setColour(juce::Label::textColourId, rateColour(mps));

  // Totals
  totalValueLabel.setText(juce::String(processor.getTotalMessageCount()),
                          juce::dontSendNotification);

  // IP
  const juce::String ip = processor.getLastConnectedIP();
  ipValueLabel.setText(ip.isEmpty() ? "-" : ip, juce::dontSendNotification);

  // IMU values
  const auto &d = processor.getIMUData();
  axVal.setText(fmt(d.ax.load()), juce::dontSendNotification);
  ayVal.setText(fmt(d.ay.load()), juce::dontSendNotification);
  azVal.setText(fmt(d.az.load()), juce::dontSendNotification);
  gxVal.setText(fmt(d.gx.load()), juce::dontSendNotification);
  gyVal.setText(fmt(d.gy.load()), juce::dontSendNotification);
  gzVal.setText(fmt(d.gz.load()), juce::dontSendNotification);
  mxVal.setText(fmt(d.mx.load()), juce::dontSendNotification);
  myVal.setText(fmt(d.my.load()), juce::dontSendNotification);
  mzVal.setText(fmt(d.mz.load()), juce::dontSendNotification);
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

juce::String NIMEReceiverEditor::fmt(float v, int decimals) {
  return juce::String(v, decimals);
}

juce::Colour NIMEReceiverEditor::rateColour(float mps) {
  if (mps <= 0.f)
    return Palette::textLo;
  if (mps < 50.f)
    return Palette::yellow;
  if (mps < 200.f)
    return Palette::accent;
  return Palette::textHi;
}