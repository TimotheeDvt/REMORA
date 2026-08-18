#include "SynthManager.h"
#include <algorithm>
#include <cmath>

#include "Graph/GraphMappingStrategy.h"
#include "Graph/Presets/AllPresets.h"
#include "Graph/PresetManager.h"

void SynthManager::pushNextSampleIntoFifo(float sample) noexcept {
    if (fifoIndex == fftSize) {
        if (!nextFFTBlockReady.load()) {
            std::copy(fifo.begin(), fifo.end(), fftData.begin());
            nextFFTBlockReady.store(true);
        }
        fifoIndex = 0;
    }
    fifo[fifoIndex++] = sample;
}

SynthManager::SynthManager() {
    auto additive = std::make_unique<AdditiveSynthEngine>();
    additiveEngine = additive.get();
    engines.push_back(std::move(additive));

    auto granular = std::make_unique<GranularSynthEngine>();
    granularEngine = granular.get();
    engines.push_back(std::move(granular));

    auto pinkTrombone = std::make_unique<PinkTromboneEngine>();
    pinkTromboneEngine = pinkTrombone.get();
    engines.push_back(std::move(pinkTrombone));

    engineScratch.resize(engines.size());

    // Each mapping is a Presets::build*() node graph wrapped in the one generic GraphMappingStrategy.
    using Graph::GraphMappingStrategy;
    auto& presets = Graph::PresetManager::instance();
    mappings.push_back(std::make_unique<GraphMappingStrategy>(
        presets.syncFactoryPreset("Simple (Pitch+Roll)", Graph::Presets::buildSimple), "Simple (Pitch+Roll)",
        "Pitch angle sets the root frequency; roll amount sets the volume. A single sine voice with no chord "
        "or filtering - the most direct staff-to-pitch mapping."));
    mappings.push_back(std::make_unique<GraphMappingStrategy>(
        presets.syncFactoryPreset("Wind Noise", Graph::Presets::buildWindNoise), "Wind Noise",
        "No pitch, no chord - just filtered noise. Staff motion (gyro+accel energy) swells and dies down like "
        "gusts of wind, opening the noise filter and the master lowpass brighter the faster you move."));
    mappings.push_back(std::make_unique<GraphMappingStrategy>(
        presets.syncFactoryPreset("Bowed Chord", Graph::Presets::buildBowedChord), "Bowed Chord",
        "Pitch angle is quantized to a chromatic scale for the root note; yaw and roll select the chord voicing. "
        "Gyro speed acts like bow pressure, driving volume, drive and brightness, and sharp jabs add a noise strike."));
    mappings.push_back(std::make_unique<GraphMappingStrategy>(
        presets.syncFactoryPreset("Lead + Drone", Graph::Presets::buildLeadDrone), "Lead + Drone",
        "Pitch angle is quantized to a major scale for the root note, shared by a 4-voice drone chord. Motion "
        "intensity (gyro/accel magnitude) sets the volume and swells the drone, while yaw crossfades two drone voices."));
    // mappings.push_back(std::make_unique<GraphMappingStrategy>(Graph::Presets::buildSpinFilter(), "Spin Filter",
    //     "Spin speed (raw gyro magnitude) sets the root note on a pentatonic scale - faster spins climb higher. "
    //     "Overall motion energy sets the volume, while roll sweeps a harmonic low-pass filter across the tone."));
    // mappings.push_back(std::make_unique<GraphMappingStrategy>(Graph::Presets::buildMartialEffort(), "Martial Effort",
    //     "Spin speed sets the root note on a pentatonic minor scale, shifted by the spin's plane (vertical/horizontal) "
    //     "and direction, which also picks the chord quality. Volume blends motion, Laban 'Weight' (force) and thrust jabs."));
    mappings.push_back(std::make_unique<GraphMappingStrategy>(
        presets.syncFactoryPreset("Martial Momentum", Graph::Presets::buildMartialMomentum), "Martial Momentum",
        "Root note is chosen from four fixed pitches by spin plane and direction, gliding to the new note at a "
        "speed set by spin momentum. Volume is gated by movement plus Laban 'Weight' and thrust jabs."));
    // mappings.push_back(std::make_unique<GraphMappingStrategy>(Graph::Presets::buildAzimut(), "Azimut",
    //     "Root note is picked from spin plane, direction, and whether the staff faces north or south, gliding to "
    //     "its target at a spin-speed-driven rate. Volume comes from motion, Laban 'Weight' and thrust jabs."));
    // mappings.push_back(std::make_unique<GraphMappingStrategy>(Graph::Presets::buildAzimutPlus(), "Azimut+",
    //     "Same pitch/volume mapping as Azimut (root from spin plane/direction/facing, volume from motion and "
    //     "Laban 'Weight'), but the filter cutoff tracks rotation speed directly instead of cycling with spin count."));
    // mappings.push_back(std::make_unique<GraphMappingStrategy>(Graph::Presets::buildAzimutReverb(), "Azimut Reverb",
    //     "Same pitch/volume mapping as Azimut. Free, fluid motion (Laban 'Flow Free') opens up a longer, brighter "
    //     "reverb tail; bound, controlled motion collapses it back toward dry."));
    mappings.push_back(std::make_unique<GraphMappingStrategy>(
        presets.syncFactoryPreset("Azimut Kinetic", Graph::Presets::buildAzimutKinetic), "Azimut Kinetic",
        "Same pitch/volume mapping as Azimut, but timbre is one-to-many: a single rotation-speed value fans out "
        "to the filter cutoff, harmonic brightness, drive, noise, vibrato/tremolo depth and reverb all at once - "
        "still and calm sounds like a pure sine, fast spins turn buzzy, driven, noisy and drenched in reverb."));
    mappings.push_back(std::make_unique<GraphMappingStrategy>(
        presets.syncFactoryPreset("Speed Gate", Graph::Presets::buildSpeedGate), "Speed Gate",
        "Below a speed threshold, pitch angle drives a simple pentatonic melody with roll controlling volume; "
        "above it, staff speed crossfades smoothly into the full Azimut mapping, with no clicks at the transition."));
    // mappings.push_back(std::make_unique<GraphMappingStrategy>(Graph::Presets::buildSpinVoice(), "Spin Voices",
    //     "Roll angle selects which of 4 independent voices is currently 'live'; the active voice's pitch glides "
    //     "with pitch angle and its volume follows Laban 'Weight', while the other voices hold their last pitch/gain."));
    mappings.push_back(std::make_unique<GraphMappingStrategy>(
        presets.syncFactoryPreset("Spin Voices (Scale)", Graph::Presets::buildSpinVoiceScale), "Spin Voices (Scale)",
        "Same 4-voice roll-select/pitch-glide mapping as Spin Voices, but every voice's pitch snaps to the "
        "nearest note of a major scale, and the graph displays every voice's gain and scale degree (not Hz), "
        "not just the selected voice's."));
    mappings.push_back(std::make_unique<GraphMappingStrategy>(
        presets.syncFactoryPreset("Vocal Tract", Graph::Presets::buildVocalTract), "Vocal Tract",
        "A physically-modeled voice (glottal source into a digital-waveguide vocal tract) instead of the "
        "additive engine. Pitch angle sets the glottal pitch; roll and yaw shape the vowel (tongue height "
        "and front-back position); motion energy and Laban 'Weight' drive volume and vocal tenseness; sharp "
        "thrust jabs pinch the tract near the lips for consonant-like bursts."));
}

