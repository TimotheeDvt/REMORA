#include "../IMappingStrategy.h"
#include "NodeTypeRegistry.h"
#include <cmath>

// Sink node write functions - one per MappingOutput field. Indexed-array
// sinks (voiceGain, partialAmp, panL/R, chordSemitone, voiceHz) share one
// node type per array with an `index` param, rather than one type per array
// element, to avoid a combinatorial explosion of node types.

namespace Graph {
namespace {

void addScalarSink(NodeTypeRegistry& registry, const char* id, const char* name, float rangeMin, float rangeMax,
                    const char* description, NodeTypeInfo::SinkWriteFn write) {
    NodeTypeInfo info;
    info.id = id;
    info.displayName = name;
    info.category = NodeCategory::Sink;
    info.subcategory = "Scalar";
    info.description = description;
    info.numInputs = 1;
    info.inputNames = { "value" };
    info.monitorRangeMin = rangeMin;
    info.monitorRangeMax = rangeMax;
    info.sinkWrite = write;
    registry.registerType(std::move(info));
}

void addArraySink(NodeTypeRegistry& registry, const char* id, const char* name, float rangeMin, float rangeMax,
                   const char* description, NodeTypeInfo::SinkWriteFn write) {
    NodeTypeInfo info;
    info.id = id;
    info.displayName = name;
    info.category = NodeCategory::Sink;
    info.subcategory = "Indexed Array";
    info.description = description;
    info.numInputs = 1;
    info.inputNames = { "value" };
    info.defaultParams = { 0.0f }; // index
    info.paramNames = { "index" };
    info.monitorRangeMin = rangeMin;
    info.monitorRangeMax = rangeMax;
    info.sinkWrite = write;
    registry.registerType(std::move(info));
}

template <int N>
int arrayIndex(const std::vector<float>& params) {
    const float raw = params.empty() ? 0.0f : params[0];
    return juce::jlimit(0, N - 1, static_cast<int>(std::lround(raw)));
}

} // namespace

void registerSinkNodes(NodeTypeRegistry& registry) {
    addScalarSink(registry, "sink.rootHz", "Root Hz", 20.0f, 2000.0f,
        "Sets the synth's root/fundamental frequency, in Hz. All voices and chord semitones are computed relative to this.",
        [](const float* in, const std::vector<float>&, MappingOutput& out) { out.rootHz = in[0]; });

    addScalarSink(registry, "sink.numVoices", "Num Voices", 0.0f, 4.0f,
        "How many of the 4 chord voices are active, 0-4. Rounded to the nearest whole number and clamped to [0, 4].",
        [](const float* in, const std::vector<float>&, MappingOutput& out) {
            out.numVoices = juce::jlimit(0, 4, static_cast<int>(std::lround(in[0])));
        });

    addScalarSink(registry, "sink.masterGain", "Master Gain", 0.0f, 1.0f,
        "Overall output level, 0.0 (silent) to 1.0 (full).",
        [](const float* in, const std::vector<float>&, MappingOutput& out) { out.masterGain = in[0]; });

    addScalarSink(registry, "sink.driveAmt", "Drive Amount", 0.0f, 4.0f,
        "Amount of waveshaping/saturation drive applied to the voices, 0.0 (clean) upward - values above ~1 push into audible distortion.",
        [](const float* in, const std::vector<float>&, MappingOutput& out) { out.driveAmt = in[0]; });

    addScalarSink(registry, "sink.vibratoDepth", "Vibrato Depth", 0.0f, 1.0f,
        "Depth of pitch vibrato, 0.0 (none) to 1.0 (max).",
        [](const float* in, const std::vector<float>&, MappingOutput& out) { out.vibratoDepth = in[0]; });

    addScalarSink(registry, "sink.vibratoRateHz", "Vibrato Rate (Hz)", 0.0f, 20.0f,
        "Speed of the pitch vibrato LFO, in Hz.",
        [](const float* in, const std::vector<float>&, MappingOutput& out) { out.vibratoRateHz = in[0]; });

    addScalarSink(registry, "sink.tremoloDepth", "Tremolo Depth", 0.0f, 1.0f,
        "Depth of amplitude tremolo, 0.0 (none) to 1.0 (max).",
        [](const float* in, const std::vector<float>&, MappingOutput& out) { out.tremoloDepth = in[0]; });

    addScalarSink(registry, "sink.tremoloRateHz", "Tremolo Rate (Hz)", 0.0f, 20.0f,
        "Speed of the amplitude tremolo LFO, in Hz.",
        [](const float* in, const std::vector<float>&, MappingOutput& out) { out.tremoloRateHz = in[0]; });

    addScalarSink(registry, "sink.noiseAmount", "Noise Amount", 0.0f, 1.0f,
        "Amount of broadband noise mixed into the voices, 0.0 (none) to 1.0 (max).",
        [](const float* in, const std::vector<float>&, MappingOutput& out) { out.noiseAmount = in[0]; });

    addScalarSink(registry, "sink.noiseLpCoef", "Noise LP Coefficient", 0.0f, 1.0f,
        "One-pole lowpass coefficient applied to the noise before mixing, 0.0 (darkest/most filtered) to 1.0 (brightest/unfiltered).",
        [](const float* in, const std::vector<float>&, MappingOutput& out) { out.noiseLpCoef = in[0]; });

    addScalarSink(registry, "sink.lpfCutoffHz", "LPF Cutoff (Hz)", 20.0f, 20000.0f,
        "Cutoff frequency of the output lowpass filter, in Hz.",
        [](const float* in, const std::vector<float>&, MappingOutput& out) { out.lpfCutoffHz = in[0]; });

    addScalarSink(registry, "sink.useIndependentVoicePitch", "Independent Voice Pitch", 0.0f, 1.0f,
        "Boolean switch: value > 0.5 makes each voice use its own Voice Hz[i] instead of Root Hz + chord semitones.",
        [](const float* in, const std::vector<float>&, MappingOutput& out) { out.useIndependentVoicePitch = in[0] > 0.5f; });

    addScalarSink(registry, "sink.reverbWetLevel", "Reverb Wet Level", 0.0f, 1.0f,
        "Reverb mix: 0.0 = fully dry/bypassed, 1.0 = fully wet.",
        [](const float* in, const std::vector<float>&, MappingOutput& out) { out.reverbWetLevel = in[0]; });

    addScalarSink(registry, "sink.reverbRoomSize", "Reverb Room Size", 0.0f, 1.0f,
        "Reverb decay length ('feedback'): 0.0 = short, 1.0 = long.",
        [](const float* in, const std::vector<float>&, MappingOutput& out) { out.reverbRoomSize = in[0]; });

    addScalarSink(registry, "sink.reverbDamping", "Reverb Damping", 0.0f, 1.0f,
        "Reverb high-frequency absorption: 0.0 = bright, 1.0 = dark.",
        [](const float* in, const std::vector<float>&, MappingOutput& out) { out.reverbDamping = in[0]; });

    addArraySink(registry, "sink.chordSemitone", "Chord Semitone[i]", -24.0f, 24.0f,
        "Semitone offset from Root Hz for chord voice 'index' (0-2), signed. index is rounded and clamped to [0, 2].",
        [](const float* in, const std::vector<float>& p, MappingOutput& out) { out.chordSemitones[arrayIndex<3>(p)] = in[0]; });

    addArraySink(registry, "sink.voiceGain", "Voice Gain[i]", 0.0f, 1.0f,
        "Per-voice gain for voice 'index' (0-3), 0.0 (silent) to 1.0 (full). index is rounded and clamped to [0, 3].",
        [](const float* in, const std::vector<float>& p, MappingOutput& out) { out.voiceGain[arrayIndex<4>(p)] = in[0]; });

    addArraySink(registry, "sink.partialAmp", "Partial Amp[i]", 0.0f, 1.0f,
        "Amplitude of harmonic partial 'index' (0-5) in the voice timbre, 0.0 (silent) to 1.0 (full). index is rounded and clamped to [0, 5].",
        [](const float* in, const std::vector<float>& p, MappingOutput& out) { out.partialAmps[arrayIndex<6>(p)] = in[0]; });

    addArraySink(registry, "sink.panL", "Pan L[i]", 0.0f, 1.0f,
        "Left-channel gain for voice 'index' (0-3), 0.0 (silent) to 1.0 (full). index is rounded and clamped to [0, 3].",
        [](const float* in, const std::vector<float>& p, MappingOutput& out) { out.panL[arrayIndex<4>(p)] = in[0]; });

    addArraySink(registry, "sink.panR", "Pan R[i]", 0.0f, 1.0f,
        "Right-channel gain for voice 'index' (0-3), 0.0 (silent) to 1.0 (full). index is rounded and clamped to [0, 3].",
        [](const float* in, const std::vector<float>& p, MappingOutput& out) { out.panR[arrayIndex<4>(p)] = in[0]; });

    addArraySink(registry, "sink.voiceHz", "Voice Hz[i]", 20.0f, 2000.0f,
        "Independent frequency in Hz for voice 'index' (0-3). Only used when Independent Voice Pitch is on. index is rounded and clamped to [0, 3].",
        [](const float* in, const std::vector<float>& p, MappingOutput& out) { out.voiceHz[arrayIndex<4>(p)] = in[0]; });
}

} // namespace Graph
