cmake_minimum_required(VERSION 3.22)

project(VocalGzzio VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.12
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(VocalGzzio
    PLUGIN_MANUFACTURER_CODE  Gzzo
    PLUGIN_CODE               Vcgj
    COMPANY_NAME              "Gzzio"
    BUNDLE_ID                 "com.gzzio.vocalgzzio"
    FORMATS                   VST3 Standalone
    PRODUCT_NAME              "VocalGzzio"
    VST3_CATEGORIES           Fx Dynamics EQ
    IS_SYNTH                  FALSE
    NEEDS_MIDI_INPUT          FALSE
    NEEDS_MIDI_OUTPUT         FALSE)

juce_add_binary_data(VocalGzzioAssets
    SOURCES Resources/character.png)

target_sources(VocalGzzio PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(VocalGzzio PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(VocalGzzio PRIVATE
    VocalGzzioAssets
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)
