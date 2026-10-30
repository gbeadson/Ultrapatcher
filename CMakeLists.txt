cmake_minimum_required(VERSION 3.22)
project(Ultrapatcher VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(external/JUCE)

juce_add_plugin(Ultrapatcher
    COMPANY_NAME "Ultrapatcher"
    PLUGIN_MANUFACTURER_CODE Upat
    PLUGIN_CODE Upt1
    FORMATS VST3 Standalone
    PRODUCT_NAME "Ultrapatcher"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    VST3_CATEGORIES Fx
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(Ultrapatcher PRIVATE
    Source/PluginProcessor.cpp
    Source/PatchEngine.cpp
    Source/Mapping.cpp
    Source/PatchCanvas.cpp
    Source/SurfacePanel.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(Ultrapatcher PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_PLUGINHOST_VST3=1
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_USE_XINPUT=0)

target_link_libraries(Ultrapatcher
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
