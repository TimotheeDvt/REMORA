#include "MappingSelectorBar.h"
#include "../DSP/Graph/GraphMappingStrategy.h"
#include "../DSP/Graph/PresetManager.h"
#include "../PluginProcessor.h"
#include "DebugLog.h"
#include "Palette.h"
#include "StyleHelpers.h"

MappingSelectorBar::MappingSelectorBar(REMORAProcessor& p) : processor(p) {
    mappingCombo.setColour(juce::ComboBox::backgroundColourId, Palette::panel);
    mappingCombo.setColour(juce::ComboBox::textColourId, Palette::textHi);
    mappingCombo.setColour(juce::ComboBox::outlineColourId, Palette::border);
    mappingCombo.setColour(juce::ComboBox::arrowColourId, Palette::textMid);

    styleLabel(descriptionLabel, {}, 12.0f, Palette::textLo, juce::Justification::topLeft);
    addAndMakeVisible(descriptionLabel);

    refreshMappingCombo(processor.getMappingStrategy());
    mappingCombo.onChange = [this] {
        const int newStrategyIndex = mappingCombo.getSelectedId() - 1;
        processor.setMappingStrategy(newStrategyIndex);
        // debug.print.cyan("Mapping strategy changed to:", processor.getSynth().getMappingName(newStrategyIndex));
        updateMappingInfo();
        notifyMappingChanged();
    };
    addAndMakeVisible(mappingCombo);

    styleButton(prevMapButton, "<", Palette::ButtonTheme::secondary,
               [this] {
                   const int count = processor.getSynth().getMappingCount();
                   if (count <= 0)
                       return;
                   selectMapping((mappingCombo.getSelectedId() - 2 + count) % count);
               }
    );
    addAndMakeVisible(prevMapButton);

    styleButton(nextMapButton, ">", Palette::ButtonTheme::secondary,
               [this] {
                   const int count = processor.getSynth().getMappingCount();
                   if (count <= 0)
                       return;
                   selectMapping(mappingCombo.getSelectedId() % count);
               }
    );
    addAndMakeVisible(nextMapButton);

    styleButton(newPresetButton, "New", Palette::ButtonTheme::secondary, [this] { createNewPreset(); });
    addAndMakeVisible(newPresetButton);

    styleButton(savePresetButton, "Save", Palette::ButtonTheme::secondary, [this] { saveCurrentPreset(); });
    addAndMakeVisible(savePresetButton);

    styleButton(loadPresetButton, "Load", Palette::ButtonTheme::secondary, [this] { showLoadPresetMenu(); });
    addAndMakeVisible(loadPresetButton);

    styleButton(optionsButton, "Options", Palette::ButtonTheme::secondary, [this] { showOptionsMenu(); });
    addAndMakeVisible(optionsButton);

    globalVolumeSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    globalVolumeSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);
    globalVolumeSlider.setColour(juce::Slider::textBoxTextColourId, Palette::textHi);
    globalVolumeSlider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    globalVolumeSlider.setRange(0.0, 2.0, 0.01);
    globalVolumeSlider.textFromValueFunction = [](double val) {
        return juce::String(juce::roundToInt(val * 100.0)) + "%";
    };
    globalVolumeSlider.valueFromTextFunction = [](const juce::String& text) {
        return text.upToFirstOccurrenceOf("%", false, false).getDoubleValue() / 100.0;
    };
    globalVolumeSlider.setValue(processor.getSynth().uiGlobalVolume.load());
    globalVolumeSlider.onValueChange = [this] {
        processor.getSynth().uiGlobalVolume.store(static_cast<float>(globalVolumeSlider.getValue()));
    };
    addAndMakeVisible(globalVolumeSlider);

    styleLabel(globalVolumeLabel, "Global Volume", 14.f, Palette::textMid, juce::Justification::centredRight);
    addAndMakeVisible(globalVolumeLabel);

    styleButton(resetButton, "Reset Changes", Palette::ButtonTheme::warning, [this] {
        if (onResetRequested)
            onResetRequested();
    });
    resetButton.setVisible(false);
    addAndMakeVisible(resetButton);

    styleButton(layoutButton, "Auto Layout", Palette::ButtonTheme::secondary, [this] {
        if (onLayoutRequested)
            onLayoutRequested();
    });
    addAndMakeVisible(layoutButton);

    auto setupSepSlider = [this](juce::Slider& slider, juce::Label& label, const juce::String& labelText,
                                  const juce::String& tooltip, float defaultValue,
                                  std::function<void(float)>& callback) {
        slider.setSliderStyle(juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 34, 20);
        slider.setColour(juce::Slider::textBoxTextColourId, Palette::textHi);
        slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        slider.setRange(0.0, 200.0, 5.0);
        slider.setValue(defaultValue, juce::dontSendNotification);
        slider.setTooltip(tooltip);
        slider.onValueChange = [&slider, &callback] {
            if (callback)
                callback(static_cast<float>(slider.getValue()));
        };
        addAndMakeVisible(slider);

        styleLabel(label, labelText, 14.f, Palette::textMid, juce::Justification::centredRight);
        label.setTooltip(tooltip);
        addAndMakeVisible(label);
    };
    setupSepSlider(rankSepSlider, rankSepLabel, "Rank Sep", "Spacing between columns (flow direction)", 60.0f,
                    onRankSepChanged);
    setupSepSlider(nodeSepSlider, nodeSepLabel, "Node Sep", "Spacing between lanes (rows)", 24.0f, onNodeSepChanged);

    startTimerHz(10);
}

