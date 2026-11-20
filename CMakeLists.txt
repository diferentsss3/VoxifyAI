cmake_minimum_required(VERSION 3.22)
project(VoxifyAI VERSION 0.1.0)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.6
    GIT_SHALLOW ON)
FetchContent_MakeAvailable(JUCE)

if(DEFINED ENV{CI})
    set(VX_COPY FALSE)
else()
    set(VX_COPY TRUE)
endif()

juce_add_plugin(VoxifyAI
    COMPANY_NAME "Voxify"
    PRODUCT_NAME "Voxify AI"
    PLUGIN_MANUFACTURER_CODE Vxfy
    PLUGIN_CODE Vxai
    FORMATS VST3
    VST3_CATEGORIES Fx
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD ${VX_COPY})

target_sources(VoxifyAI PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/Common.h Source/Params.h Source/Presets.h Source/Analysis.h Source/Dsp.h)

target_compile_features(VoxifyAI PRIVATE cxx_std_17)
target_compile_definitions(VoxifyAI PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
if(MSVC)
    target_compile_options(VoxifyAI PRIVATE /utf-8)
endif()

target_link_libraries(VoxifyAI
    PRIVATE juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags)
