cmake_minimum_required(VERSION 3.22)
project(NIMEReceiver VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_subdirectory(JUCE)

juce_add_plugin(NIMEReceiver
    PLUGIN_MANUFACTURER_CODE  Nime      PLUGIN_CODE  NmRx
    FORMATS                   VST3 Standalone
    PRODUCT_NAME              "NIME OSC Receiver"
    COMPANY_NAME              "NIME Project"
    BUNDLE_ID                 com.NIMEProject.NIMEReceiver
    ICON_BIG                  "Assets/logo.png"
    VST3_CATEGORIES           Tools
)

juce_generate_juce_header(NIMEReceiver)
juce_add_binary_data(NIMEReceiverData SOURCES Assets/logo.png)

file(GLOB_RECURSE SOURCE_FILES "Source/*.cpp" "Source/*.h")
target_sources(NIMEReceiver PRIVATE ${SOURCE_FILES})

target_compile_definitions(NIMEReceiver PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_options(NIMEReceiver PRIVATE -fuse-ld=mold)

target_link_libraries(NIMEReceiver
    PRIVATE NIMEReceiverData juce::juce_audio_utils juce::juce_osc juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags
)