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
                    NodeTypeInfo::SinkWriteFn write) {
    NodeTypeInfo info;
    info.id = id;
    info.displayName = name;
    info.category = NodeCategory::Sink;
    info.numInputs = 1;
    info.monitorRangeMin = rangeMin;
    info.monitorRangeMax = rangeMax;
    info.sinkWrite = write;
    registry.registerType(std::move(info));
}

void addArraySink(NodeTypeRegistry& registry, const char* id, const char* name, float rangeMin, float rangeMax,
                   NodeTypeInfo::SinkWriteFn write) {
    NodeTypeInfo info;
    info.id = id;
    info.displayName = name;
    info.category = NodeCategory::Sink;
    info.numInputs = 1;
    info.defaultParams = { 0.0f }; // index
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
        [](const float* in, const std::vector<float>&, MappingOutput& out) { out.rootHz = in[0]; });

    addScalarSink(registry, "sink.numVoices", "Num Voices", 0.0f, 4.0f,
        [](const float* in, const std::vector<float>&, MappingOutput& out) {
            out.numVoices = juce::jlimit(0, 4, static_cast<int>(std::lround(in[0])));
        });

    addScalarSink(registry, "sink.masterGain", "Master Gain", 0.0f, 1.0f,
        [](const float* in, const std::vector<float>&, MappingOutput& out) { out.masterGain = in[0]; });

    addScalarSink(registry, "sink.driveAmt", "Drive Amount", 0.0f, 4.0f,
        [](const float* in, const std::vector<float>&, MappingOutput& out) { out.driveAmt = in[0]; });

    addScalarSink(registry, "sink.vibratoDepth", "Vibrato Depth", 0.0f, 1.0f,
        [](const float* in, const std::vector<float>&, MappingOutput& out) { out.vibratoDepth = in[0]; });

    addScalarSink(registry, "sink.vibratoRateHz", "Vibrato Rate (Hz)", 0.0f, 20.0f,
        [](const float* in, const std::vector<float>&, MappingOutput& out) { out.vibratoRateHz = in[0]; });

    addScalarSink(registry, "sink.tremoloDepth", "Tremolo Depth", 0.0f, 1.0f,
        [](const float* in, const std::vector<float>&, MappingOutput& out) { out.tremoloDepth = in[0]; });

    addScalarSink(registry, "sink.tremoloRateHz", "Tremolo Rate (Hz)", 0.0f, 20.0f,
        [](const float* in, const std::vector<float>&, MappingOutput& out) { out.tremoloRateHz = in[0]; });

    addScalarSink(registry, "sink.noiseAmount", "Noise Amount", 0.0f, 1.0f,
        [](const float* in, const std::vector<float>&, MappingOutput& out) { out.noiseAmount = in[0]; });

    addScalarSink(registry, "sink.noiseLpCoef", "Noise LP Coefficient", 0.0f, 1.0f,
        [](const float* in, const std::vector<float>&, MappingOutput& out) { out.noiseLpCoef = in[0]; });

    addScalarSink(registry, "sink.lpfCutoffHz", "LPF Cutoff (Hz)", 20.0f, 20000.0f,
        [](const float* in, const std::vector<float>&, MappingOutput& out) { out.lpfCutoffHz = in[0]; });

    addScalarSink(registry, "sink.useIndependentVoicePitch", "Independent Voice Pitch", 0.0f, 1.0f,
        [](const float* in, const std::vector<float>&, MappingOutput& out) { out.useIndependentVoicePitch = in[0] > 0.5f; });

    addScalarSink(registry, "sink.reverbWetLevel", "Reverb Wet Level", 0.0f, 1.0f,
        [](const float* in, const std::vector<float>&, MappingOutput& out) { out.reverbWetLevel = in[0]; });

    addScalarSink(registry, "sink.reverbRoomSize", "Reverb Room Size", 0.0f, 1.0f,
        [](const float* in, const std::vector<float>&, MappingOutput& out) { out.reverbRoomSize = in[0]; });

    addScalarSink(registry, "sink.reverbDamping", "Reverb Damping", 0.0f, 1.0f,
        [](const float* in, const std::vector<float>&, MappingOutput& out) { out.reverbDamping = in[0]; });

    addArraySink(registry, "sink.chordSemitone", "Chord Semitone[i]", -24.0f, 24.0f,
        [](const float* in, const std::vector<float>& p, MappingOutput& out) { out.chordSemitones[arrayIndex<3>(p)] = in[0]; });

    addArraySink(registry, "sink.voiceGain", "Voice Gain[i]", 0.0f, 1.0f,
        [](const float* in, const std::vector<float>& p, MappingOutput& out) { out.voiceGain[arrayIndex<4>(p)] = in[0]; });

    addArraySink(registry, "sink.partialAmp", "Partial Amp[i]", 0.0f, 1.0f,
        [](const float* in, const std::vector<float>& p, MappingOutput& out) { out.partialAmps[arrayIndex<6>(p)] = in[0]; });

    addArraySink(registry, "sink.panL", "Pan L[i]", 0.0f, 1.0f,
        [](const float* in, const std::vector<float>& p, MappingOutput& out) { out.panL[arrayIndex<4>(p)] = in[0]; });

    addArraySink(registry, "sink.panR", "Pan R[i]", 0.0f, 1.0f,
        [](const float* in, const std::vector<float>& p, MappingOutput& out) { out.panR[arrayIndex<4>(p)] = in[0]; });

    addArraySink(registry, "sink.voiceHz", "Voice Hz[i]", 20.0f, 2000.0f,
        [](const float* in, const std::vector<float>& p, MappingOutput& out) { out.voiceHz[arrayIndex<4>(p)] = in[0]; });
}

} // namespace Graph