SynthManager::~SynthManager() {}

void SynthManager::prepareToPlay(double sampleRate, int samplesPerBlock) {
    currentSampleRate = static_cast<float>(sampleRate);

    {
        const juce::ScopedLock sl(mappingsLock);
        for (auto& mapping : mappings) {
            mapping->prepare(sampleRate);
        }
    }

    for (auto& engine : engines)
        engine->prepare(sampleRate, samplesPerBlock);

    masterGain.reset(sampleRate, 0.010);
    masterGain.setCurrentAndTargetValue(0.0f);
    wasReceivingValidData = true;

    muteGain.reset(sampleRate, 0.020);
    muteGain.setCurrentAndTargetValue(isSoundEnabled() ? 1.0f : 0.0f);
}

void SynthManager::setSoundEnabled(bool b) { soundEnabled.store(b); }
bool SynthManager::isSoundEnabled() const { return soundEnabled.load(); }

void SynthManager::setMappingStrategy(int index) {
    activeMappingIndex.store(juce::jlimit(0, getMappingCount() - 1, index));
}

int SynthManager::getMappingStrategy() const noexcept {
    return activeMappingIndex.load();
}

const char* SynthManager::getMappingName(int index) const {
    const juce::ScopedLock sl(mappingsLock);
    if (index >= 0 && index < static_cast<int>(mappings.size()))
        return mappings[static_cast<size_t>(index)]->getName();
    return nullptr;
}

