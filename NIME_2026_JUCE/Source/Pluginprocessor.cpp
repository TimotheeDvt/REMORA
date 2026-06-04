#include "PluginProcessor.h"
#include "PluginEditor.h"

NIMEReceiverProcessor::NIMEReceiverProcessor()
    : AudioProcessor(BusesProperties()) {
  startTimer(1000);

  // Automatically attempt to connect to port 8000 on startup
  startOSCReceiver(8000);
}

NIMEReceiverProcessor::~NIMEReceiverProcessor() {
  stopTimer();
}

// Timer - fires every 1000ms on the message thread
void NIMEReceiverProcessor::timerCallback() {
  oscManager.updateMessagesPerSecond();
}

juce::AudioProcessorEditor *NIMEReceiverProcessor::createEditor() {
  return new NIMEReceiverEditor(*this);
}

juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter() {
  return new NIMEReceiverProcessor();
}