void MappingSelectorBar::setResetButtonVisible(bool shouldBeVisible) {
    resetButton.setVisible(shouldBeVisible);
}

MappingSelectorBar::~MappingSelectorBar() {
    stopTimer();
}

void MappingSelectorBar::notifyMappingChanged() {
    if (onMappingChanged)
        onMappingChanged();
}

void MappingSelectorBar::refreshMappingCombo(int selectIndex) {
    mappingCombo.clear(juce::dontSendNotification);
    const int mappingCount = processor.getSynth().getMappingCount();
    for (int i = 0; i < mappingCount; ++i) {
        const char* name = processor.getSynth().getMappingName(i);
        if (name != nullptr)
            mappingCombo.addItem(name, i + 1);
    }
    mappingCombo.setSelectedId(selectIndex + 1, juce::dontSendNotification);
    updateMappingInfo();
}

void MappingSelectorBar::selectMapping(int index) {
    processor.setMappingStrategy(index);
    mappingCombo.setSelectedId(index + 1, juce::dontSendNotification);
    updateMappingInfo();
    notifyMappingChanged();
}

void MappingSelectorBar::updateMappingInfo() {
    const auto* mapping = processor.getSynth().getMapping(processor.getMappingStrategy());
    const juce::String description = mapping != nullptr ? mapping->getDescription() : juce::String();
    mappingCombo.setTooltip(description);
    descriptionLabel.setText(description, juce::dontSendNotification);
}

void MappingSelectorBar::timerCallback() {
    if (!mappingCombo.isPopupActive() && mappingCombo.getSelectedId() != processor.getMappingStrategy() + 1) {
        mappingCombo.setSelectedId(processor.getMappingStrategy() + 1, juce::dontSendNotification);
        updateMappingInfo();
        notifyMappingChanged();
    }
}

void MappingSelectorBar::promptForPresetName(const juce::String& title, const juce::String& initialValue,
                                              std::function<void(juce::String)> onConfirmed) {
    namePromptWindow = std::make_unique<juce::AlertWindow>(title, "Preset name:", juce::MessageBoxIconType::NoIcon);
    namePromptWindow->addTextEditor("name", initialValue);
    namePromptWindow->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey));
    namePromptWindow->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    namePromptWindow->enterModalState(true, juce::ModalCallbackFunction::create([this, onConfirmed](int result) {
        if (result == 1 && namePromptWindow != nullptr) {
            const juce::String name = namePromptWindow->getTextEditorContents("name").trim();
            if (name.isNotEmpty() && onConfirmed)
                onConfirmed(name);
        }
        namePromptWindow.reset();
    }), false);
}

void MappingSelectorBar::createNewPreset() {
    promptForPresetName("New Preset", "New Preset", [this](juce::String name) {
        auto& synth = processor.getSynth();
        const int newIndex = synth.addGraphMapping(name, std::make_unique<Graph::NodeGraph>());
        if (auto* newMapping = dynamic_cast<Graph::GraphMappingStrategy*>(synth.getMapping(newIndex))) {
            const auto file = Graph::PresetManager::instance().filePathForName(name);
            Graph::PresetManager::instance().savePreset(file, newMapping->getGraph());
            presetFileByMappingIndex[newIndex] = file;
        }
        refreshMappingCombo(newIndex);
        processor.setMappingStrategy(newIndex);
        notifyMappingChanged();
    });
}

void MappingSelectorBar::saveCurrentPreset() {
    const int index = processor.getMappingStrategy();
    auto it = presetFileByMappingIndex.find(index);
    if (it == presetFileByMappingIndex.end()) {
        // Built-in preset, or a preset never persisted this session - nothing to overwrite yet.
        saveCurrentPresetAs();
        return;
    }
    if (auto* graphMapping = dynamic_cast<Graph::GraphMappingStrategy*>(processor.getSynth().getMapping(index))) {
        Graph::PresetManager::instance().savePreset(it->second, graphMapping->getGraph());
        if (onPresetSaved)
            onPresetSaved();
    }
}