int SynthManager::getMappingCount() const noexcept {
    const juce::ScopedLock sl(mappingsLock);
    return static_cast<int>(mappings.size());
}

IMappingStrategy* SynthManager::getMapping(int index) const noexcept {
    const juce::ScopedLock sl(mappingsLock);
    if (index >= 0 && index < static_cast<int>(mappings.size()))
        return mappings[static_cast<size_t>(index)].get();
    return nullptr;
}

int SynthManager::addGraphMapping(const juce::String& name, std::unique_ptr<Graph::NodeGraph> graph) {
    auto mapping = std::make_unique<Graph::GraphMappingStrategy>(std::move(graph), name);
    mapping->prepare(currentSampleRate);
    const juce::ScopedLock sl(mappingsLock);
    mappings.push_back(std::move(mapping));
    return static_cast<int>(mappings.size()) - 1;
}

void SynthManager::processBlock(juce::AudioBuffer<float> &buffer,
                                const StaffSoundParams& params)
{
    buffer.clear();
    const int numSamples = buffer.getNumSamples();
    if (buffer.getNumChannels() == 0) return;

    muteGain.setTargetValue(isSoundEnabled() ? 1.0f : 0.0f);

    constexpr float kNormalGainRampSeconds = 0.010f;
    constexpr float kDropoutFadeSeconds = 0.120f;

    if (!params.isReceivingValidData) {
        if (wasReceivingValidData) {
            masterGain.reset(currentSampleRate, kDropoutFadeSeconds);
            wasReceivingValidData = false;
        }
        masterGain.setTargetValue(0.0f);
    } else {
        if (!wasReceivingValidData) {
            masterGain.reset(currentSampleRate, kNormalGainRampSeconds);
            wasReceivingValidData = true;
        }

        const int activeIndex = activeMappingIndex.load();
        bool updated = false;
        {
            const juce::ScopedLock sl(mappingsLock);
            if (activeIndex >= 0 && activeIndex < static_cast<int>(mappings.size()))
                updated = mappings[static_cast<size_t>(activeIndex)]->process(params, mappingOut);
        }

        if (updated)
            masterGain.setTargetValue(mappingOut.masterGain);
    }

    const bool engineActive[] = { mappingOut.additiveActive, mappingOut.granularActive, mappingOut.pinkTromboneActive };

    if (engineActive[0]) additiveEngine->setParams(mappingOut.additive);
    if (engineActive[1]) granularEngine->setParams(mappingOut.granular);
    if (engineActive[2]) pinkTromboneEngine->setParams(mappingOut.pinkTrombone);

    for (size_t e = 0; e < engines.size(); ++e) {
        if (!engineActive[e])
            continue;
        auto& scratch = engineScratch[e];
        scratch.setSize(buffer.getNumChannels(), numSamples, false, false, true);
        scratch.clear();
        engines[e]->render(scratch, numSamples);
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            buffer.addFrom(ch, 0, scratch, ch, 0, numSamples);
    }

    auto* left  = buffer.getWritePointer(0);
    auto* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;
    const float vol = uiGlobalVolume.load();

    for (int i = 0; i < numSamples; ++i) {
        const float gain = masterGain.getNextValue() * vol;
        left[i] *= gain;
        if (right) right[i] *= gain;

        pushNextSampleIntoFifo(left[i]);

        const float mute = muteGain.getNextValue();
        left[i] *= mute;
        if (right) right[i] *= mute;
    }
}
