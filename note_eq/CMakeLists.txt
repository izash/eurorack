cmake_minimum_required(VERSION 3.22)
project(NoteEQ VERSION 0.1.0)

# Logic needs the plugin to run natively on both Apple Silicon and Intel Macs
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW ON)
FetchContent_MakeAvailable(JUCE)

# AU only exists on macOS; Linux/Windows builds just skip it
if(APPLE)
    set(NOTEEQ_FORMATS AU VST3 Standalone)
    set(NOTEEQ_COPY TRUE)    # installs to ~/Library/Audio/Plug-Ins/{Components,VST3}
else()
    set(NOTEEQ_FORMATS VST3 Standalone)
    set(NOTEEQ_COPY FALSE)
endif()

juce_add_plugin(NoteEQ
    COMPANY_NAME "izash"
    PLUGIN_MANUFACTURER_CODE Izsh
    PLUGIN_CODE NtEq
    FORMATS ${NOTEEQ_FORMATS}
    AU_MAIN_TYPE kAudioUnitType_Effect
    COPY_PLUGIN_AFTER_BUILD ${NOTEEQ_COPY}
    PRODUCT_NAME "Note EQ"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
)

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
