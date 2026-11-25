cmake_minimum_required(VERSION 3.22)

# Runtime Visual C++ intégré au plugin : l'utilisateur n'a rien d'autre à installer.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(TOPVOX_VOICE VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 7.0.12
    GIT_SHALLOW ON)

# Permet de réutiliser une copie locale de JUCE : -DTOPVOX_JUCE_DIR=chemin
if(TOPVOX_JUCE_DIR)
    add_subdirectory(${TOPVOX_JUCE_DIR} JUCE EXCLUDE_FROM_ALL)
else()
    FetchContent_MakeAvailable(JUCE)
endif()

set(TOPVOX_FORMATS VST3)
if(NOT TOPVOX_NO_STANDALONE)
    list(APPEND TOPVOX_FORMATS Standalone)
endif()

juce_add_plugin(TOPVOX_VOICE
    COMPANY_NAME "TOP"
    PLUGIN_MANUFACTURER_CODE Topx
    PLUGIN_CODE Tvox
    FORMATS ${TOPVOX_FORMATS}
    PRODUCT_NAME "TOPVOX VOICE"
    VST3_CATEGORIES Fx
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(TOPVOX_VOICE PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(TOPVOX_VOICE PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_compile_options(TOPVOX_VOICE PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

target_link_libraries(TOPVOX_VOICE
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
)