void MappingSelectorBar::saveCurrentPresetAs() {
    const int currentIndex = processor.getMappingStrategy();
    auto* graphMapping = dynamic_cast<Graph::GraphMappingStrategy*>(processor.getSynth().getMapping(currentIndex));
    if (graphMapping == nullptr)
        return;

    promptForPresetName("Save Preset As", juce::String(graphMapping->getName()), [this, graphMapping](juce::String name) {
        auto xml = graphMapping->getGraph().toXml();
        auto clonedGraph = xml != nullptr ? Graph::NodeGraph::fromXml(*xml) : nullptr;
        if (clonedGraph == nullptr)
            return;

        auto& synth = processor.getSynth();
        const int newIndex = synth.addGraphMapping(name, std::move(clonedGraph));
        if (auto* newMapping = dynamic_cast<Graph::GraphMappingStrategy*>(synth.getMapping(newIndex))) {
            const auto file = Graph::PresetManager::instance().filePathForName(name);
            Graph::PresetManager::instance().savePreset(file, newMapping->getGraph());
            presetFileByMappingIndex[newIndex] = file;
        }
        refreshMappingCombo(newIndex);
        processor.setMappingStrategy(newIndex);
        notifyMappingChanged();
    });
}

void MappingSelectorBar::showLoadPresetMenu() {
    auto& presetManager = Graph::PresetManager::instance();
    const auto files = presetManager.listPresetFiles();

    juce::PopupMenu menu;
    if (files.isEmpty())
        menu.addItem(1, "No presets found in " + presetManager.getPresetFolder().getFullPathName(), false);
    else
        for (int i = 0; i < files.size(); ++i)
            menu.addItem(i + 1, files.getReference(i).getFileNameWithoutExtension());

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(loadPresetButton),
        [this, files](int result) {
            if (result <= 0 || result > files.size())
                return;
            const juce::File file = files.getReference(result - 1);

            // Already loaded this session - just switch to it rather than adding a duplicate entry.
            for (auto& [index, existingFile] : presetFileByMappingIndex) {
                if (existingFile == file) {
                    selectMapping(index);
                    return;
                }
            }

            auto graph = Graph::PresetManager::instance().loadPreset(file);
            if (graph == nullptr)
                return;

            auto& synth = processor.getSynth();
            const int newIndex = synth.addGraphMapping(file.getFileNameWithoutExtension(), std::move(graph));
            presetFileByMappingIndex[newIndex] = file;
            refreshMappingCombo(newIndex);
            processor.setMappingStrategy(newIndex);
            notifyMappingChanged();
        });
}

void MappingSelectorBar::showOptionsMenu() {
    juce::PopupMenu menu;
    menu.addItem(1, "Preset Folder: " + Graph::PresetManager::instance().getPresetFolder().getFullPathName(), false);
    menu.addSeparator();
    menu.addItem(2, "Change Preset Folder...");
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(optionsButton), [this](int result) {
        if (result == 2)
            changePresetFolder();
    });
}

void MappingSelectorBar::changePresetFolder() {
    auto chooser = std::make_shared<juce::FileChooser>("Choose a preset folder",
                                                         Graph::PresetManager::instance().getPresetFolder());
    constexpr auto chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories;
    chooser->launchAsync(chooserFlags, [chooser](const juce::FileChooser& fc) {
        const auto result = fc.getResult();
        if (result != juce::File{})
            Graph::PresetManager::instance().setPresetFolder(result);
    });
}

void MappingSelectorBar::resized() {
    auto bounds = getLocalBounds().reduced(10, 4);
    auto topRow = bounds.removeFromTop(26);
    bounds.removeFromTop(4);
    auto bottomRow = bounds.removeFromTop(26);
    bounds.removeFromTop(4);
    descriptionLabel.setBounds(bounds);

    optionsButton.setBounds(topRow.removeFromRight(80));
    topRow.removeFromRight(10);
    loadPresetButton.setBounds(topRow.removeFromRight(90));
    topRow.removeFromRight(6);
    savePresetButton.setBounds(topRow.removeFromRight(90));
    topRow.removeFromRight(6);
    newPresetButton.setBounds(topRow.removeFromRight(70));
    topRow.removeFromRight(10);
    mappingCombo.setBounds(topRow.removeFromLeft(140));
    topRow.removeFromLeft(5);
    prevMapButton.setBounds(topRow.removeFromLeft(25));
    nextMapButton.setBounds(topRow.removeFromLeft(25));

    resetButton.setBounds(bottomRow.removeFromRight(110));
    bottomRow.removeFromRight(10);
    layoutButton.setBounds(bottomRow.removeFromRight(100));
    bottomRow.removeFromRight(10);
    nodeSepSlider.setBounds(bottomRow.removeFromRight(90));
    nodeSepLabel.setBounds(bottomRow.removeFromRight(60));
    bottomRow.removeFromRight(10);
    rankSepSlider.setBounds(bottomRow.removeFromRight(90));
    rankSepLabel.setBounds(bottomRow.removeFromRight(60));
    bottomRow.removeFromRight(10);
    globalVolumeLabel.setBounds(bottomRow.removeFromLeft(100));
    bottomRow.removeFromLeft(5);
    globalVolumeSlider.setBounds(bottomRow.removeFromLeft(200));
}
