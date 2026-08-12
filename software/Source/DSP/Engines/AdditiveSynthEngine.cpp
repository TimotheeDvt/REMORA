#include "AdditiveSynthEngine.h"
#include <cmath>

float AdditiveSynthEngine::semitoneRatio(float semitones) {
    return MathHelpers::semitoneRatio(semitones);
}

void AdditiveSynthEngine::prepare(double sampleRate, int samplesPerBlock) {
    currentSampleRate = static_cast<float>(sampleRate);
    sampleRateRecip   = 1.0f / currentSampleRate;

    rootFreq.reset(sampleRate, 0.060);
    rootFreq.setCurrentAndTargetValue(110.0f);

    chordBlendSmoother.reset(sampleRate, 0.200);
    chordBlendSmoother.setCurrentAndTargetValue(1.0f);

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

void AdditiveSynthEngine::render(juce::AudioBuffer<float>& buffer, int numSamples) {
    if (buffer.getNumChannels() == 0)
        return;

    auto* left  = buffer.getWritePointer(0);
    auto* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;

    constexpr float twoPi = 6.28318530717958f;

    rootFreq.setTargetValue(params.rootHz);
    masterLowPassFilter.setCutoffFrequency(std::clamp(params.lpfCutoffHz, 20.0f, 20000.0f));

    float vibratoDepth  = params.vibratoDepth;
    float vibratoRate   = params.vibratoRateHz;
    float tremoloDepth  = params.tremoloDepth;
    float tremoloRate   = params.tremoloRateHz;
    float driveAmt      = params.driveAmt;
    float noiseAmt      = params.noiseAmount;
    float noiseCoef     = params.noiseLpCoef;
    int   numVoices     = params.numVoices;

    bool chordChanged = false;
    for (int v = 0; v < 3; ++v) {
        if (std::abs(params.chordSemitones[v] - prevChordSemitones[v]) > 0.01f) {
            chordChanged = true;
            break;
        }
    }

    if (chordChanged) {
        for (int v = 0; v < 3; ++v) blendFromSemitones[v] = prevChordSemitones[v];
        for (int v = 0; v < 3; ++v) prevChordSemitones[v] = params.chordSemitones[v];
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

        const float tremoloNow = tremoloMod;
        const float rootNow    = rootFreq.getNextValue() * vibratoMod;

        float mixL = 0.f, mixR = 0.f;

        for (int v = 0; v < kNumVoices; ++v)
        {
            chorusPhase[v] += twoPi * kChorusRate * sampleRateRecip;
            if (chorusPhase[v] >= twoPi) chorusPhase[v] -= twoPi;
            float chorusMod = 1.0f + kChorusDepth * std::sin(chorusPhase[v]);

            float voiceFreq;
            if (params.useIndependentVoicePitch) {
                voiceFreq = params.voiceHz[v] * vibratoMod * chorusMod;
            } else {
                float semi = (v == 0) ? 0.0f
                           : blendFromSemitones[v-1] + (params.chordSemitones[v-1] - blendFromSemitones[v-1]) * blend;
                voiceFreq = rootNow * semitoneRatio(semi) * chorusMod;
            }

            voices[v].voiceAmp.setTargetValue((v < numVoices) ? params.voiceGain[v] : 0.0f);
            float voiceGain = voices[v].voiceAmp.getNextValue();

            float voiceSample = voices[v].tick(voiceFreq, sampleRateRecip,
                                               params.partialAmps, noiseOut * 0.3f,
                                               rngState, driveAmt, params.usePinkNoise);
            voiceSample *= voiceGain;

            mixL += voiceSample * params.panL[v];
            mixR += voiceSample * params.panR[v];
        }

        left[i]  = mixL * tremoloNow;
        if (right) right[i] = mixR * tremoloNow;
    }

    juce::dsp::AudioBlock<float> audioBlock(buffer);
    juce::dsp::ProcessContextReplacing<float> context(audioBlock);
    masterLowPassFilter.process(context);

    reverbWetSmoothed.setTargetValue(params.reverbWetLevel);
    {
        juce::dsp::Reverb::Parameters reverbParams;
        reverbParams.roomSize = params.reverbRoomSize;
        reverbParams.damping  = params.reverbDamping;
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
}
