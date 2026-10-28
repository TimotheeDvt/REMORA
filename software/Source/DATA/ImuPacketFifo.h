#pragma once

#include "IMUData.h"
#include <JuceHeader.h>
#include <array>

// Single-producer (OSC network thread) / single-consumer (audio thread) queue of sensor packets, so
// the audio thread sees every packet exactly once, in order, instead of re-reading the latest one
// on every block. Wait-free on both sides; when full, new packets are dropped and counted.
class ImuPacketFifo {
public:
  static constexpr int kCapacity = 128;

  bool push(const IMURawSnapshot &packet) {
    int start1, size1, start2, size2;
    fifo.prepareToWrite(1, start1, size1, start2, size2);
    if (size1 + size2 < 1) {
      overflowCount.fetch_add(1, std::memory_order_relaxed);
      return false;
    }
    buffer[static_cast<size_t>(size1 > 0 ? start1 : start2)] = packet;
    fifo.finishedWrite(1);
    return true;
  }

  // Copies up to maxPackets queued packets, oldest first, and returns how many were copied.
  int pop(IMURawSnapshot *dest, int maxPackets) {
    int start1, size1, start2, size2;
    fifo.prepareToRead(maxPackets, start1, size1, start2, size2);
    for (int i = 0; i < size1; ++i) dest[i] = buffer[static_cast<size_t>(start1 + i)];
    for (int i = 0; i < size2; ++i) dest[size1 + i] = buffer[static_cast<size_t>(start2 + i)];
    fifo.finishedRead(size1 + size2);
    return size1 + size2;
  }

  int getOverflowCount() const noexcept { return overflowCount.load(std::memory_order_relaxed); }

private:
  juce::AbstractFifo fifo{kCapacity};
  std::array<IMURawSnapshot, kCapacity> buffer{};
  std::atomic<int> overflowCount{0};
};
