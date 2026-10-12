cmake_minimum_required(VERSION 3.22)
project(REMORA VERSION 0.1.0)
set(ENV{CMAKE_BUILD_PARALLEL_LEVEL} 4)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_subdirectory(JUCE)

juce_add_plugin(REMORA
    PLUGIN_MANUFACTURER_CODE  Nime      PLUGIN_CODE  Remo
    FORMATS                   Standalone
    PRODUCT_NAME              "REMORA"
    COMPANY_NAME              "NIME Project"
    BUNDLE_ID                 com.NIMEProject.REMORA
    ICON_BIG                  "Assets/logo.svg"
    VST3_CATEGORIES           Tools
)

juce_generate_juce_header(REMORA)
juce_add_binary_data(REMORAData SOURCES Assets/logo.svg)

file(GLOB_RECURSE SOURCE_FILES "Source/*.cpp" "Source/*.h")
target_sources(REMORA PRIVATE ${SOURCE_FILES})

# Timestamp marking the last time any preset source changed, so Graph::PresetManager::syncFactoryPreset
# can tell a stale on-disk FACTORY/*.xml from one a user has hand-edited since the build
file(GLOB PRESET_SOURCE_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/DSP/Graph/Presets/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/DSP/Graph/Presets/*.h")
set(PRESET_BUILD_VERSION 0)
foreach(presetSourceFile ${PRESET_SOURCE_FILES})
    file(TIMESTAMP "${presetSourceFile}" presetSourceFileTime "%s" UTC)
    if(presetSourceFileTime GREATER PRESET_BUILD_VERSION)
        set(PRESET_BUILD_VERSION ${presetSourceFileTime})
    endif()
endforeach()
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/DSP/Graph/Presets/PresetBuildVersion.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/PresetBuildVersion.h"
    @ONLY)
target_include_directories(REMORA PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")

target_compile_definitions(REMORA PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_options(REMORA PRIVATE -fuse-ld=mold)

target_link_libraries(REMORA
    PRIVATE REMORAData juce::juce_audio_utils juce::juce_osc juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags
)
# Console tool that builds every preset and prints its node composition (make preset-stats).
# Excluded from the default build so `make build` only compiles the plugin.
juce_add_console_app(REMORAPresetStats PRODUCT_NAME "REMORAPresetStats")
set_target_properties(REMORAPresetStats PROPERTIES EXCLUDE_FROM_ALL TRUE)
juce_generate_juce_header(REMORAPresetStats)
file(GLOB_RECURSE PRESET_STATS_DSP_FILES "Source/DSP/*.cpp" "Source/DSP/*.h")
target_sources(REMORAPresetStats PRIVATE
    Tools/PresetStats.cpp
    ${PRESET_STATS_DSP_FILES}
    Source/UI/DebugLog.cpp
    Source/UI/DebugConsole.cpp)
target_include_directories(REMORAPresetStats PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Source"
    "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_compile_definitions(REMORAPresetStats PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
)
target_link_options(REMORAPresetStats PRIVATE -fuse-ld=mold)
target_link_libraries(REMORAPresetStats
    PRIVATE juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags
)
