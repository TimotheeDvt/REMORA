#include "BoStaffSynth.h"
#include <cmath>
#include <algorithm>
#include "MathHelpers.h"

#include "Graph/GraphMappingStrategy.h"
#include "Graph/Presets/AllPresets.h"

float BoStaffSynth::semitoneRatio(float semitones) {
    return MathHelpers::semitoneRatio(semitones);
}

void BoStaffSynth::pushNextSampleIntoFifo(float sample) noexcept {
    if (fifoIndex == fftSize) {
        if (!nextFFTBlockReady.load()) {
            std::copy(fifo.begin(), fifo.end(), fftData.begin());
            nextFFTBlockReady.store(true);
        }
        fifoIndex = 0;
    }
    fifo[fifoIndex++] = sample;
}

BoStaffSynth::BoStaffSynth() {
    // Register mappings - each is a node graph (see Source/DSP/Graph/) built
    // by a Presets::build*() function, wrapped in the one generic
    // GraphMappingStrategy. These replace what used to be 11 distinct
    // hand-written IMappingStrategy subclasses under Mappings/ - see
    // Source/DSP/Graph/Presets/*.cpp for each preset's construction.
    using Graph::GraphMappingStrategy;
    mappings.push_back(std::make_unique<GraphMappingStrategy>(Graph::Presets::buildSimple(), "Simple (Pitch+Roll)"));
    mappings.push_back(std::make_unique<GraphMappingStrategy>(Graph::Presets::buildBowedChord(), "Bowed Chord"));
    mappings.push_back(std::make_unique<GraphMappingStrategy>(Graph::Presets::buildLeadDrone(), "Lead + Drone"));
    mappings.push_back(std::make_unique<GraphMappingStrategy>(Graph::Presets::buildSpinFilter(), "Spin Filter"));
    mappings.push_back(std::make_unique<GraphMappingStrategy>(Graph::Presets::buildBozendo(), "Bozendo"));
    mappings.push_back(std::make_unique<GraphMappingStrategy>(Graph::Presets::buildBozendo2(), "Bozendo 2"));
    mappings.push_back(std::make_unique<GraphMappingStrategy>(Graph::Presets::buildAzimut(), "Azimut"));
    mappings.push_back(std::make_unique<GraphMappingStrategy>(Graph::Presets::buildAzimutPlus(), "Azimut+"));
    mappings.push_back(std::make_unique<GraphMappingStrategy>(Graph::Presets::buildAzimutReverb(), "Azimut Reverb"));
    mappings.push_back(std::make_unique<GraphMappingStrategy>(Graph::Presets::buildBens(), "Ben's"));
    mappings.push_back(std::make_unique<GraphMappingStrategy>(Graph::Presets::buildSpinVoice(), "Spin Voices"));
}

BoStaffSynth::~BoStaffSynth() {}

void BoStaffSynth::prepareToPlay(double sampleRate, int samplesPerBlock) {
    currentSampleRate = static_cast<float>(sampleRate);
    sampleRateRecip   = 1.0f / currentSampleRate;

    for (auto& mapping : mappings) {
        mapping->prepare(sampleRate);
    }

    masterGain.reset(sampleRate, 0.010);
    masterGain.setCurrentAndTargetValue(0.0f);

    bowPressure.reset(sampleRate, 0.040);
    bowPressure.setCurrentAndTargetValue(0.0f);

    rootFreq.reset(sampleRate, 0.060);
    rootFreq.setCurrentAndTargetValue(110.0f);

    chordBlendSmoother.reset(sampleRate, 0.200);
    chordBlendSmoother.setCurrentAndTargetValue(1.0f);

    muteGain.reset(sampleRate, 0.020);
    muteGain.setCurrentAndTargetValue(isSoundEnabled() ? 1.0f : 0.0f);

    for (int v = 0; v < kNumVoices; ++v) {
        voices[v].prepare(sampleRate);
        chorusPhase[v] = static_cast<float>(v) * 1.57f;
    }

    noiseFilterState = 0.0f;

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = static_cast<uint32_t>(samplesPerBlock);
    spec.numChannels = 2; // Assuming stereo output

    masterLowPassFilter.prepare(spec);
    masterLowPassFilter.setType(juce::dsp::StateVariableTPTFilterType::lowpass);

    reverb.prepare(spec);
    reverb.reset();
    reverbWetBuffer.setSize(2, samplesPerBlock);

    reverbWetSmoothed.reset(sampleRate, 0.050);
    reverbWetSmoothed.setCurrentAndTargetValue(0.0f);
}

