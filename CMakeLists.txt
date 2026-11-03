cmake_minimum_required(VERSION 3.22)
project(LoFiVinyl VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)

# Télécharge JUCE automatiquement au premier configure (git requis)
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(LoFiVinyl
    COMPANY_NAME "Home"
    PLUGIN_MANUFACTURER_CODE Home
    PLUGIN_CODE Lfvn
    FORMATS VST3 Standalone
    PRODUCT_NAME "LoFi Vinyl"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    # TRUE nécessite de lancer le build en administrateur (copie dans Program Files)
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(LoFiVinyl PRIVATE
    Source/PluginProcessor.h
    Source/PluginProcessor.cpp)

target_compile_definitions(LoFiVinyl PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(LoFiVinyl
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
