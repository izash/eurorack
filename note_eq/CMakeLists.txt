cmake_minimum_required(VERSION 3.22)
project(NoteEQ VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW ON)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(NoteEQ
    COMPANY_NAME "izash"
    PLUGIN_MANUFACTURER_CODE Izsh
    PLUGIN_CODE NtEq
    FORMATS VST3 Standalone          # add AU on macOS: FORMATS AU VST3 Standalone
    PRODUCT_NAME "Note EQ"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(NoteEQ PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp)

target_include_directories(NoteEQ PRIVATE src)

target_compile_definitions(NoteEQ PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(NoteEQ PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

# DSP core has no JUCE dependency; this builds the standalone test
add_executable(NoteEQ_core_test tests/test_core.cpp)
target_include_directories(NoteEQ_core_test PRIVATE src)