void BoStaffSynth::setSoundEnabled(bool b) { soundEnabled.store(b); }
bool BoStaffSynth::isSoundEnabled() const { return soundEnabled.load(); }

void BoStaffSynth::setMappingStrategy(int index) {
    activeMappingIndex.store(juce::jlimit(0, getMappingCount() - 1, index));
}

int BoStaffSynth::getMappingStrategy() const noexcept {
    return activeMappingIndex.load();
}

const char* BoStaffSynth::getMappingName(int index) const {
    if (index >= 0 && index < static_cast<int>(mappings.size()))
        return mappings[static_cast<size_t>(index)]->getName();
    return nullptr;
}

int BoStaffSynth::getMappingCount() const noexcept {
    return static_cast<int>(mappings.size());
}

const IMappingStrategy* BoStaffSynth::getMapping(int index) const noexcept {
    if (index >= 0 && index < static_cast<int>(mappings.size()))
        return mappings[static_cast<size_t>(index)].get();
    return nullptr;
}

void BoStaffSynth::processBlock(juce::AudioBuffer<float> &buffer,
                                const StaffSoundParams& params)
{
    buffer.clear();
    const int numSamples  = buffer.getNumSamples();
    if (buffer.getNumChannels() == 0) return;

    auto* left  = buffer.getWritePointer(0);
    auto* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;

    constexpr float twoPi = 6.28318530717958f;

    muteGain.setTargetValue(isSoundEnabled() ? 1.0f : 0.0f);

    if (!params.isReceivingValidData) {
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
        muteGain.skip(numSamples);
        reverbWetSmoothed.skip(numSamples);
        if (right) juce::FloatVectorOperations::copy(right, left, numSamples);
        return;
    }

    // Only AzimutReverbMapping sets this; reset it here so switching away
    // from it can't leave a stale wet level stuck on for every other mapping.
    mappingOut.reverbWetLevel = 0.0f;

    int activeIndex = activeMappingIndex.load();
    if (activeIndex >= 0 && activeIndex < static_cast<int>(mappings.size())) {
        mappings[static_cast<size_t>(activeIndex)]->process(params, mappingOut);
    }

    rootFreq.setTargetValue(mappingOut.rootHz);
    masterGain.setTargetValue(mappingOut.masterGain);

    masterLowPassFilter.setCutoffFrequency(std::clamp(mappingOut.lpfCutoffHz, 20.0f, 20000.0f));

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

            float voiceFreq;
            if (mappingOut.useIndependentVoicePitch) {
                voiceFreq = mappingOut.voiceHz[v] * vibratoMod * chorusMod;
            } else {
                float semi = (v == 0) ? 0.0f
                           : blendFromSemitones[v-1] + (mappingOut.chordSemitones[v-1] - blendFromSemitones[v-1]) * blend;
                voiceFreq = rootNow * semitoneRatio(semi) * chorusMod;
            }

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

    juce::dsp::AudioBlock<float> audioBlock(buffer);
    juce::dsp::ProcessContextReplacing<float> context(audioBlock);
    masterLowPassFilter.process(context);

    reverbWetSmoothed.setTargetValue(mappingOut.reverbWetLevel);
    {
        juce::dsp::Reverb::Parameters reverbParams;
        reverbParams.roomSize = mappingOut.reverbRoomSize;
        reverbParams.damping  = mappingOut.reverbDamping;
        reverbParams.wetLevel = 1.0f;
        reverbParams.dryLevel = 0.0f;
        reverbParams.width    = 1.0f;
        reverb.setParameters(reverbParams);

        reverbWetBuffer.setSize(buffer.getNumChannels(), numSamples, false, false, true);
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            reverbWetBuffer.copyFrom(ch, 0, buffer, ch, 0, numSamples);

        juce::dsp::AudioBlock<float> wetBlock(reverbWetBuffer);
        juce::dsp::ProcessContextReplacing<float> wetContext(wetBlock);
        reverb.process(wetContext);

        for (int i = 0; i < numSamples; ++i) {
            const float wetAmt = reverbWetSmoothed.getNextValue();
            left[i] = left[i] * (1.0f - wetAmt) + reverbWetBuffer.getSample(0, i) * wetAmt;
            if (right) right[i] = right[i] * (1.0f - wetAmt) + reverbWetBuffer.getSample(1, i) * wetAmt;
        }
    }

    float vol = uiGlobalVolume.load();
    for (int i = 0; i < numSamples; ++i) {
        left[i] *= vol;
        if (right) right[i] *= vol;
        pushNextSampleIntoFifo(left[i]);

        const float mute = muteGain.getNextValue();
        left[i] *= mute;
        if (right) right[i] *= mute;
    }
}