#include "BoStaffSynth.h"
#include <cmath>
#include <algorithm>

float BoStaffSynth::semitoneRatio(float semitones) {
    return std::pow(2.0f, semitones / 12.0f);
}

BoStaffSynth::BoStaffSynth() {}
BoStaffSynth::~BoStaffSynth() {}

void BoStaffSynth::prepareToPlay(double sampleRate, int) {
    currentSampleRate = static_cast<float>(sampleRate);
    sampleRateRecip   = 1.0f / currentSampleRate;

    mappingSimple.prepare(sampleRate);
    mappingBowedChord.prepare(sampleRate);
    mappingLeadDrone.prepare(sampleRate);

    masterGain.reset(sampleRate, 0.010);
    masterGain.setCurrentAndTargetValue(0.0f);

    bowPressure.reset(sampleRate, 0.040);
    bowPressure.setCurrentAndTargetValue(0.0f);

    rootFreq.reset(sampleRate, 0.150);
    rootFreq.setCurrentAndTargetValue(110.0f);

    chordBlendSmoother.reset(sampleRate, 0.200);
    chordBlendSmoother.setCurrentAndTargetValue(1.0f);

    for (int v = 0; v < kNumVoices; ++v) {
        voices[v].prepare(sampleRate);
        chorusPhase[v] = static_cast<float>(v) * 1.57f;
    }

    noiseFilterState = 0.0f;
}

void BoStaffSynth::setSoundEnabled(bool b) { soundEnabled.store(b); }
bool BoStaffSynth::isSoundEnabled() const { return soundEnabled.load(); }

void BoStaffSynth::setMappingStrategy(int index) {
    activeMappingIndex.store(juce::jlimit(0, getMappingCount() - 1, index));
}

int BoStaffSynth::getMappingStrategy() const noexcept {
    return activeMappingIndex.load();
}

const char* BoStaffSynth::getMappingName(int index) {
    switch (index) {
        case 0: return SimpleMapping{}.getName();
        case 1: return BowedChordMapping{}.getName();
        case 2: return LeadDroneMapping{}.getName();
        default: return nullptr;
    }
}

int BoStaffSynth::getMappingCount() { return 3; }

void BoStaffSynth::processBlock(juce::AudioBuffer<float> &buffer,
                                const StaffSoundParams& params)
{
    buffer.clear();
    const int numSamples  = buffer.getNumSamples();
    if (buffer.getNumChannels() == 0) return;

    auto* left  = buffer.getWritePointer(0);
    auto* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;

    constexpr float twoPi = 6.28318530717958f;

    if (!params.isReceivingValidData || !isSoundEnabled()) {
        masterGain.setTargetValue(0.0f);
        bowPressure.setTargetValue(0.0f);
        for (int v = 0; v < kNumVoices; ++v)
            voices[v].voiceAmp.setTargetValue(0.0f);
        for (int i = 0; i < numSamples; ++i) {
            masterGain.getNextValue();
            bowPressure.getNextValue();
            rootFreq.getNextValue();
            for (int v = 0; v < kNumVoices; ++v) {
                voices[v].voiceAmp.getNextValue();
                for (int p = 0; p < SynthVoice::kPartials; ++p)
                    voices[v].partialAmp[p].getNextValue();
            }
            left[i] = 0.f;
        }
        if (right) juce::FloatVectorOperations::copy(right, left, numSamples);
        return;
    }

    int activeIndex = activeMappingIndex.load();
    IMappingStrategy* mapping = (activeIndex == 0) ? static_cast<IMappingStrategy*>(&mappingSimple)
                              : (activeIndex == 1) ? static_cast<IMappingStrategy*>(&mappingBowedChord)
                                                   : static_cast<IMappingStrategy*>(&mappingLeadDrone);

    mapping->process(params, mappingOut);

    rootFreq.setTargetValue(mappingOut.rootHz);
    masterGain.setTargetValue(mappingOut.masterGain);

    float vibratoDepth  = mappingOut.vibratoDepth;
    float vibratoRate   = mappingOut.vibratoRateHz;
    float tremoloDepth  = mappingOut.tremoloDepth;
    float tremoloRate   = mappingOut.tremoloRateHz;
    float driveAmt      = mappingOut.driveAmt;
    float noiseAmt      = mappingOut.noiseAmount;
    float noiseCoef     = mappingOut.noiseLpCoef;
    int   numVoices     = mappingOut.numVoices;

    bool chordChanged = false;
    for (int v = 0; v < 3; ++v) {
        if (std::abs(mappingOut.chordSemitones[v] - prevChordSemitones[v]) > 0.01f) {
            chordChanged = true;
            break;
        }
    }

    if (chordChanged) {
        for (int v = 0; v < 3; ++v) blendFromSemitones[v] = prevChordSemitones[v];
        for (int v = 0; v < 3; ++v) prevChordSemitones[v] = mappingOut.chordSemitones[v];
        chordBlendSmoother.setCurrentAndTargetValue(0.0f);
        chordBlendSmoother.setTargetValue(1.0f);
    }

    constexpr float kChorusDepth = 0.003f;
    constexpr float kChorusRate  = 0.3f;

    for (int i = 0; i < numSamples; ++i)
    {
        vibratoPhase += twoPi * vibratoRate * sampleRateRecip;
        if (vibratoPhase >= twoPi) vibratoPhase -= twoPi;

        tremoloPhase += twoPi * tremoloRate * sampleRateRecip;
        if (tremoloPhase >= twoPi) tremoloPhase -= twoPi;

        float blend = chordBlendSmoother.getNextValue();

        float vibratoMod = 1.0f + vibratoDepth * std::sin(vibratoPhase);

        float tremoloMod = 1.0f - tremoloDepth * (0.5f + 0.5f * std::sin(tremoloPhase));

        float noiseFilterIn = [&]() -> float {
            rngState ^= rngState << 13;
            rngState ^= rngState >> 17;
            rngState ^= rngState << 5;
            return static_cast<float>(rngState) * (2.0f / 4294967296.0f) - 1.0f;
        }();
        noiseFilterState = noiseFilterState * noiseCoef + noiseFilterIn * (1.0f - noiseCoef);
        float noiseOut = noiseFilterState * noiseAmt;

        const float masterNow = masterGain.getNextValue() * tremoloMod;
        const float rootNow   = rootFreq.getNextValue() * vibratoMod;

        float mixL = 0.f, mixR = 0.f;

        for (int v = 0; v < kNumVoices; ++v)
        {
            chorusPhase[v] += twoPi * kChorusRate * sampleRateRecip;
            if (chorusPhase[v] >= twoPi) chorusPhase[v] -= twoPi;
            float chorusMod = 1.0f + kChorusDepth * std::sin(chorusPhase[v]);

            float semi = (v == 0) ? 0.0f
                       : blendFromSemitones[v-1] + (mappingOut.chordSemitones[v-1] - blendFromSemitones[v-1]) * blend;
            float voiceFreq = rootNow * semitoneRatio(semi) * chorusMod;

            voices[v].voiceAmp.setTargetValue((v < numVoices) ? mappingOut.voiceGain[v] : 0.0f);
            float voiceGain = voices[v].voiceAmp.getNextValue();

            float voiceSample = voices[v].tick(voiceFreq, sampleRateRecip,
                                               mappingOut.partialAmps, noiseOut * 0.3f,
                                               rngState, driveAmt);
            voiceSample *= voiceGain;

            mixL += voiceSample * mappingOut.panL[v];
            mixR += voiceSample * mappingOut.panR[v];
        }

        left[i]  = mixL * masterNow;
        if (right) right[i] = mixR * masterNow;
    }
}