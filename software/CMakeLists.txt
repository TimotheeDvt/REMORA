cmake_minimum_required(VERSION 3.22)
project(REMORA VERSION 0.1.0)
set(ENV{CMAKE_BUILD_PARALLEL_LEVEL} 4)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_subdirectory(JUCE)

juce_add_plugin(REMORA
    PLUGIN_MANUFACTURER_CODE  Nime      PLUGIN_CODE  Remo
    FORMATS                   VST3 Standalone
    PRODUCT_NAME              "REMORA"
    COMPANY_NAME              "NIME Project"
    BUNDLE_ID                 com.NIMEProject.REMORA
    ICON_BIG                  "Assets/logo.png"
    VST3_CATEGORIES           Tools
)

juce_generate_juce_header(REMORA)
juce_add_binary_data(REMORAData SOURCES Assets/logo.png)

file(GLOB_RECURSE SOURCE_FILES "Source/*.cpp" "Source/*.h")
target_sources(REMORA PRIVATE ${SOURCE_FILES})

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