#pragma once

#include "NodeGraph.h"
#include <JuceHeader.h>
#include <memory>

namespace Graph {

// Owns the user's preset folder setting (persisted via a JUCE PropertiesFile, the
// standard cross-session settings store) and reads/writes NodeGraph XML snapshots to it.
class PresetManager {
public:
    static PresetManager& instance();

    juce::File getPresetFolder() const;
    void setPresetFolder(const juce::File& folder);

    // .xml files directly inside the preset folder, sorted by name.
    juce::Array<juce::File> listPresetFiles() const;

    juce::File filePathForName(const juce::String& presetName) const;
    bool savePreset(const juce::File& file, const NodeGraph& graph) const;
    std::unique_ptr<NodeGraph> loadPreset(const juce::File& file) const;

    // Subfolder holding factory-preset XML snapshots regenerated from Source/DSP/Graph/Presets/*.cpp.
    juce::File getFactoryFolder() const;
    juce::File factoryFilePathForName(const juce::String& presetName) const;

    std::unique_ptr<NodeGraph> syncFactoryPreset(const juce::String& name, std::unique_ptr<NodeGraph> (*build)()) const;

private:
    PresetManager();

    juce::PropertiesFile properties;

    JUCE_DECLARE_NON_COPYABLE(PresetManager)
};

} // namespace Graph
