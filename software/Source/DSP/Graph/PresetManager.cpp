#include "PresetManager.h"

namespace Graph {

namespace {

juce::File defaultPresetFolder() {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("REMORA")
        .getChildFile("Presets");
}

juce::PropertiesFile::Options makePropertiesOptions() {
    juce::PropertiesFile::Options options;
    options.applicationName = "REMORA";
    options.filenameSuffix = "settings";
    options.folderName = "REMORA";
    options.osxLibrarySubFolder = "Application Support";
    return options;
}

} // namespace

PresetManager& PresetManager::instance() {
    static PresetManager instance_;
    return instance_;
}

PresetManager::PresetManager() : properties(makePropertiesOptions()) {}

juce::File PresetManager::getPresetFolder() const {
    const juce::String stored = properties.getValue("presetFolder");
    const juce::File folder = stored.isNotEmpty() ? juce::File(stored) : defaultPresetFolder();
    folder.createDirectory();
    return folder;
}

void PresetManager::setPresetFolder(const juce::File& folder) {
    folder.createDirectory();
    properties.setValue("presetFolder", folder.getFullPathName());
    properties.saveIfNeeded();
}

juce::Array<juce::File> PresetManager::listPresetFiles() const {
    auto files = getPresetFolder().findChildFiles(juce::File::findFiles, false, "*.xml");
    files.sort();
    return files;
}

juce::File PresetManager::filePathForName(const juce::String& presetName) const {
    return getPresetFolder().getChildFile(juce::File::createLegalFileName(presetName) + ".xml");
}

bool PresetManager::savePreset(const juce::File& file, const NodeGraph& graph) const {
    auto xml = graph.toXml();
    return xml != nullptr && xml->writeTo(file);
}

std::unique_ptr<NodeGraph> PresetManager::loadPreset(const juce::File& file) const {
    auto xml = juce::parseXML(file);
    if (xml == nullptr)
        return nullptr;
    return NodeGraph::fromXml(*xml);
}

} // namespace Graph
